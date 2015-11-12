
#include "gm.h"
#include "scc.h"
#include "my_work_queue.h"
#include <list>
#include <sys/resource.h>
#include <assert.h>

#define HAVE_PROFILER 0

#if HAVE_PROFILER
//#include <gperftools/profiler.h>
#include <gperftools/profiler.h>
#endif


class uf_stack_array {
    public:
        uf_stack_array(int max_sz) {
            stack = new int32_t[max_sz];
            stack_ptr = 0;
        }
        virtual ~uf_stack_array() { delete [] stack;}


    inline void push(node_t n) {
        stack[stack_ptr] = n;
        stack_ptr ++;
    }
    inline int size()   {return stack_ptr;}
    inline node_t top() {return stack[stack_ptr-1];}
    inline void pop()   {stack_ptr--;}
    inline bool empty() {return (stack_ptr == 0);}
    int32_t* get_ptr()  {return stack;}
private:
    int32_t* stack;
    int32_t stack_ptr;
};


typedef enum uf_status_e {
    UF_LIVE           = 0,                    // LIVE state
    UF_LOCK           = 1,                    // prevent other workers from
                                              //   updating the parent
    UF_DEAD           = 2,                    // completed SCC
} uf_status;


typedef enum list_status_e {
    LIST_LIVE         = 0,                    // LIVE (initial value)
    LIST_LOCK         = 1,                    // busy merging lists
    LIST_TOMB         = 2,                    // fully explored state
} list_status;


typedef enum pick_result {
    PICK_DEAD         = 1,
    PICK_SUCCESS      = 2,
    PICK_MARK_DEAD    = 3,
} pick_e;


typedef uint64_t worker_set;
#define CLAIM_DEAD      1
#define CLAIM_FIRST     2
#define CLAIM_SUCCESS   3
#define CLAIM_FOUND     4

#define atomic_read(v)      (*(volatile typeof(*v) *)(v))
#define atomic_write(v,a)   (*(volatile typeof(*v) *)(v) = (a))
#define fetch_or(a, b)      __sync_fetch_and_or(a,b)
#define cas(a, b, c)        __sync_bool_compare_and_swap(a,b,c)


struct uf_node {
    worker_set          workers;
    node_t              parent;
    node_t              list_next;
    unsigned char       uf_status;
    unsigned char       list_status;
};


struct stats_counter {
    int n_claim_dead;
    int n_claim_found;
    int n_self_loop;
    int n_claim_success;
    int n_states;
    int n_trans;
    int n_unique_states;
    int n_unique_trans;
    int n_initial_states;
};


class unionfind {
public:

    unionfind (int max_sz) {
        uf_array = new uf_node[max_sz+1](); // initialize with all zeroes
    }
    ~unionfind () { delete [] uf_array;}

    /* **************************** list operations **************************** */


    bool is_in_list (node_t state)
    {
        return (atomic_read (&uf_array[state].list_status) != LIST_TOMB);
    }


    /**
     * searches the first LIVE state in the cyclic list, starting from state:
     * if all elements in the list are TOMB, we mark the SCC DEAD
     * if three consecutive items a -> b -> c with a  == TOMB and b == TOMB :
     *   we try to update a -> c (and thereby reducing the size of the cyclic list)
     */
    pick_e pick_from_list (node_t state, node_t *ret)
    {
        // invariant: every consecutive non-LOCK state is in the same set
        node_t              a, b, c;
        unsigned char       a_status, b_status;

        a = state;

        while ( 1 ) {

            // if we exit this loop, a.status == TOMB or we returned a LIVE state
            while ( 1 ) {
                a_status = atomic_read (&uf_array[a].list_status);

                // return directly if a is LIVE
                if (a_status == LIST_LIVE) {
                    *ret = a;
                    return PICK_SUCCESS;
                }

                // otherwise wait until a is TOMB (it might be LOCK now)
                else if (a_status == LIST_TOMB)
                    break;
            }

            // find next state: a --> b
            b = atomic_read (&uf_array[a].list_next);

            // if a is TOMB and only element, then the SCC is DEAD
            if (a == b || b == 0) {
                if ( mark_dead (a) )
                    return PICK_MARK_DEAD;
                return PICK_DEAD;
            }

            // if we exit this loop, b.status == TOMB or we returned a LIVE state
            while ( 1 ) {
                b_status = atomic_read (&uf_array[b].list_status);

                // return directly if b is LIVE
                if (b_status == LIST_LIVE) {
                    *ret = b;
                    return PICK_SUCCESS;
                }

                // otherwise wait until b is TOMB (it might be LOCK now)
                else if (b_status == LIST_TOMB)
                    break;
            }

            // a --> b --> c
            c = atomic_read (&uf_array[b].list_next);

            // make the list shorter (a --> c)
            if (atomic_read (&uf_array[a].list_next) == b) {
                atomic_write (&uf_array[a].list_next, c);
            }

            a = c; // continue searching from c
        }
    }


