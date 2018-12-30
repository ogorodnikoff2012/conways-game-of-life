CFLAGS=-std=c11 -O0 -ggdb3 -Iinclude
SRC_COMMON=src/main.c src/common.c

.PHONY: clean

all: bin/game_pthread bin/game_openmp

bin/game_pthread: $(SRC_COMMON) src/back_end/pthread.c
	gcc $(CFLAGS) -pthread -DBACKEND=PTHREAD $(SRC_COMMON) src/back_end/pthread.c -o $@

bin/game_openmp: $(SRC_COMMON) src/back_end/openmp.c
	gcc $(CFLAGS) -DBACKEND=OPENMP -D_DEFAULT_SOURCE -fopenmp $(SRC_COMMON) src/back_end/openmp.c -lrt -o $@

clean:
	rm -f bin/game_{pthread,openmp}
