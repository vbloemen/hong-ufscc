#include "gm.h"
#include "scc.h"
#include "my_work_queue.h"

static std::vector<node_t> trim_targets;
static std::vector<node_t> L[MAX_THREADS];
int sz[MAX_THREADS];

void initialize_trim1() {
    trim_targets.clear();
    trim_targets.reserve(G_num_nodes);
    for(int i=0;i<gm_rt_get_num_threads();i++)
    {
        sz[i] = 0;
        L[i].reserve(4*1024*1024);
    }
}

std::vector<node_t>& get_compact_trim_targets() { return trim_targets;}


//------------------------------------------------------------------------------
// Array + Set + BFS + WeakCC
//------------------------------------------------------------------------------
inline static void trim_once_node(gm_graph& G, int curr_color, int& count, node_t n)
{
    if (G_Color[n] != curr_color) return;

    int degree = 0;

    // check if its neighbors are all zero
    for (edge_t k_idx = G.begin[n];k_idx < G.begin[n+1] ; k_idx ++) 
    {
        node_t k = G.node_idx [k_idx];
        if (k==n) continue;
        if (G_Color[k] == curr_color){
            degree = 1; break;
        }
    }

    if (degree == 0) {
        G_SCC[n] = n;
        G_Color[n] = -2;
        count++;
        return;
    }

    degree = 0;
    for (edge_t k_idx = G.r_begin[n];k_idx < G.r_begin[n+1] ; k_idx ++) 
    {
        node_t k = G.r_node_idx [k_idx];
        if (k==n) continue;
        if (G_Color[k] == curr_color){
            degree = 1; break;
        }
    }
    if (degree == 0) {
        G_SCC[n] = n;
        G_Color[n] = -2;
        count++;
        return;
    }
}

int do_global_trim1(gm_graph& G)
{
    int count = 0;
    //struct timeval T1,T2;
    //gettimeofday(&T1,NULL);
    #pragma omp parallel
    {
        int count_prv = 0;

        #pragma omp for nowait schedule(dynamic,512)
        for (node_t n = 0; n < G.num_nodes(); n ++) 
        {
            if (G_Color[n] == -2) continue; // already
            int curr_color = G_Color[n];
            trim_once_node(G, curr_color, count_prv, n);
        }
        __sync_fetch_and_add(&count, count_prv);
    }
    //gettimeofday(&T2,NULL);
    //printf("%lf (ms)\n", (T2.tv_sec - T1.tv_sec)*1000 + (T2.tv_usec - T1.tv_usec)*0.001);

    return count;
}

int do_global_trim1_compact(gm_graph& G)
{

    int count = 0;
    //struct timeval T1,T2;
    //gettimeofday(&T1,NULL);
    //printf("size = %d\n", trim_targets.size());
    #pragma omp parallel
    {
        int count_prv = 0;

        #pragma omp for nowait schedule(dynamic,512)
        for (node_t ix = 0; ix < trim_targets.size(); ix ++) 
        {
            node_t n = trim_targets[ix];
            if (G_Color[n] == -2) continue; // already
            int curr_color = G_Color[n];
            trim_once_node(G, curr_color, count_prv, n);
        }
        __sync_fetch_and_add(&count, count_prv);
    }
    //gettimeofday(&T2,NULL);
    //printf("%lf (ms)\n", (T2.tv_sec - T1.tv_sec)*1000 + (T2.tv_usec - T1.tv_usec)*0.001);

    return count;
}


int do_local_trim1(gm_graph& G, my_work* w)
{
    int count = 0;
    NODE_SET* set = w->color_set;
    int curr_color = w->color;
    if (set != NULL)
    {
        NODE_SET::iterator I;
        for(I=set->begin(); I!=set->end(); I++) {
            trim_once_node(G, curr_color, count, *I);
        }
        for(I=set->begin(); I!=set->end(); I++) {
            if (G_Color[*I] != curr_color)
                set->erase(I);
        }
    }
    else 
    {
        for (node_t n = 0; n < G.num_nodes(); n ++) 
        {
            if (G_Color[n] == curr_color) {
                trim_once_node(G, curr_color, count, n);
            }
        }
    }

    w->count -= count;
    return count;
}

int repeat_global_trim1(gm_graph& G, int TRIM_STOP)
{
    int total_count = 0;

    int count=0;
    do {
        count = do_global_trim1(G);
        total_count += count;
        if (total_count >= G.num_nodes() * 0.1) {
            return total_count + repeat_global_trim1_compact(G);
        }
    } while (count > TRIM_STOP);

    return total_count;
}

static void create_trim1_compact_1(gm_graph& G)
{
    assert(trim_targets.size() == 0);
    for(int i=0;i<gm_rt_get_num_threads();i++) L[i].clear();

    #pragma omp parallel 
    { 
        int tid = gm_rt_thread_id();
        #pragma omp for nowait schedule(dynamic, 2048)
        for(int i=0;i<G.num_nodes();i++) {
            if (G_Color[i] == -2) continue; // already
            L[tid].push_back(i);
        }
    }
}
static void create_trim1_compact_1b(gm_graph& G)
{
    assert(trim_targets.size() != 0);
    for(int i=0;i<gm_rt_get_num_threads();i++) L[i].clear();

    #pragma omp parallel 
    { 
        int tid = gm_rt_thread_id();
        #pragma omp for nowait 
        for(int i=0;i<trim_targets.size();i++) 
        {
            node_t t = trim_targets[i];
            if (G_Color[t] == -2) continue; // already
            L[tid].push_back(t);
        }
    }

}
   
static void create_trim1_compact_2()
{
    trim_targets.clear();
    sz[0] = L[0].size();
    for(int i = 1; i < gm_rt_get_num_threads(); i++) 
    {
        sz[i] = sz[i-1] + L[i].size();
    }
    trim_targets.resize(sz[gm_rt_get_num_threads()-1]);
}

static void create_trim1_compact_3()
{
    #pragma omp parallel
    {
        int tid = gm_rt_thread_id();
        int begin = (tid == 0) ? 0 : sz[tid -1];
        memcpy(&trim_targets[begin],&(L[tid][0]), sizeof(node_t)*(L[tid].size()));
    }
}

static void create_trim1_compact(gm_graph& G)
{
    if (trim_targets.size() == 0) 
        create_trim1_compact_1(G);
    else 
        create_trim1_compact_1b(G);

    create_trim1_compact_2();
    create_trim1_compact_3();
}

int repeat_global_trim1_compact(gm_graph& G, int TRIM_STOP)
{
    create_trim1_compact(G);

    int total_count = 0;

    int count;
    do {
        count = do_global_trim1_compact(G);
        total_count += count;
    } while (count > TRIM_STOP);

    return total_count;
}

int repeat_local_trim1(gm_graph& G, my_work* w)
{

    int total_count = 0;
    int count;
    do {
        count = do_local_trim1(G,w);
        total_count += count;
    } while (count > 0);

    return total_count;
}