    bool remove_from_list (node_t state)
    {
        unsigned char         list_s;

        // only remove list item if it is LIVE , otherwise (LIST_LOCK) wait
        while ( true ) {
            list_s = atomic_read (&uf_array[state].list_status);
            if (list_s == LIST_LIVE) {
                if (cas (&uf_array[state].list_status, LIST_LIVE, LIST_TOMB) ){
                    G_Color[state-1] = -2; // globally visited (prevent other workers from exploring)
                    return 1;
                }
            } else if (list_s == LIST_TOMB)
                return 0;
        }
    }


    /* ********************* 'basic' union find operations ********************* */

    /**
     * returns:
     * - CLAIM_FIRST   : if we initialized the state
     * - CLAIM_FOUND   : if the state is LIVE and we have visited its SCC before
     * - CLAIM_SUCCESS : if the state is LIVE and we have not yet visited its SCC
     * - CLAIM_DEAD    : if the state is part of a completed SCC
     */
    char make_claim (node_t state, int worker)
    {
        worker_set          w_id      = 1ULL << worker;
        worker_set          orig_workers;
        node_t              f         = find (state);

        // is the state dead?
        if (atomic_read (&uf_array[f].uf_status) == UF_DEAD)
            return CLAIM_DEAD;

        // did we previously explore a state in this SCC?
        if ( (atomic_read (&uf_array[f].workers) & w_id ) != 0) {
            return CLAIM_FOUND;
            // NB: cycle is possibly missed (in case f got updated)
            // - however, next iteration should detect this
        }

        // Add our worker ID to the set, and ensure it is the UF representative
        orig_workers = fetch_or (&uf_array[f].workers, w_id);
        while ( atomic_read (&uf_array[f].parent) != 0 ) {
            f = find (f);
            fetch_or (&uf_array[f].workers, w_id);
        }
        if (orig_workers == 0ULL)
            return CLAIM_FIRST;
        else
            return CLAIM_SUCCESS;
    }


    /**
     * returns the representative for the UF set
     */
    node_t find (node_t state)
    {
        // recursively find and update the parent (path compression)
        node_t       parent = atomic_read(&uf_array[state].parent);
        if (parent == 0)
            return state;

        node_t root = find (parent);
        if (root != parent)
            atomic_write(&uf_array[state].parent, root);
        return root;
    }


    /**
     * returns whether or not a and b reside in the same UF set
     */
    bool sameset (node_t a, node_t b)
    {
        if (a == b)
             return 1;

        // we assume that a == find(a)
        node_t   b_r = find (b);

        // return true if the representatives are equal
        if (a == b_r)
            return 1;

        if (b_r < a) {
            if (atomic_read(&uf_array[b_r].parent) == 0)
                return 0;
        }

        if (atomic_read(&uf_array[a].parent) == 0)
            return 0;

        // otherwise retry
        return sameset (find(a), b_r);
    }


