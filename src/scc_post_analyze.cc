
#include "scc.h"

static int32_t* G_scc_size = NULL;
static bool scc_size_counted = false;
void initialize_analyze()
{
    G_scc_size = new int32_t[G_num_nodes];

#pragma omp parallel for
    for(int i=0;i<G_num_nodes;i++)
        G_scc_size[i] = 0;
}
void finalize_analyze() 
{
    delete [] G_scc_size;
}

void scc_count_size()
{
#pragma omp parallel for
    for(int i=0;i<G_num_nodes;i++)
    {
        node_t dest = G_SCC[i]; assert(dest >= 0);
        __sync_add_and_fetch(&(G_scc_size[dest]), 1);
    }
    scc_size_counted = true;
}

// Print SCCs to file
void output_scc_list()
{
    FILE *f = fopen("scc_list.txt", "w");
    
    if(f == NULL) 
    {
        printf("Error: Cannot open scc_list output file");
        exit(1);
    }

    for(int i=0;i<G_num_nodes;i++)
    {
        fprintf(f, "%d %d\n", i, G_SCC[i]);
    }
    
}

void create_histogram_and_print()
{
    if (!scc_size_counted) 
        scc_count_size();

    std::map<int, int> Map;

    for(int i=0;i<G_num_nodes;i++)
    {
        if (G_SCC[i] == i) 
        {
            int size = G_scc_size[i];
            if (Map.find(size) == Map.end()) {
                Map[size] = 1;
            }
            else {
                Map[size] ++;
            }
        }
    }
    std::map<int, int>::iterator I;
    for(I=Map.begin(); I!= Map.end(); I++)
    {
        printf("%d => %d\n", I->first, I->second);
    } 
    printf("\n");
}

// N^2
void print_scc_of_size(int size)
{
    if (!scc_size_counted) 
        scc_count_size();

    for(int i=0;i<G_num_nodes;i++)
    {
        if (G_scc_size[i] == size) {
            int cnt =0;
            printf("[%d ", i);
            for(int j=0;j<G_num_nodes;j++) {
                if (j==i) continue;
                if (cnt == size) break;
                if (G_SCC[j] == i) {
                    printf("%d ", j);
                    cnt++;
                    if ((cnt%16)==15) printf("\n");
                }
            }
            printf("]");
        }
    }
}



void print_scc_of_nontrivial_size(int min_sz)
{
    if (!scc_size_counted) 
        scc_count_size();

    for(int i=0;i<G_num_nodes;i++)
    {
        if (G_scc_size[i] >= min_sz) {
            int cnt =0;
            int size = G_scc_size[i];
            printf("[%d ", i);
            for(int j=0;j<G_num_nodes;j++) {
                if (j==i) continue;
                if (cnt == size) break;
                if (G_SCC[j] == i) {
                    printf("%d ", j);
                    cnt++;
                    if ((cnt%32)==31) printf("\n");
                }
            }
            printf("]\n");
        }
    }
}
