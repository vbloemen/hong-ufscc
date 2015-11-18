#include "gm.h"
#include "my_work_queue.h"
#include "scc.h"

#define DEBUG 0
static node_t TARGET_NODE = 898;
static node_t TARGET_ROOT = 0;


class fw_trim_dfs : public gm_dfs_template
    <true, false, true, false>
{
public:
    fw_trim_dfs(gm_graph& _G, int32_t _base_color, int32_t _fw_color, NODE_SET* _base_set)
    : gm_dfs_template<true,false,true,false>(_G),
    G(_G), fw_color(_fw_color), base_color(_base_color), base_set(_base_set) { 
        count = 0; 
        fw_set = new NODE_SET ();
    }

    int get_fw_count() {return count;}

    // return the result set 
    NODE_SET*  get_fw_set() { 
        if ((fw_set != NULL) && (fw_set->size() != 0)) 
            return fw_set;

        if (fw_set != NULL) {
            delete fw_set;
            fw_set = NULL;
        }

        return NULL;
    }

private:  // list of varaibles
    gm_graph& G;
    int32_t base_color;
    int32_t fw_color;
    int count;

    NODE_SET* fw_set;
    NODE_SET* base_set;

protected:
    virtual void visit_pre(node_t k) 
    {
#if DEBUG
        if ((k==TARGET_NODE) || (get_root() == TARGET_ROOT)) {
            printf("fw visiting:%d, old_color = %d, new_color= %d, pivot=%d [level:%d]\n", k, G_Color[k], fw_color, get_root(), get_curr_level());
        }
#endif
        G_Color[k] = fw_color ;
        count ++;
        if (base_set != NULL) base_set->erase(k);
        if (fw_set != NULL) {
            if (fw_set->size() >= G_num_nodes * 0.01) {
                delete fw_set;
                fw_set = NULL;
            }
            else {
                fw_set->insert(k);
            }
        }
    }

    virtual void visit_post(node_t k9) {}
    virtual bool check_navigator(node_t k9, edge_t k9_idx) 
    {
#if DEBUG
        if ((k9==TARGET_NODE) || (get_root() == TARGET_ROOT)) {
            printf("navigating:%d, color:%d, result:%c, pivod=%d\n", k9, G_Color[k9], (G_Color[k9] == base_color) ? 'Y':'N', get_root());
        }
#endif
        return (G_Color[k9] == base_color);
    }
};

class bw_trim_dfs : public gm_dfs_template
    <true, false, true, true>
{
public:
    bw_trim_dfs(gm_graph& _G, int32_t _base_color, int32_t _fw_color, int32_t _bw_color, 
                                 NODE_SET* _base_set, NODE_SET* _fw_set, node_t _pivot)
    : gm_dfs_template<true,false,true,true>(_G),
      G(_G), fw_color(_fw_color), base_color(_base_color), bw_color(_bw_color),
      base_set(_base_set), fw_set(_fw_set), pivot(_pivot) 
    { 
        count = 0; 
        scc_count = 0;
        bw_set = new NODE_SET ();
    }

    int get_bw_count() {return count;}
    int get_scc_count() {return scc_count;}

    // return the result set 
    NODE_SET*  get_bw_set() { 
        if ((bw_set != NULL) && (bw_set->size() != 0)) 
            return bw_set;

        if (bw_set != NULL) {
            delete bw_set;
            bw_set = NULL;
        }
        return NULL;
    }

    NODE_SET*  get_fw_set() { 
        if ((fw_set != NULL) && (fw_set->size() != 0)) 
            return fw_set;

        if (fw_set != NULL) {
            delete fw_set;
            fw_set = NULL;
        }
        return NULL;
    }

private:  // list of varaibles
    gm_graph& G;
    int32_t base_color;
    int32_t fw_color;
    int32_t bw_color;
    int count;
    int scc_count;
    node_t pivot;

    NODE_SET* fw_set;
    NODE_SET* bw_set;
    NODE_SET* base_set;

protected:
    virtual void visit_pre(node_t k) 
    {
        if (G_Color[k] == fw_color)     // intersection
        {
#if DEBUG
        if ((k==TARGET_NODE) || (get_root() == TARGET_ROOT)) {
            printf("scc visiting:%d, pivot = %d [level:%d]\n", k, pivot, get_curr_level());
        }
#endif
            G_SCC[k] = pivot ;
            G_Color[k] = -2;
            scc_count++;
            if (fw_set != NULL)         // remove from FW SET
                fw_set->erase(k);
        }
        else {                          // bw-set
#if DEBUG
        if ((k==TARGET_NODE) || (get_root() == TARGET_ROOT)) {
            printf("bw visiting:%d, old_color = %d, new_color= %d, pivot=%d [level:%d]\n", k, G_Color[k], bw_color, pivot, get_curr_level());
        }
#endif
            G_Color[k] = bw_color;
            count ++;
            if (base_set != NULL) base_set->erase(k);

            if (bw_set != NULL) {
                if (bw_set->size() >= (G_num_nodes * 0.01)) {
                    delete bw_set;
                    bw_set = NULL;
                }
                else {
                    bw_set->insert(k);
                }
            }
        }
    }

    virtual void visit_post(node_t k10) {}
    virtual bool check_navigator(node_t k10, edge_t k10_idx) 
    {
        int color = G_Color[k10];
        return (color == fw_color) || (color == base_color) ;
    }
};

