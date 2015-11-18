
CC= g++
NAME = ../scc
OBJS = scc_main.o my_work_queue.o scc_tarjan.o  scc_ufscc.o scc_color.o scc_trim1.o scc_fb_seq.o scc_post_analyze.o scc_trim2.o scc_fb_global.o scc_weak.o scc_fb_seq2.o scc_trim2_new.o
INCDIR = ../gm_graph/inc
LIBDIR = ../gm_graph/lib
OUTDIR = ./
LIB = gmgraph
LFLAGS = -L$(LIBDIR) -l$(LIB)
CFLAGS = -O3 -g -I. -I$(INCDIR) -fopenmp  -std=gnu++0x
INCFILES = $(shell ls $(INCDIR))
H_FILES = $(addprefix $(INCDIR)/, $(INCFILES))

all : $(NAME)

$(NAME) : $(OBJS) ../gm_graph/lib/libgmgraph.a
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

%.o:%.cc
	$(CC) $(CFLAGS) -c $^ -o $@

clean :
	\rm $(NAME) *.o