    /**
     * unites two sets and ensures that their cyclic lists are combined to one
     */
    void unite (node_t a, node_t b)
    {
        node_t               a_r, b_r, a_l, b_l, a_n, b_n, r, q;
        worker_set           q_w, r_w;

        while ( 1 ) {

            // find the representatives
            a_r = find (a);
            b_r = find (b);

            if (a_r == b_r) {
                return;
            }

            // decide on the new root (deterministically)
            // take the highest index as root
            r = a_r;
            q = b_r;
            if (a_r < b_r) {
                r = b_r;
                q = a_r;
            }

            // lock the non-root
            if ( !lock_uf (q) )
                continue;

            break;
        }
        // lock the list entries
        if ( !lock_list (a, &a_l) ) {
            return;
        }
        if ( !lock_list (b, &b_l) ) {
            unlock_list (a_l);
            return;
        }

        // swap the list entries
        a_n = atomic_read (&uf_array[a_l].list_next);
        b_n = atomic_read (&uf_array[b_l].list_next);

        // in case singleton sets
        if (a_n == 0)
            a_n = a_l;
        if (b_n == 0)
            b_n = b_l;

        atomic_write (&uf_array[a_l].list_next, b_n);
        atomic_write (&uf_array[b_l].list_next, a_n);

        // update parent
        atomic_write (&uf_array[q].parent, r);

        // only update worker set for r if q adds workers
        q_w = atomic_read (&uf_array[q].workers);
        r_w = atomic_read (&uf_array[r].workers);
        if ( (q_w | r_w) != r_w) {
            // update!
            fetch_or (&uf_array[r].workers, q_w);
            while (atomic_read (&uf_array[r].parent) != 0) {
                r = find (r);
                fetch_or (&uf_array[r].workers, q_w);
            }
        }

        // unlock
        unlock_list (a_l);
        unlock_list (b_l);
        unlock_uf (q);

        return;
    }


    /* ******************************* dead SCC ******************************** */


    /**
     * (return == 1) ==> ensures DEAD (we cannot ensure a non-DEAD state)
     */
    bool is_dead (node_t state)
    {
        node_t               f = find (state);
        return ( atomic_read (&uf_array[f].uf_status) == UF_DEAD );
    }


    /**
     * set the UF status for the representative of state to DEAD
     */
    bool mark_dead (node_t state)
    {
        bool                result = false;
        node_t              f      = find (state);
        unsigned char       status = atomic_read (&uf_array[f].uf_status);

        while ( status != UF_DEAD ) {
            if (status == UF_LIVE)
                result = cas (&uf_array[f].uf_status, UF_LIVE, UF_DEAD);
            status = atomic_read (&uf_array[f].uf_status);
        }

        return result;
    }


    /* ******************************** locking ******************************** */


    bool lock_uf (node_t a)
    {
        if (atomic_read (&uf_array[a].uf_status) == UF_LIVE) {
           if (cas (&uf_array[a].uf_status, UF_LIVE, UF_LOCK)) {

               // successfully locked
               // ensure that we actually locked the representative
               if (atomic_read (&uf_array[a].parent) == 0)
                   return 1;

               // otherwise unlock and try again
               atomic_write (&uf_array[a].uf_status, UF_LIVE);
           }
        }
        return 0;
    }


    void unlock_uf (node_t a)
    {
        atomic_write (&uf_array[a].uf_status, UF_LIVE);
    }


    bool lock_list (node_t a, node_t *a_l)
    {
        char pick;

        while ( 1 ) {
            pick = pick_from_list (a, a_l);
            if ( pick != PICK_SUCCESS )
                return 0;
            if (cas (&uf_array[*a_l].list_status, LIST_LIVE, LIST_LOCK) )
                return 1;
        }
    }


    void unlock_list (node_t a_l)
    {
        atomic_write (&uf_array[a_l].list_status, LIST_LIVE);
    }


    string print_list_status (unsigned char ls)
    {
        if (ls == LIST_LIVE) return "LIVE";
        if (ls == LIST_LOCK) return "LOCK";
        if (ls == LIST_TOMB) return "TOMB";
        else                 return "????";
    }


