#include "scc.h"

inline static bool check_out_degree_is_one(gm_graph& G, int curr_color, node_t n, node_t& the_nbr)
{
    the_nbr = gm_graph::NIL_NODE;
    int cnt = 0;

    for (edge_t k_idx = G.begin[n];k_idx < G.begin[n+1] ; k_idx ++) 
    {
        node_t k = G.node_idx [k_idx];
        if (k==n) continue; // self edge
        if (k==the_nbr) continue; // repeated edge
        if (G_Color[k] != curr_color) continue; 
        cnt ++;
        the_nbr = k;
        if (cnt == 2) return false;
    }
    if (cnt==1) return true;
    return false;
}

inline static bool check_in_degree_is_one(gm_graph& G, int curr_color, node_t n, node_t& the_nbr)
{
    the_nbr = gm_graph::NIL_NODE;
    int cnt = 0;

    for (edge_t k_idx = G.r_begin[n];k_idx < G.r_begin[n+1] ; k_idx ++) 
    {
        node_t k = G.r_node_idx [k_idx];
        if (k==n) continue; // self edge
        if (k==the_nbr) continue; // repeated edge
        if (G_Color[k] != curr_color) continue; 
        cnt ++;
        the_nbr = k;
        if (cnt == 2) return false;
    }
    if (cnt==1) return true;
    return false;
}


inline static void trim_2nd_new_main(gm_graph& G, int curr_color, int& count, node_t n)
{
    node_t k;

    if (G_Color[n] != curr_color) return;

    if (check_out_degree_is_one(G, curr_color, n, k))
    {
        if (n < k) {
            node_t kk;
            if (check_out_degree_is_one(G, curr_color, k,kk)) {
                if (kk == n) {
                    count += 2;
                    G_Color[n] = G_Color[k] = -2;
                    G_SCC[n] = G_SCC[k] = n;
                    return;
                }
            }
        }
    }

    if (check_in_degree_is_one(G, curr_color, n, k))
    {
        if (n < k) {
            node_t kk;
            if (check_in_degree_is_one(G, curr_color, k,kk)) {
                if (kk == n) {
                    count += 2;
                    G_Color[n] = G_Color[k] = -2;
                    G_SCC[n] = G_SCC[k] = n;
                    return;
                }
            }
        }
    }
}


int do_global_trim2_new(gm_graph& G)
{
    std::vector<node_t> &V = get_compact_trim_targets() ;
    int count = 0;
    #pragma omp parallel
    {
        int count_prv = 0;

        #pragma omp for nowait
        for (node_t x = 0; x < V.size(); x ++) 
        {
            node_t n = V[x];
            int curr_color = G_Color[n];
            if (curr_color == -2) continue;
            trim_2nd_new_main(G, curr_color, count_prv, n);
        }
        __sync_fetch_and_add(&count, count_prv);
    }

    if (count ==0) return count;
    return count;
}


int repeat_global_trim2_new(gm_graph& G, int exit_count) 
{
    int count = 0;
    int count_this = 0;
    do {
        count_this = do_global_trim2(G);
        count+= count_this;
        printf("trim2 = %d\n", count_this);
    } while (count_this > exit_count);

    return count;
}
