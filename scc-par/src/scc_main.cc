#include "gm.h"
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <omp.h>
#include <assert.h>
#include <sys/time.h>

#include "scc.h"
#include "common_main.h"
#include "my_work_queue.h"

//extern void work_q_print_max_depth();
extern void check_WCC();

node_t* G_SCC;
int32_t G_num_nodes;

#define USE_TRIM2   1

class my_main : public main_t
{
    private:
        int method;
        int detail_time;
        int analyze;
        int print;

    public:
        virtual bool check_args(int argc, char** argv)
        {
            method = 0;
            detail_time = 0;
            analyze = 0;
            print = 0;

            if (argc < 1) return false;
            method = atoi(argv[0]);
            if ((method < 0) || (method > 7)) return false;

            if (argc >= 2) {
                if (strncmp(argv[1],"-d",2)==0) 
                    detail_time = 1;
                else if (strncmp(argv[1],"-a",2)==0) 
                    analyze = 1;
                else if (strncmp(argv[1],"-p",2)==0)
                    print = 1;
                else {printf("ignoring %s\n", argv[1]);}
            }

            return true;
        }

        virtual void print_arg_info() 
        {
            printf("<method> {-d|-a|-p}\n");
            printf("   method  0: Trim1 + FW-BW (Baseline) \n");
            printf("   method  1: Trim1 + Global FW-BW + Trim1 + FW-BW (Method 1)\n");
            printf("   method  2: Trim1 + Global FW-BW + Trim1/2 + WCC + FW-BW (Method 2)\n");
            printf("   method  3: Tarjan \n");
            printf("   method  4: Trim1 + Tarjan \n");
            printf("   method  5: UFSCC \n");
            printf("   -d: Print detailed execution time breakdown\n");
            printf("   -a: Print SCC size histogram\n");
            printf("   -p: Output SCC list to file\n");
        }

        virtual bool prepare() 
        {
            gm_rt_initialize();
            G.make_reverse_edges();

            G_num_nodes = G.num_nodes();
            G_SCC= new node_t[G.num_nodes()];

            #pragma omp parallel for
            for(int i =0;i <G_num_nodes; i++)
                G_SCC[i] = gm_graph::NIL_NODE;

            work_q_init(gm_rt_get_num_threads());  // called at the beginning
            initialize_color();
            initialize_trim1();
            initialize_trim2();
            initialize_tarjan();
            initialize_ufscc();
            initialize_analyze();
            initialize_global_fb();
            initialize_WCC();

            return true;
        }

        virtual bool run() 
        {
            if (method == 0) {
                printf("Running Baseline: Trim1 + FW-BW\n");
                do_baseline();
                return true;
            } else if (method == 1) {
                printf("Running Method 1: Trim1 + Global FW-BW + Trim1 + FW-BW\n");
                do_baseline_global_fb();
                return true;
            } else if (method == 2) {
                printf("Running Method 2: Trim1 + Global FW-BW + Trim1/2 + WCC + FW-BW\n");
                do_baseline_global_wcc_fb();
                return true;
            } else if (method == 3) {
                printf("Running Tarjan\n");
                do_tarjan_all(G);
                return true;
            } else if (method == 4) {
                printf("Running Trim1 + Tarjan\n");
                do_baseline_global_trim_tarjan();
                return true;
            } else if (method == 5) {
                printf("Running UFSCC\n");
                do_baseline_global_ufscc();
                return true;
            } else {
                return false;
            }
        }

        virtual bool post_process() {
            int count = 0;
            for(int i=0;i<G.num_nodes(); i++) {
                if (G_SCC[i] == i) count ++;
            }
            printf("Total # SCCs = %d\n", count);

            //print_scc_of_nontrivial_size(2);
            //if (analyze) work_q_print_max_depth();

            if (analyze) create_histogram_and_print();

            if (print) output_scc_list();
            
            finalize_color();
            finalize_tarjan();
            finalize_ufscc();
            finalize_trim2();
            finalize_WCC();
            finalize_analyze();
            return true;
        }

#define PHASE_BEGIN(X) {if (detail_time) {gettimeofday(&V1, NULL);}}
#define PHASE_END(X)   {if (detail_time) {gettimeofday(&V2, NULL); \
    printf("\t[%s phase: %f ms]\n", X,  \
            (V2.tv_sec - V1.tv_sec)*1000.0 + (V2.tv_usec - V1.tv_usec)*0.001);}}