    string print_uf_status (unsigned char us)
    {
        if (us == UF_LIVE)   return "LIVE";
        if (us == UF_LOCK)   return "LOCK";
        if (us == UF_DEAD)   return "DEAD";
        else                 return "????";
    }


    void debug_aux (node_t state, int depth)
    {
        if (depth == 0) {
            printf ("\x1B[45mParent structure:\x1B[0m\n");
            printf ("\x1B[45m%5s %10s %10s %7s %7s %10s\x1B[0m\n",
                     "depth",
                     "state",
                     "parent",
                     "uf_s",
                     "list_s",
                     "next");
        }

        printf ("\x1B[44m%5d %10d %10d %7s %7s %10d\x1B[0m\n",
            depth,
            state-1,
            atomic_read (&uf_array[state].parent)-1,
            print_uf_status (atomic_read (&uf_array[state].uf_status)).c_str() ,
            print_list_status (atomic_read (&uf_array[state].list_status)).c_str() ,
            atomic_read (&uf_array[state].list_next) -1 );

        if (uf_array[state].parent != 0) {
            debug_aux (uf_array[state].parent, depth+1);
        }
    }


    node_t debug (node_t state)
    {
        debug_aux (state, 0);
        return state;
    }

private:
    uf_node *uf_array = NULL;
};


unionfind     *uf;
stats_counter *global_c;
int used_ufscc = 0;


void initialize_ufscc ()
{
    // globally initialize
    uf       = new unionfind (G_num_nodes);
    global_c = new stats_counter ();
}


void finalize_ufscc ()
{
    if (used_ufscc) {
        printf("\n");
        printf("initial states count:     %10d\n", global_c->n_initial_states);
        printf("unique states count:      %10d\n", global_c->n_unique_states);
        printf("total states count:       %10d\n", global_c->n_states);
        printf("unique transitions count: %10d\n", global_c->n_unique_trans);
        printf("total transitions count:  %10d\n", global_c->n_trans);
        printf("- self-loop count:        %10d\n", global_c->n_self_loop);
        printf("- claim dead count:       %10d\n", global_c->n_claim_dead);
        printf("- claim found count:      %10d\n", global_c->n_claim_found);
        printf("- claim success count:    %10d\n", global_c->n_claim_success);
    }
    delete uf;
}


/**
 * This code gets executed for each worker, and for each unvisited state
 */
static void scc_ufscc_recurse (gm_graph &G,
                               node_t state,
                               uf_stack_array &roots,
                               uf_stack_array &recursion,
                               stats_counter *cnt,
                               int thread_id)
{
    pick_e              pick;
    node_t              v, v_p, w, root;
    char                claim;
    edge_t              k_idx, t, gbegin;

    v = state;


start_recursion:
    roots.push (v);

    while ( 1 ) {

        // if previous state is in the same set, do not explore here, but backtrack
        if (!recursion.empty() && uf->sameset(recursion.top()+1, v+1) ) {
            goto backtrack;
        }

        pick = uf->pick_from_list(v+1, &v_p);
        if (pick != PICK_SUCCESS) {
            break; // no more LIVE states in the SCC ==> completed SCC
        }
        v_p --;

        cnt->n_states += 1;

        // v <--> v_p
        for (k_idx = G.begin[v_p]; k_idx < G.begin[v_p+1] ; k_idx ++)
        {

            // shift permutation per thread
            gbegin = G.begin[v_p];
            t = gbegin + ( (k_idx - gbegin + thread_id) % (G.begin[v_p+1] - gbegin) );
            w = G.node_idx[t]; // v --> v_p --> w (successor)

            cnt->n_trans += 1;
            if (w == v_p) {
                cnt->n_self_loop += 1;
                continue; // self-loop
            }

            claim = uf->make_claim (w+1, thread_id);

            // (w is in a DEAD SCC) ==> disregard
            if (claim == CLAIM_DEAD){
                cnt->n_claim_dead += 1;
                continue;
            }

            // (w is a 'new' successor) ==> recursively explore
            else if (claim == CLAIM_FIRST || claim == CLAIM_SUCCESS) {
                cnt->n_claim_success += 1;

                if (claim == CLAIM_FIRST) {
                    cnt->n_unique_states += 1;
                    cnt->n_unique_trans +=  (G.begin[w+1] - G.begin[w]);
                }

                recursion.push(v_p);
                recursion.push(k_idx);
                recursion.push(v);
                v = w;
                goto start_recursion;
                // segfaults with normal recursion (probably too deep?)
                //scc_ufscc_recurse(G, w, roots, thread_id);

end_recursion:
                v       = recursion.top(); recursion.pop();
                k_idx   = recursion.top(); recursion.pop();
                v_p     = recursion.top(); recursion.pop();

                // break early if sameset(v,w)
                if (uf->is_dead(v+1))
                    break;

            }

            // (CLAIM_FOUND: we have previously visited some w' <--> w)
            // ==> unite roots stack until w' is on top
            else {
                cnt->n_claim_found += 1;

                // perhaps it helps that w is find(w)
                while ( !uf->sameset(w+1, v+1) ) {

                    root = roots.top(); roots.pop();

                    uf->unite(roots.top()+1, root+1);
                }
            }
        }

        uf->remove_from_list(v_p+1);
    }

backtrack:
    if (roots.top() == v) {
        roots.pop();
    }

    if (!recursion.empty()) {
        goto end_recursion;
    }

}