// returns number of SCC
int do_fw_bw_dfs(gm_graph& G, my_work* work, std::vector<my_work*>& new_works )
{
    if (work->count ==0) {
        return 0;
    }

    NODE_SET* base_set = work->color_set;
    int base_color = work->color;
    int base_count = work->count;

    
    // choose pivot
    node_t pivot;
    if (base_set==NULL) {
        //printf("count = %d\n", base_count);
        pivot = choose_pivot_from_color(G,base_color); 
        assert(pivot != gm_graph::NIL_NODE);
    } else {
        if (base_set->size() == 0) {
            delete base_set; base_set = NULL;
            pivot = choose_pivot_from_color(G, base_color); 
            assert(pivot != gm_graph::NIL_NODE);
        }
        else {
            pivot = *(base_set->begin());
            assert(pivot != gm_graph::NIL_NODE);
        }
    }
    
    assert(pivot != gm_graph::NIL_NODE);
    assert(G_Color[pivot] == base_color);

    if (work->count ==1) 
    {
        G_Color[pivot] = -2;
        G_SCC[pivot] = pivot;
        delete base_set;
        return 1;
    }

    int fw_color = get_new_color();
    int bw_color = get_new_color();
    //printf("base color = %d, fw_color = %d, bw_color = %d\n", base_color, fw_color, bw_color);


    // do FW BFS
    fw_trim_dfs FW_BFS(G, base_color, fw_color, base_set);
    FW_BFS.prepare(pivot);
    FW_BFS.do_dfs();

    // result
    int fw_count = FW_BFS.get_fw_count();
    NODE_SET* fw_set = FW_BFS.get_fw_set();

    // do BW BFS
    bw_trim_dfs BW_BFS(G, base_color, fw_color, bw_color, base_set, fw_set, pivot);
    BW_BFS.prepare(pivot);
    BW_BFS.do_dfs();

    //----------------------------------------------------------
    // check the result
    //----------------------------------------------------------
    fw_set = BW_BFS.get_fw_set();
    NODE_SET* bw_set = BW_BFS.get_bw_set();
    if ((base_set != NULL) && (base_set->size() == 0)) {
        delete base_set;
        base_set = NULL;
    }

    int bw_count = BW_BFS.get_bw_count();
    int scc_count = BW_BFS.get_scc_count();

    fw_count = fw_count - scc_count;
    base_count = base_count - fw_count - bw_count - scc_count;

    //----------------------------------------------------------
    // Return new work items
    //----------------------------------------------------------
    int depth = work->depth + 1;
    if (fw_count > 0) {
        work = new my_work();
        work->color = fw_color;
        work->count = fw_count;
        work->color_set = fw_set; 
        work->depth = depth;
        new_works.push_back(work);
        if (fw_set!=NULL) {assert(fw_set->size() == fw_count);}
    }

    if (bw_count > 0) {
        work = new my_work();
        work->color = bw_color;
        work->count = bw_count;
        work->color_set = bw_set;
        work->depth = depth;
        new_works.push_back(work);
        if (bw_set!=NULL) {assert(bw_set->size() == bw_count);}
    }
    if (base_count > 0) {
        work = new my_work();
        work->color = base_color;
        work->count = base_count;
        work->color_set = base_set;
        work->depth = depth;
        new_works.push_back(work);
        if (base_set!=NULL) {assert(base_set->size() == base_count);}
    }

    return scc_count;
}


void start_workers_fw_bw_dfs(gm_graph& G, int N)
{
    #pragma omp parallel
    {
        int tid = gm_rt_thread_id();
        std::vector<my_work*> new_works;
        std::vector<my_work*> my_works;
        my_works.reserve(4096);
        //struct timeval T1, T2;
        //float t_tot  =0;
        //gettimeofday(&T1, NULL);
        while (true) {

            if (my_works.size() == 0) {
                //gettimeofday(&T2, NULL);
                //if (tid == 0)
                //    printf("ttot  ms = %6.3lf, \n", t_tot);
                //t_tot += (T2.tv_sec - T1.tv_sec) * 1000 + (T2.tv_usec - T1.tv_usec)*0.001;
                work_q_fetch_N(tid, std::max(N/2,1), my_works);
                //gettimeofday(&T1, NULL);
            }

            // done, no more work
            if (my_works.size() == 0) break;
            //if (w == NULL) break; 
            //
            while (my_works.size() > 0) {
                my_work*w = my_works.back();
                my_works.pop_back();

                //printf("size = %d, set = %p\n", w->count, w->color_set);

                //if ((w->count < G.num_nodes() * 0.01) && (w->color_set == NULL)) 
                //if ((w->count < G.num_nodes() * 0.1) && (w->color_set == NULL)) 
                if ((w->count < G.num_nodes() * 1) && (w->color_set == NULL)) 
                {
                    //printf("size:%d create compact set\n", w->count);
                    w->color_set = generate_compact_set(G, w->color);
                }

                do_fw_bw_dfs(G, w, new_works);

                delete w; // delete old work
                w = NULL;

                while ((my_works.size() < N) && (new_works.size() > 0)) {
                    my_work* w = new_works.back();
                    new_works.pop_back();
                    my_works.push_back(w);
                }

                if (new_works.size() > 0) {
                    // remaining jobs for others
                    work_q_put_all(tid, new_works);
                    new_works.clear();
                }
            }
        }
    } // end of omp parallel
}
