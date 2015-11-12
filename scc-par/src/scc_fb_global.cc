#include "gm.h"
#include "my_work_queue.h"
#include "scc.h"

class thread_local_t {
public:
    int val0;
    int val1;
    int padding[16];
};

thread_local_t  thread_data[MAX_THREADS];

static int init_fw_color;
static int init_bw_color;
static int init_base_color;

void initialize_global_fb() 
{
    for(int i=0;i<gm_rt_get_num_threads(); i++)
    {
        thread_data[i].val0 = 0; thread_data[i].val1= 0;
    }
}


class fw_trim_global : public gm_bfs_template
// <level_t, use_multi_thread, has_navigator, use_reverse, save_child>
    <short, true, true, false, false>
{
public:
    fw_trim_global(gm_graph& _G, int32_t _base_color, int32_t _fw_color)
    : gm_bfs_template<short, true, true, false, false>(_G),
    G(_G), fw_color(_fw_color), base_color(_base_color) { 
        count = 0; 
    }

    int get_fw_count() {return count;}

private:  // list of varaibles
    gm_graph& G;
    int32_t base_color;
    int32_t fw_color;
    int count;

protected:
    virtual void visit_fw(node_t k) 
    {
        G_Color[k] = fw_color ;
        thread_data[gm_rt_thread_id()].val0 ++;
    }
    virtual void do_end_of_level_fw() {
        for(int i=0;i<gm_rt_get_num_threads();i++) {
            count += thread_data[i].val0; 
            thread_data[i].val0 = 0;
        }
    }

    virtual void visit_rv(node_t k9) {}
    virtual bool check_navigator(node_t k9, edge_t k9_idx) 
    {
        return (G_Color[k9] == base_color);
    }
};

class bw_trim_global : public gm_bfs_template
// <level_t, use_multi_thread, has_navigator, use_reverse_edgge, save_child>
    <short, true, true, true, false>
{
public:
    bw_trim_global(gm_graph& _G, int32_t _base_color, int32_t _fw_color, int32_t _bw_color, 
                                 node_t _pivot)
    : gm_bfs_template<short, true, true, true, false>(_G),
      G(_G), fw_color(_fw_color), base_color(_base_color), bw_color(_bw_color),
      pivot(_pivot) 
    { 
        count = 0; 
        scc_count = 0;
    }

    int get_bw_count() {return count;}
    int get_scc_count() {return scc_count;}

private:  // list of varaibles
    gm_graph& G;
    int32_t base_color;
    int32_t fw_color;
    int32_t bw_color;
    int count;
    int scc_count;
    node_t pivot;

protected:
    virtual void visit_fw(node_t k) 
    {
        if (G_Color[k] == fw_color)     // intersection
        {
            G_SCC[k] = pivot ;
            G_Color[k] = -2;
            thread_data[gm_rt_thread_id()].val1 ++;
            //scc_count++;
        }
        else {                          // bw-set
            G_Color[k] = bw_color;
            //count ++;
            thread_data[gm_rt_thread_id()].val0 ++;
        }
    }
    virtual void do_end_of_level_fw() {
        for(int i=0;i<gm_rt_get_num_threads();i++) {
            count += thread_data[i].val0; 
            scc_count += thread_data[i].val1; 
            thread_data[i].val0 = 0;
            thread_data[i].val1 = 0;
        }
    }

    virtual void visit_rv(node_t k10) {}
    virtual bool check_navigator(node_t k10, edge_t k10_idx) 
    {
        int color = G_Color[k10];
        return (color == fw_color) || (color == base_color) ;
    }
};

static my_work* base_work_item = NULL;
// returns SCC size
int do_fw_bw_global_main(gm_graph& G, int curr_color, int count, bool create_work_items )
{

    int base_color = curr_color;
    int base_count = count;

    node_t pivot;
    pivot = choose_pivot_from_color(G,base_color); 
    assert(pivot != gm_graph::NIL_NODE);
    assert(G_Color[pivot] == base_color);

    if (count == 1) {
        G_Color[pivot] = -2;
        G_SCC[pivot] = pivot;
        return 1;
    }

    int fw_color = get_new_color();
    int bw_color = get_new_color();


    // do FW BFS
    fw_trim_global FW_BFS(G, base_color, fw_color);
    FW_BFS.prepare(pivot, gm_rt_get_num_threads());
    FW_BFS.do_bfs_forward();

    // result
    int fw_count = FW_BFS.get_fw_count();

    // do BW BFS
    bw_trim_global BW_BFS(G, base_color, fw_color, bw_color, pivot);
    BW_BFS.prepare(pivot, gm_rt_get_num_threads());
    BW_BFS.do_bfs_forward();

    int bw_count = BW_BFS.get_bw_count();
    int scc_count = BW_BFS.get_scc_count();
    fw_count = fw_count - scc_count;
    base_count = base_count - fw_count - bw_count - scc_count;

    init_fw_color = fw_color;
    init_bw_color = bw_color;
    init_base_color = base_color;


    if (!create_work_items) return scc_count;

    //----------------------------------------------------------
    // create work items
    //----------------------------------------------------------
    int depth = 1;
    my_work* work;
    if (fw_count > 0) {
        work = new my_work();
        work->color = fw_color;
        work->count = fw_count;
        work->color_set = NULL; 
        work->depth = depth;
        work_q_put(gm_rt_thread_id(), work);
    }

    if (bw_count > 0) {
        work = new my_work();
        work->color = bw_color;
        work->count = bw_count;
        work->color_set = NULL;
        work->depth = depth;
        work_q_put(gm_rt_thread_id(), work);
    }

    if (base_count > 0) {
        work = new my_work();
        work->color = base_color;
        work->count = base_count;
        work->color_set = NULL;
        work->depth = depth;
        work_q_put(gm_rt_thread_id(), work);
        base_work_item = work;
    }

    return scc_count;
}

my_work* get_base_work_item() {return base_work_item; }

void create_works_after_bfs_trim(gm_graph& G)
{
    int fw_color = init_fw_color;
    int bw_color = init_bw_color;
    int base_color = init_base_color;

    int fw_count = 0;
    int bw_count = 0;
    int base_count = 0;

    std::vector<node_t>& V = get_compact_trim_targets(); 
#pragma omp parallel 
    {
        int local_fw_count = 0;
        int local_bw_count = 0;
        int local_base_count = 0;

        #pragma omp for 
        for(int i = 0; i < V.size(); i++) {
            node_t t = V[i];
            if (G_Color[t] == fw_color) local_fw_count++;
            else if (G_Color[t] == bw_color) local_bw_count++;
            else if (G_Color[t] == base_color) local_base_count++;
        }

        __sync_add_and_fetch(&fw_count, local_fw_count); // first add, then return
        __sync_add_and_fetch(&bw_count, local_bw_count); // first add, then return
        __sync_add_and_fetch(&base_count, local_base_count); // first add, then return
    }

    int depth = 1;
    my_work* work;
    if (fw_count > 0) {
        work = new my_work();
        work->color = fw_color;
        work->count = fw_count;
        work->color_set = NULL; 
        work->depth = depth;
        work_q_put(gm_rt_thread_id(), work);
    }

    if (bw_count > 0) {
        work = new my_work();
        work->color = bw_color;
        work->count = bw_count;
        work->color_set = NULL;
        work->depth = depth;
        work_q_put(gm_rt_thread_id(), work);
    }

    if (base_count > 0) {
        work = new my_work();
        work->color = base_color;
        work->count = base_count;
        work->color_set = NULL;
        work->depth = depth;
        work_q_put(gm_rt_thread_id(), work);
        base_work_item = work;
    }

}

