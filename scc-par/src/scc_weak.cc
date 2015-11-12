#include "gm.h"
#include "my_work_queue.h"
#include "scc.h"

static node_t* G_WCC;
static NODE_SET** wcc_sets; 
 static node_t* temp_buf; 
 static int* temp_buf_ptr; 

void check_WCC() {
    printf("wcc_sts = %p\n", wcc_sets);
}
node_t* get_WCC() {return G_WCC;}

static int pool_cnt=0;
static NODE_SET** node_set_pool;
NODE_SET* init_node_set_pool(int sz) {
    pool_cnt = sz;
    node_set_pool = new NODE_SET*[sz];
    for(int i =0;i<sz;i++)
        node_set_pool[i] = new NODE_SET();
}

NODE_SET* get_node_set_from_pool() {
    int index = __sync_add_and_fetch(&pool_cnt, -1);
    return node_set_pool[index];

    assert(pool_cnt > 0);
}

void initialize_WCC() {
    G_WCC = new node_t[G_num_nodes];
    wcc_sets = new NODE_SET*[G_num_nodes];
    assert(wcc_sets != NULL);
    init_node_set_pool(65536);

    #pragma omp parallel for
    for (node_t t4 = 0; t4 < G_num_nodes; t4 ++) 
    {
        G_WCC[t4] = gm_graph::NIL_NODE;
        wcc_sets[t4] = NULL;
    }

    int num_threads = gm_rt_get_num_threads();
   temp_buf = new node_t [num_threads * 2*1024*1024];
  temp_buf_ptr = new int [num_threads * 32];
    for(int i=0;i<num_threads;i++)
        temp_buf_ptr[i*32] = 0;
}

void finalize_WCC() {
    delete [] G_WCC;
    delete [] wcc_sets;
}


#if 0
#define INIT_WCC_ROOT_LG(t) (t) 
#define GET_WCC_ROOT(K) (G_WCC[K])
#else 
#define INIT_WCC_ROOT_LG(t) ((t) | (0x20000000))
#define GET_WCC_ROOT(K)     (G_WCC[K]&0x1FFFFFFF)
#endif

void propagate_color(gm_graph& G, std::vector<node_t>& wcc_candidate)
{
    int cnt = 0;
    // WCC
    bool finished;
    //printf("size = %ld\n",(long) wcc_candidate.size());

    do {
        finished = true;

#define M_TIME  0
#if M_TIME
        struct timeval T1, T2;
        gettimeofday(&T1, NULL);
#endif 

        #pragma omp parallel for schedule(dynamic, 32)
        for (int index = 0; index< wcc_candidate.size(); index++)
        {
            node_t n = wcc_candidate[index];
            node_t min_val = G_WCC[n];
            if (G_Color[n] == -2) continue;
            for (edge_t k_idx = G.begin[n];k_idx < G.begin[n+1] ; k_idx ++) 
            {
                node_t k = G.node_idx [k_idx];
                if (G_Color[k] != G_Color[n]) continue;
                if (G_WCC[k] < min_val) { 
                    min_val = G_WCC[k];
                    if (finished) finished = false;
                }
            }
            if (min_val != G_WCC[n]) G_WCC[n] = min_val;
        }


        #pragma omp parallel for 
        for (int index = 0; index< wcc_candidate.size(); index++)
        {
            node_t n = wcc_candidate[index];
            if (G_Color[n] == -2) continue;
            if (GET_WCC_ROOT(n) != n)
            {
                node_t root = GET_WCC_ROOT(n);
                if (GET_WCC_ROOT(root) != root) { // root has changed
                    G_WCC[n] = G_WCC[root];
                    if (finished) finished = false;
                }
            }
        }

#if M_TIME
        gettimeofday(&T2, NULL);
        printf("WCC: %6.3lf ms\n",  (T2.tv_usec-T1.tv_usec)*0.001 + (T2.tv_sec - T1.tv_sec)*10000);
#endif 
        cnt++;
        //printf("------\n");

    } while (!finished);

    //printf("cnt = %d\n", cnt);
}


