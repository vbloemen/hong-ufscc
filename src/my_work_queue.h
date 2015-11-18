#ifndef MY_WORK_QUEUE_H
#define MY_WORK_QUEUE_H

#include "gm.h"
#include <set>
#include<vector>
#include <unordered_set>

typedef std::unordered_set<node_t> NODE_SET;

class my_work {
public:
    int color;                          // color of the base-set
    int count;                          // count of the set
    NODE_SET* color_set;        // the base-set
    int depth;                          // what is this for?
};

bool     is_work_q_empty_from_seq_context(); 
my_work* work_q_fetch(int thread_id);
void     work_q_fetch_N(int thread_id, int N, std::vector<my_work*>& works);
void     work_q_put(int thread_id, my_work* work);
void     work_q_put_all(int thread_id, std::vector<my_work*>& work);
void     work_q_init(int num_threads);  // called at the beginning
void     work_q_print_max_depth();
int      work_q_size();

#endif