void do_ufscc_all (gm_graph& G)
{
#if HAVE_PROFILER
    printf ("Start profiling\n");
    ProfilerStart ("ufscc.perf");
#endif

    used_ufscc=1;
    // run the algorithm in parallel
    #pragma omp parallel
    {
        int              thread_id       = gm_rt_thread_id();
        uf_stack_array  *roots_stack     = new uf_stack_array(G_num_nodes);
        uf_stack_array  *recursion_stack = new uf_stack_array(3*G_num_nodes);
        stats_counter   *local_c         = new stats_counter();

        #pragma omp for
        for (int32_t i=0; i<G.num_nodes(); i++) {
            if (G_Color[i] != -2) {
                if (uf->make_claim(i+1, thread_id) == CLAIM_FIRST) {
                    local_c->n_unique_states += 1;
                    local_c->n_unique_trans += (G.begin[i+1] -  G.begin[i]);
                }
                local_c->n_initial_states += 1;
                scc_ufscc_recurse (G, i, *roots_stack, *recursion_stack, local_c, thread_id);
            }
        }

        #pragma omp critical
        {
            global_c->n_unique_states   += local_c->n_unique_states;
            global_c->n_unique_trans    += local_c->n_unique_trans;
            global_c->n_initial_states  += local_c->n_initial_states;
            global_c->n_trans           += local_c->n_trans;
            global_c->n_states          += local_c->n_states;
            global_c->n_self_loop       += local_c->n_self_loop;
            global_c->n_claim_dead      += local_c->n_claim_dead;
            global_c->n_claim_found     += local_c->n_claim_found;
            global_c->n_claim_success   += local_c->n_claim_success;
        }

        delete roots_stack;
        delete recursion_stack;
        delete local_c;
    }


#if HAVE_PROFILER
    printf ("Done profiling\n");
    ProfilerStop();
#endif


    int *scc_sizes = new int[G_num_nodes]();

    // count the number of SCCs
    {
        for (int32_t i=0; i<G.num_nodes(); i++) {
            G_SCC[i] = uf->find(i+1) - 1;
            scc_sizes[G_SCC[i]] ++;
        }
    }

    int max_SCC = 0;
    for (int32_t i=0; i<G.num_nodes(); i++) {
        if (scc_sizes[i] > max_SCC) {
            max_SCC = scc_sizes[i];
        }
    }
    printf("\n\nMAX SCC SIZE:     %10d\n\n", max_SCC);


}