void do_global_wcc(gm_graph& G)
{
    std::vector<node_t>& wcc_candidate = get_compact_trim_targets(); 

#if M_TIME
    struct timeval T1,T2;
        gettimeofday(&T1, NULL);
#endif

    #pragma omp parallel for
    for (node_t idx = 0; idx < wcc_candidate.size(); idx++)
    {
        node_t t4 = wcc_candidate[idx];
        if (G_Color[t4] != -2) {
            assert(t4 < 0x1FFFFFFF);
            if (G.begin[t4+1] - G.begin[t4] >= 50) {
                G_WCC[t4] = INIT_WCC_ROOT_LG(t4);
            } 
            else {
                G_WCC[t4] = t4; 
            }
        }
    }

#if M_TIME
        gettimeofday(&T2, NULL);
        printf("WCC1: %6.3lf ms\n",  (T2.tv_usec-T1.tv_usec)*0.001 + (T2.tv_sec - T1.tv_sec)*10000);
        gettimeofday(&T1, NULL);
#endif
    propagate_color(G, wcc_candidate);
    //propagate_color_long_diameter(G, wcc_candidate);

#if M_TIME
        gettimeofday(&T1, NULL);
#endif
    // Giving a Color
    #pragma omp parallel for
    for (int index = 0; index< wcc_candidate.size(); index++)
    {
        node_t t4 = wcc_candidate[index];
        if (G_Color[t4] == -2) continue;
        node_t root = GET_WCC_ROOT(t4);
        if (t4 == root) {
            G_Color[t4] = get_new_color();
        }
    }

#if M_TIME
        gettimeofday(&T2, NULL);
        printf("WCC3: %6.3lf ms\n",  (T2.tv_usec-T1.tv_usec)*0.001 + (T2.tv_sec - T1.tv_sec)*10000);
        gettimeofday(&T1, NULL);
#endif

    #pragma omp parallel for
    for (int index = 0; index< wcc_candidate.size(); index++)
    {
        node_t t4 = wcc_candidate[index];
        if (G_Color[t4] == -2) continue;
        node_t root = GET_WCC_ROOT(t4);
        if (t4!=root)
            G_Color[t4] = G_Color[root]; // copy root's color
    }
#if M_TIME
        gettimeofday(&T2, NULL);
        printf("WCC4: %6.3lf ms\n",  (T2.tv_usec-T1.tv_usec)*0.001 + (T2.tv_sec - T1.tv_sec)*10000);
        gettimeofday(&T1, NULL);
#endif

}

void create_work_items_from_wcc(gm_graph& G)
{
    std::vector<node_t>& wcc_candidate = get_compact_trim_targets(); 

#if M_TIME
    struct timeval T1,T2;
        gettimeofday(&T1, NULL);
#endif
    
    #pragma omp parallel for
    for (int index = 0; index< wcc_candidate.size(); index++)
    {
        node_t t4 = wcc_candidate[index];

        if (G_Color[t4] == -2) continue;
        node_t root = GET_WCC_ROOT(t4);
        if (root == t4) {
            wcc_sets[t4] = get_node_set_from_pool();
        }
    }
    
    #pragma omp parallel for  schedule(dynamic, 32)
    for (int index = 0; index< wcc_candidate.size(); index++)
    {
        node_t t4 = wcc_candidate[index];

        if (G_Color[t4] == -2) continue;
        node_t root = GET_WCC_ROOT(t4);
        if (root == gm_graph::NIL_NODE) continue;

        gm_spinlock_acquire_for_node(root);
        wcc_sets[root]->insert(t4);
        gm_spinlock_release_for_node(root);

    }


#if M_TIME
        gettimeofday(&T2, NULL);
        printf("WCC5: %6.3lf ms\n",  (T2.tv_usec-T1.tv_usec)*0.001 + (T2.tv_sec - T1.tv_sec)*10000);
        gettimeofday(&T1, NULL);
#endif
    // Create works
    #pragma omp parallel
    {
        std::vector<my_work*> small_works;
        #pragma omp for nowait schedule(dynamic,32)
        for (int index = 0; index< wcc_candidate.size(); index++)
        {
            node_t i = wcc_candidate[index];
            if (G_Color[i] == -2) continue;
            node_t root = GET_WCC_ROOT(i);
            if (root == i)
            {
                assert(wcc_sets[i]!= NULL);
                my_work* w1 = new my_work();
                w1->color = G_Color[i];
                w1->count = wcc_sets[i]->size();
                assert(w1->count > 0);
                w1->color_set = wcc_sets[i];
                small_works.push_back(w1);
            }
        }

        int tid = gm_rt_thread_id();
        work_q_put_all(tid, small_works);
    }
#if M_TIME
        gettimeofday(&T2, NULL);
        printf("WCC6: %6.3lf ms\n",  (T2.tv_usec-T1.tv_usec)*0.001 + (T2.tv_sec - T1.tv_sec)*10000);
        gettimeofday(&T1, NULL);
#endif

}

