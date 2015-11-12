#include "gm.h"
#include "scc.h"

int32_t* G_Color;
extern int32_t G_num_nodes;
static int _the_color; 

static int the_colors[MAX_THREADS*16]; 

#define MAX_COLOR_PER_THREAD        0x100000

void initialize_color()
{
    G_Color = new int32_t[G_num_nodes];

#pragma omp parallel for
    for(int i =0; i<G_num_nodes; i++)
    {
        G_Color[i] = -1;
    }
    _the_color = -1;

    for(int i =0; i < gm_rt_get_num_threads();i++) 
    {
        the_colors[i*16+0] = -1;  // used
        the_colors[i*16+1] = -1;  // max_assigned
    }
}

void finalize_color()
{
    delete [] G_Color;
}

int get_new_color() 
{
    const int CHUNK=1024;
    int tid = gm_rt_thread_id();
    int used         = the_colors[tid*16+0];
    int max_assigned = the_colors[tid*16+1];
    if (used == max_assigned) {
        max_assigned = the_colors[tid*16+1] =  __sync_add_and_fetch(&_the_color, CHUNK); // assign 32 new colors
        used = the_colors[tid*16+0] =  max_assigned - CHUNK + 1;
    }
    else {
        used = ++the_colors[tid*16+0]; 
    }

    return used;

}

int get_curr_color()
{
    int tid = gm_rt_thread_id();
    assert(tid==0);
    return the_colors[0];
}


node_t choose_pivot_from_color(gm_graph& G, int color)
{
    std::vector<node_t>& V = get_compact_trim_targets();
    if (V.size() == 0) {
        for(node_t i=0;i<G.num_nodes(); i++)
        {
            if (G_Color[i] == color) return i;
        }
    } else {
        for(node_t j=0;j<V.size(); j++)
        {
            node_t i = V[j];
            if (G_Color[i] == color) return i;
        }

    }
    return gm_graph::NIL_NODE;
}

NODE_SET* generate_compact_set(gm_graph& G, int color)
{
    NODE_SET* S = new NODE_SET();
    std::vector<node_t>& V = get_compact_trim_targets();
    if (V.size() == 0) {
        for(node_t i=0;i<G.num_nodes(); i++)
        {
            if (G_Color[i] == color) S->insert(i);
        }
    } else {
        for(node_t j=0;j<V.size(); j++)
        {
            node_t i = V[j];
            if (G_Color[i] == color) S->insert(i);
        }
    }

    return S;
}
