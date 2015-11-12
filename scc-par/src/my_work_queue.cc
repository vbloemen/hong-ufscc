#include <omp.h>
#include <list>
#include <unistd.h>
#include "my_work_queue.h"
#include "scc.h"


static int max_th=1;
static int padding1[8];
static gm_spinlock_t q_lock = 0;
static int padding2[32];
static long work_sheet[MAX_THREADS * 8];
static int padding3[32];
static bool all_finished = false;
static bool work_queue_empty = true;
static int padding4[32];
std::vector<my_work*> the_q;
static int max_depth = 0;

int work_q_size() {return the_q.size();}

bool is_work_q_empty_from_seq_context() {return work_queue_empty;}

void work_q_init(int num_threads)
{
    q_lock = 0;
    for(int i=0;i<num_threads;i++)
        work_sheet[i*8] = 0;
    all_finished = false;
    work_queue_empty = true;
    max_th = num_threads;
    max_depth = 0;

}

static bool check_if_all_finished()
{
    bool b = true;
    if (!work_queue_empty) return false;

    gm_spinlock_acquire(&q_lock);
    if ((int)the_q.size() > 0) { // queue is not empty
        b = false;
        work_queue_empty = false;
    }
    else {
        for(int i=0;i<max_th;i++) { // somebody is working
            if (work_sheet[i*8] == 1) {
                b = false;
                break;
            }
        }
    }
    gm_spinlock_release(&q_lock);

    if (b) all_finished = b;
    return b;
}

static void my_sleep(int& sleep_cnt)
{
    if (sleep_cnt < 50000) {
        for(int i=0;i<800;i++) {
             asm volatile ("pause" ::: "memory");
        }
    }
    else if (sleep_cnt < 80000) {
        usleep(1); // sleep for 10 us
    }
    else if (sleep_cnt < 100000) {
        usleep(10); // sleep for 100 us
    } 
    /*else {
        usleep(1000); // sleep for 1 ms
    }*/

    sleep_cnt ++;
}

my_work* work_q_fetch(int id)
{
    int sleep_cnt = 1;

    // set idle
    work_sheet[id*8] = 0;

    while (true) {
        if (all_finished) return NULL;

        if (work_queue_empty) {
            if (id == 0) { // master
                bool b = check_if_all_finished();
                if (b) continue; // all finished
            }
            my_sleep(sleep_cnt);
            continue;
        }

        gm_spinlock_acquire(&q_lock);
        if ((int)the_q.size() == 0) {
            work_queue_empty = true;
            gm_spinlock_release(&q_lock);
            continue;
        }

        sleep_cnt = 1;
        my_work* ret = the_q.back();
        the_q.pop_back();
        work_sheet[id*8] = 1;

        if (the_q.size() == 0) work_queue_empty = true;
         
        gm_spinlock_release(&q_lock);

        return ret;
    }
}

void work_q_fetch_N(int id, int N, std::vector<my_work*>& works)
{
    int sleep_cnt = 1;

    // set idle
    work_sheet[id*8] = 0;

    while (true) {
        if (all_finished) return ;

        if (work_queue_empty) {
            if (id == 0) { // master
                bool b = check_if_all_finished();
                if (b) continue; // all finished
            }
            my_sleep(sleep_cnt);
            continue;
        }

        gm_spinlock_acquire(&q_lock);
        if ((int)the_q.size() == 0) {
            work_queue_empty = true;
            gm_spinlock_release(&q_lock);
            continue;
        }

        sleep_cnt = 1;
        int max = std::min(N,(int)the_q.size());
        for(int i = 0;i<max;i++) { 
            my_work* ret = the_q.back();
            the_q.pop_back();
            works.push_back(ret);
        }

        work_sheet[id*8] = 1;
        if (the_q.size() == 0) work_queue_empty = true;
         
        gm_spinlock_release(&q_lock);

        return;
    }
}


void work_q_put_all(int thread_id, std::vector<my_work*>& work)
{
    gm_spinlock_acquire(&q_lock);

    std::vector<my_work*>::iterator I;
    for(I=work.begin(); I!=work.end(); I++){
        the_q.push_back(*I);
        work_queue_empty = false;
    }

    int depth = the_q.size();
    if (depth > max_depth) max_depth = depth;

    gm_spinlock_release(&q_lock);
}

void work_q_print_max_depth() {printf("max depth = %d\n", max_depth);}


void work_q_put(int thread_id, my_work* w)
{
    gm_spinlock_acquire(&q_lock);

    the_q.push_back(w);
    work_queue_empty = false;

    int depth = the_q.size();
    if (depth > max_depth) max_depth = depth;

    gm_spinlock_release(&q_lock);
}