#define EMPTY_PHASE(X) {if (detail_time) printf("\t[%s phase: %f ms]\n", X, 0.0);}

    private:
        struct timeval V1, V2;

        // Baseline: Trim1 + FW-BW
        void do_baseline()
        {
            // trim repeatedly
            PHASE_BEGIN("Trim");

            int trimmed = repeat_global_trim1(G);
            int curr_count = G_num_nodes - trimmed;
            PHASE_END("Trim");
            if (analyze) printf("trimmed = %d\n", trimmed);

            EMPTY_PHASE("Global");
            EMPTY_PHASE("Trim");
            EMPTY_PHASE("WCC");
            if (curr_count == 0) {
                EMPTY_PHASE("FB");
                return;
            }

            my_work* work = new my_work();
            work->color = get_curr_color(); // -1
            assert(work->color == -1);
            work->color_set = NULL;
            work->count = G_num_nodes - trimmed;
            work->depth = 0;
            work_q_put(0, work);

            PHASE_BEGIN("FB");
            start_workers_fw_bw(G,1);
            PHASE_END("FB");
        }

        // Method 1
        void do_baseline_global_fb()
        {
            // trim repeatedly
            PHASE_BEGIN("TRIM1");
            int trimmed = repeat_global_trim1(G);
            PHASE_END("TRIM1");


            int curr_color = get_curr_color(); // -1
            assert(curr_color == -1);
            int curr_count = G_num_nodes - trimmed;
            if (analyze) printf("trimmed = %d\n", trimmed);
            if (curr_count == 0) {
                EMPTY_PHASE("GLOBAL_BFS");
                EMPTY_PHASE("TRIM1");
                EMPTY_PHASE("WCC");
                EMPTY_PHASE("FB");
                return;
            }

            // global BFS
            PHASE_BEGIN("GLOBAL_BFS");

            int scc_size = do_fw_bw_global_main(G, curr_color, curr_count, false);
            //int scc_size = do_fw_bw_global_main(G, curr_color, curr_count, true);
            PHASE_END("GLOBAL_BFS");

            if (analyze) printf("First SCC size = %d\n", scc_size);

            PHASE_BEGIN("TRIM1");
            trimmed = repeat_global_trim1_compact(G);
            PHASE_END("TRIM1");
            curr_count = G_num_nodes - trimmed;
            if (analyze) printf("trimmed = %d\n", trimmed);
            if (curr_count == 0) {
                EMPTY_PHASE("WCC");
                EMPTY_PHASE("FB");
                return;
            }
            
            EMPTY_PHASE("WCC");
            PHASE_BEGIN("FB");
            create_works_after_bfs_trim(G);
            //start_workers_fw_bw(G, 1);
            start_workers_fw_bw_dfs(G, 1);

            PHASE_END("FB");
        }

        // Method 2
        void do_baseline_global_wcc_fb()
        {
            // trim repeatedly
            PHASE_BEGIN("TRIM1");
            int trimmed = repeat_global_trim1(G);
            PHASE_END("TRIM1");

            // global BFS
            int curr_color = get_curr_color(); // -1
            assert(curr_color == -1);
            int curr_count = G_num_nodes - trimmed;
            if (analyze) printf("trimmed = %d\n", trimmed);
            if (curr_count == 0) {
                EMPTY_PHASE("GLOBAL_BFS");
                EMPTY_PHASE("TRIM1");
                EMPTY_PHASE("WCC");
                EMPTY_PHASE("FB");
                if (analyze) printf("First_SCC_size = 0\n");
                if (analyze) printf("trimmed = 0\n", trimmed);
                if (analyze) printf("SCC_size_from_WCC = %d\n", 0); // WCC never identifies a SCC
                return;
            }

            PHASE_BEGIN("GLOBAL_BFS");
            int scc_size = do_fw_bw_global_main(G, curr_color, curr_count, false);
            PHASE_END("GLOBAL_BFS");
            if (analyze) printf("First_SCC_size = %d\n", scc_size);

            PHASE_BEGIN("TRIM1");
            trimmed = repeat_global_trim1_compact(G);
#if USE_TRIM2
            {
                int trim_total = 0;
                trim_total = 0;
                int cnt = do_global_trim2_new(G); 
                //int cnt = do_global_trim2(G); 
                //int cnt = repeat_global_trim2_new(G, 100); 
                trim_total += cnt;
                trim_total += repeat_global_trim1_compact(G, 100);
                trimmed += trim_total;
            }
#endif
            PHASE_END("TRIM1");
            curr_count = curr_count- trimmed - scc_size;
            if (analyze) printf("trimmed = %d\n", trimmed);
            if (analyze) printf("SCC_size_from_WCC = %d\n", 0); // WCC never identifies a SCC
            if (curr_count == 0) {
                EMPTY_PHASE("WCC");
                EMPTY_PHASE("FB");
                return;
            }

            PHASE_BEGIN("WCC");
            do_global_wcc(G);
            create_work_items_from_wcc(G);
            if (analyze) {printf("#queue_size = %d\n",work_q_size());}
            PHASE_END("WCC");

            PHASE_BEGIN("FB");
            //start_workers_fw_bw(G, 64);
            start_workers_fw_bw_dfs(G, 40);
            PHASE_END("FB");
        }

        // Trim1 + Tarjan 
        void do_baseline_global_trim_tarjan()
        {

            // trim repeatedly
            PHASE_BEGIN("TRIM1");
            int trimmed = repeat_global_trim1(G);
            PHASE_END("TRIM1");
            if (detail_time) printf("Trimmed = %d\n", trimmed);

            do_tarjan_all(G);

        }

        // UFSCC
        void do_baseline_global_ufscc()
        {
            do_ufscc_all(G);
        }
};

int main(int argc, char** argv)
{
    my_main T;
    T.main(argc, argv);
}
