CFLAGS=-std=c11 -O0 -ggdb3 -Iinclude
SRC_COMMON=src/main.c src/common.c

.PHONY: clean

all: bin/game_pthread bin/game_openmp bin/game_mpi

bin/game_pthread: $(SRC_COMMON) src/back_end/pthread.c
	gcc $(CFLAGS) -pthread -DBACKEND=PTHREAD $(SRC_COMMON) src/back_end/pthread.c -o $@

bin/game_openmp: $(SRC_COMMON) src/back_end/openmp.c
	gcc $(CFLAGS) -DBACKEND=OPENMP -D_DEFAULT_SOURCE -fopenmp $(SRC_COMMON) src/back_end/openmp.c -lrt -o $@

bin/game_mpi: $(SRC_COMMON) src/back_end/mpi.c
	mpicc $(CFLAGS) -DBACKEND=MPI -D_DEFAULT_SOURCE $(SRC_COMMON) src/back_end/mpi.c -o $@

clean:
	rm -f bin/game_{pthread,openmp,mpi}
