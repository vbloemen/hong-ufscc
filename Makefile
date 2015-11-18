all: lib bin

lib:  
	@cd gm_graph; make

bin:
	@cd src; make

clean:
	@cd src; make clean

clean_all:
	@cd gm_graph; make clean
	@cd src; make clean

help:
	@echo "make <what>"
	@echo "  what: what to build"
	@echo "    lib: build gm_graph runtime library (once)"
	@echo "    bin: build executable scc binary"
	@echo "    all: lib and bin"
	@echo "    clean: clean generated target code"
	@echo "    clean_all: clean generated target code and runtime library"
