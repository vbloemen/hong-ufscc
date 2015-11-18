#ifndef SCC_H
#define SCC_H

#include "my_work_queue.h"
#include "gm.h"
#include <assert.h>
#include <unordered_set>



extern int32_t* G_Color;
extern node_t* G_SCC;
extern int32_t  G_num_nodes;
#define MAX_THREADS     1024

//------------------------
// Color
//------------------------
void initialize_color();  // all the colors are initlized as -1, becomes -2 when SCC found. positive numbers in between
void finalize_color();
int get_new_color();      // increase color and give the new one (thread-safe)
int get_curr_color();     // return current color without increasing

node_t choose_pivot_from_color(gm_graph& G, int color);
NODE_SET* generate_compact_set(gm_graph& G, int color);

//------------------------
// Tarjan
//------------------------
void initialize_tarjan();
void finalize_tarjan(); 
void do_tarjan_all(gm_graph& G);
void do_tarjan(gm_graph& G, int root_color, node_t root);
void do_tarjan_parallel_color(gm_graph& G, node_t* G_WCC);

//------------------------
// UFSCC
//------------------------
void initialize_ufscc();
void finalize_ufscc();
void do_ufscc_all(gm_graph& G);
void do_ufscc(gm_graph& G, int root_color, node_t root);

//-----------------------------
// TRIM1 
//-----------------------------
void initialize_trim1();
int do_global_trim1(gm_graph& G); // returns number of trimmed  nodes
int do_global_trim1_compact(gm_graph& G); // returns number of trimmed  nodes
int repeat_global_trim1(gm_graph& G, int TRIM_STOP=100);
int repeat_global_trim1_compact(gm_graph& G, int TRIM_STOP=100);
std::vector<node_t>& get_compact_trim_targets();

int repeat_local_trim1(gm_graph& G,my_work* w);

//-----------------------------
// TRIM2
//-----------------------------
void initialize_trim2();
void finalize_trim2();
int do_global_trim2(gm_graph& G);
int repeat_global_trim2(gm_graph& G, int exit_count);

int do_global_trim2_new(gm_graph& G);
int repeat_global_trim2_new(gm_graph& G, int exit_count);

//------------------------------
// WCC
//------------------------------
void initialize_WCC();
void finalize_WCC();
void do_global_wcc(gm_graph& G);
void create_work_items_from_wcc(gm_graph& G);
node_t* get_WCC();

//-------------------------------
// FW-BW
//-------------------------------
int do_fw_bw_single_thread(gm_graph& G, my_work* work, std::vector<my_work*>& new_works);
void start_workers_fw_bw(gm_graph& G, int local_items);
void start_workers_fw_bw_dfs(gm_graph& G, int local_items);

void initialize_global_fb();
int do_fw_bw_global_main(gm_graph& G, int curr_color, int count, bool create_work_items);
void create_works_after_bfs_trim(gm_graph& G);

//-----------------------------
// Post Analysis
//-----------------------------
void initialize_analyze();
void finalize_analyze();
void print_scc_of_size(int size); // may take long time
void print_scc_of_nontrivial_size(int min_size); // may take long time
void create_histogram_and_print();
void output_scc_list();

#endif
