
#include "gm.h"
#include "scc.h"
#include "my_work_queue.h"
#include <list>
#include <sys/resource.h>

class simple_stack {
    public:
        virtual void push(node_t n) = 0;
        virtual int size() = 0;
        virtual node_t top()=0;
        virtual void pop()=0;
};

class simple_stack_array : public simple_stack {
    public:
        simple_stack_array(int max_sz) {
            stack = new int32_t[max_sz];
            stack_ptr = 0;
        }
        virtual ~simple_stack_array() { delete [] stack;}


    inline void push(node_t n) {
        stack[stack_ptr] = n;
        stack_ptr ++;
    }
    inline int size() {return stack_ptr;}
    inline node_t top() {return stack[stack_ptr-1];}
    inline void pop() {stack_ptr--;}

    int32_t* get_ptr() {return stack;}
private:
    int32_t* stack;
    int32_t stack_ptr;
};

class simple_stack_vector : public simple_stack  {
    public:
        simple_stack_vector(int max_sz) {
            stack.reserve(max_sz>1024?max_sz:1024);
        }
        virtual ~simple_stack_vector() { }

    inline void push(node_t n) { stack.push_back(n); }
    inline int size() {return (int) stack.size();}
    inline node_t top() {return stack[stack.size()-1];}
    inline void pop() {stack.pop_back();}

private:
    std::vector<node_t> stack;
};

// globals for this file
static int32_t* G_lowlink = NULL;
static int32_t* G_instack = NULL;

simple_stack_array* the_stack;


void initialize_tarjan()
{
    G_lowlink = new int32_t[G_num_nodes];
    G_instack = new int32_t[G_num_nodes];

    // need enough program stack size for recursion
    {
        const rlim_t kStackSize = 4096 * 1024L * 1024L;  
        struct rlimit rl;
        int result;

        result = getrlimit(RLIMIT_STACK, &rl);
        assert(result == 0);
        if (rl.rlim_cur < kStackSize)
        {
            rl.rlim_cur = kStackSize;
            result = setrlimit(RLIMIT_STACK, &rl);
            assert(result == 0);
        }
    }
    the_stack = new simple_stack_array(G_num_nodes);

#pragma omp parallel for
    for(int i = 0;i < G_num_nodes; i++) 
    {
        G_lowlink[i] = -1;
        G_instack[i] = -2;
        the_stack->get_ptr()[i] = 0;
    }
}

void finalize_tarjan()
{
    delete [] G_lowlink;
    delete [] G_instack;
}

static void scc_tarjan_recurse(gm_graph& G, node_t v, simple_stack& stack, int base_color)
{
    int color = get_new_color();

    G_Color[v] = color;
    G_lowlink[v] = color;

    stack.push(v);
    G_instack[v] = base_color;

    // lookat every neigbhor
    for (edge_t k_idx = G.begin[v];k_idx < G.begin[v+1] ; k_idx ++) 
    {
        node_t w = G.node_idx [k_idx];

        if (G_Color[w] == base_color) {     // first visit
            scc_tarjan_recurse(G, w, stack, base_color);
            if (G_lowlink[v] > G_lowlink[w]) G_lowlink[v] = G_lowlink[w];
        }
        else if (G_instack[w] == base_color)    // already visited and in stack
            if (G_lowlink[v] > G_lowlink[w]) G_lowlink[v] = G_lowlink[w];
    }

    if (G_lowlink[v] == G_Color[v]) {
        node_t w;
        while(true) {
            assert(stack.size() > 0);
            w = stack.top();
            stack.pop();

            G_SCC[w] = v;
            G_instack[w] = -2; // not used anymore
            G_Color[w] = -2;   // SCC found
            if (w == v) break;
        }
    }
}

void do_tarjan_all(gm_graph& G)
{
    // initalize_tarjan has been called
    // G_SCC is inialized as NIL
    // G_Color is inialized as -1
    //int base_color = get_curr_color(); assert(base_color == -1);

    int n_calls = 0;
    for(int32_t i=0;i<G.num_nodes();i++) {
        if (G_Color[i] != -2) {
            n_calls += 1;
            scc_tarjan_recurse(G, i, *the_stack, G_Color[i]);
        }
    }

    printf ("n_calls: %d\n", n_calls);
}

void do_tarjan_parallel_color(gm_graph& G, node_t* G_WCC)
{
    #pragma omp parallel 
    {
        simple_stack_vector stack(1024);

        #pragma omp for nowait
        for (node_t t=0; t < G.num_nodes(); t ++)
        {
            if (G_WCC[t] == t) {
                scc_tarjan_recurse(G, t, stack, G_Color[t]);
                assert(stack.size() == 0);
            }
        }
    }

    {
        simple_stack_vector stack(1024);

        #pragma omp for nowait schedule(dynamic,128)
        for (node_t t=0; t < G.num_nodes(); t ++)
        {
            if (G_Color[t] != -2) {
                scc_tarjan_recurse(G, t, stack, G_Color[t]);
                assert(stack.size() == 0);
            }
        }
    }

}
