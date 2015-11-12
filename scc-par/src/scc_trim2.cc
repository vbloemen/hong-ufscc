#include "scc.h"

static node_t* G_nbr;
static bool* G_maybe_2nd;

void initialize_trim2() {
    G_nbr = new node_t[G_num_nodes];
    G_maybe_2nd = new bool[G_num_nodes];

#pragma omp parallel for
    for(int i=0;i<G_num_nodes;i++) {
        G_nbr[i] = gm_graph::NIL_NODE;
        G_maybe_2nd[i] = false;
    }
}

void finalize_trim2() {
    delete [] G_nbr;
    delete [] G_maybe_2nd;
}



inline static void trim_2nd_main1(gm_graph& G, int curr_color, int& count, node_t n)
{
    if (G_Color[n] != curr_color) return;

    int out_degree = 0;
    G_maybe_2nd[n] = false;
    G_nbr[n] = gm_graph::NIL_NODE;
    node_t last_seen2 = gm_graph::NIL_NODE;

    for (edge_t k_idx = G.begin[n];k_idx < G.begin[n+1] ; k_idx ++) 
    {
        node_t k = G.node_idx [k_idx];
        if (k==n) continue;
        if (k == last_seen2) continue;
        if (G_Color[k] == curr_color){
            if (out_degree == 0) {
                last_seen2 = k;
                out_degree = 1;
            }
            else if (out_degree == 1) {
                out_degree = 2;
                last_seen2 = gm_graph::NIL_NODE;
                break;
            }
        }
    }

    int in_degree = 0;
    node_t last_seen = gm_graph::NIL_NODE;
    for (edge_t k_idx = G.r_begin[n];k_idx < G.r_begin[n+1] ; k_idx ++) 
    {
        node_t k = G.r_node_idx [k_idx];
        if (k==n) continue;
        if (k == last_seen) continue; 
        if (G_Color[k] == curr_color){

            if (in_degree == 0) {
                in_degree = 1;
                last_seen = k;
            }
            else if (in_degree == 1) {
                 // k != last_seen 
                 in_degree = 2;
                 last_seen = gm_graph::NIL_NODE;
                 break;
            }
        }
    }

    if ((in_degree == 0) || (out_degree == 0))
    {


    }
    else if ((in_degree == 1) && (out_degree == 1))
    {
        if (last_seen == last_seen2) {
            G_nbr[n] = last_seen;
            count ++;
        }
    }
    else if ((in_degree == 1) || (out_degree == 1))
    {
        G_maybe_2nd[n] = true;
    }
}


inline static void trim_2nd_main2(gm_graph& G, int curr_color, int& count, node_t n)
{
    if (G_Color[n] != curr_color) return;
    if (G_nbr[n] == gm_graph::NIL_NODE) return;

    node_t k = G_nbr[n];
    if (G_nbr[k] != gm_graph::NIL_NODE)  {
        G_SCC[n] = (n < k) ? n : k;
        G_Color[n] = -2;
        count ++;
    }
    else if (G_maybe_2nd[k]) {
        G_SCC[n] = n;
        G_Color[n] = -2;

        G_SCC[k] = n;
        G_Color[k] = -2;
        count += 2;
    }
}


int do_global_trim2(gm_graph& G)
{
    std::vector<node_t> &V = get_compact_trim_targets() ;
    int count = 0;
    #pragma omp parallel
    {
        int count_prv = 0;

        #pragma omp for nowait schedule(dynamic,512)
        for (node_t x = 0; x < V.size(); x ++) 
        {
            node_t n = V[x];
            int curr_color = G_Color[n];
            if (curr_color == -2) continue;
            trim_2nd_main1(G, curr_color, count_prv, n);
        }
        __sync_fetch_and_add(&count, count_prv);
    }

    if (count ==0) return count;
    count = 0;

    #pragma omp parallel
    {
        int count_prv = 0;

        #pragma omp for nowait schedule(dynamic, 512)
        for (node_t x = 0; x < V.size(); x ++)  {
            node_t n = V[x];
            int curr_color = G_Color[n];
            if (curr_color == -2) continue;
            trim_2nd_main2(G, curr_color, count_prv, n);
        }

        __sync_fetch_and_add(&count, count_prv);
    }

    return count;
}


int repeat_global_trim2(gm_graph& G, int exit_count) 
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


