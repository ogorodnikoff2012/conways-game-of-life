#include <interface.h>
#include <omp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

const char* get_version() {
    return "0.1_openmp";
}

struct workers_internal {
    field_t* field;
    field_t* next_field;
    int required_gen;
    int current_gen;
    bool stop_requested;

    omp_lock_t cur_gen_lock;
//    omp_lock_t req_gen_lock;
};

static field_t second_field;

const char* setup_workers(field_t* field, workers_t* workers) {
    workers->impl = calloc(1, sizeof(struct workers_internal));

    init_field(&second_field, field->width, field->height);
    workers->impl->field = field;
    workers->impl->next_field = &second_field;

    workers->impl->required_gen = 0;
    workers->impl->current_gen = 0;
    workers->impl->stop_requested = false;

    omp_init_lock(&workers->impl->cur_gen_lock);
//    omp_init_lock(&workers->impl->req_gen_lock);

    return NULL;
}

void destroy_workers(workers_t* workers) {
    destroy_field(&second_field);
    omp_destroy_lock(&workers->impl->cur_gen_lock);
//    omp_destroy_lock(&workers->impl->req_gen_lock);
    free(workers->impl);
}

void dump_field(field_t* field, workers_t* workers) {
    omp_set_lock(&workers->impl->cur_gen_lock);

    printf("# Current iteration: %d\n", workers->impl->current_gen);
    for (int y = 0; y < field->height; ++y) {
        printf("# ");
        for (int x = 0; x < field->width; ++x) {
            char ch = '_';
            if (*get_cell(workers->impl->field, x, y)) {
                ch = 'O';
            }
            printf("%c", ch);
        }
        printf("\n");
    }

    omp_unset_lock(&workers->impl->cur_gen_lock);
}

void run(field_t* field, workers_t* workers) {
    int n = 0;
    if (scanf("%d", &n) != 1 || n <= 0) {
        printf("# A positive integer expected!\n");
        return;
    }

    #pragma omp critical
    workers->impl->required_gen += n;
}

void stop(field_t* field, workers_t* workers) {
    omp_set_lock(&workers->impl->cur_gen_lock);
    #pragma omp critical
    workers->impl->required_gen = workers->impl->current_gen;
    omp_unset_lock(&workers->impl->cur_gen_lock);
}

void run_controller_loop(field_t* field, workers_t* workers) {
    const int width = field->width;
    const int height = field->height;

    int x, y;

    while (!workers->impl->stop_requested) {
        while (workers->impl->required_gen > workers->impl->current_gen) {

#pragma omp parallel default(shared) private(x, y)
            for (x = 0; x < width; ++x) {
                for (y = 0; y < height; ++y) {
                    int alive_neighbors = 0;
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            if (dx == 0 && dy == 0) {
                                continue;
                            }
                            int nx = (x + dx + width) % width;
                            int ny = (y + dy + height) % height;

                            if (*get_cell(workers->impl->field, nx, ny)) {
                                ++alive_neighbors;
                            }
                        }
                    }

                    *get_cell(workers->impl->next_field, x, y) = (alive_neighbors == 3) ||
                        (alive_neighbors == 2 && *get_cell(workers->impl->field, x, y));
                }
            }

            omp_set_lock(&workers->impl->cur_gen_lock);
            ++workers->impl->current_gen;
            field_t* temp = workers->impl->field;
            workers->impl->field = workers->impl->next_field;
            workers->impl->next_field = temp;
            omp_unset_lock(&workers->impl->cur_gen_lock);
        }
        usleep(10000);
    }
}

void stop_emulation(workers_t* workers) {
    #pragma omp critical
    workers->impl->stop_requested = true;
}
