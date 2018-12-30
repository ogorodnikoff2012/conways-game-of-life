#include <interface.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>

#define kSlaveThreadsCount 4

typedef struct {
    pthread_t thread_descr;
    int min_x, max_x;
    int local_gen;
    struct workers_internal* shared;
    pthread_mutex_t mtx_local_gen;
    pthread_cond_t cv_local_gen;
} slave_thread_t;

struct workers_internal {
    pthread_t master_thread;
    slave_thread_t slave_threads[kSlaveThreadsCount];
    atomic_int required_gen;
    atomic_int current_gen;
    field_t* field;
    field_t* next_field;
    atomic_bool stop_required;

    pthread_cond_t  cv_req_gen,  cv_cur_gen;
    pthread_mutex_t mtx_req_gen, mtx_cur_gen;
};

const int DX[] = {-1, -1, -1,  0, 0,  1, 1, 1};
const int DY[] = {-1,  0,  1, -1, 1, -1, 0, 1};
const int kNeighborsCount = 8;

static inline int min(int x, int y) {
    return x < y ? x : y;
}

void* slave_thread(void* arg) {
    slave_thread_t* data = arg;

    bool stop_required = false;

    while (true) {
        pthread_mutex_lock(&data->shared->mtx_cur_gen);
        while (data->local_gen > data->shared->current_gen &&
               !data->shared->stop_required) {
            pthread_cond_wait(&data->shared->cv_cur_gen, &data->shared->mtx_cur_gen);
        }
        stop_required = data->shared->stop_required;
        pthread_mutex_unlock(&data->shared->mtx_cur_gen);
        if (stop_required) {
            break;
        }


        pthread_mutex_lock(&data->mtx_local_gen);
        for (int x = data->min_x; x <= data->max_x; ++x) {
            for (int y = 0; y < data->shared->field->height; ++y) {
                int alive_neigbors = 0;
                for (int i = 0; i < kNeighborsCount; ++i) {
                    int nx = (x + DX[i] + data->shared->field->width) %
                            data->shared->field->width;
                    int ny = (y + DY[i] + data->shared->field->height) %
                            data->shared->field->height;
                    if (*get_cell(data->shared->field, nx, ny)) {
                        ++alive_neigbors;
                    }
                }
                *get_cell(data->shared->next_field, x, y) = (alive_neigbors == 3) ||
                    (alive_neigbors == 2 && *get_cell(data->shared->field, x, y));
            }
        }
        ++data->local_gen;
        pthread_cond_signal(&data->cv_local_gen);
        pthread_mutex_unlock(&data->mtx_local_gen);
    }

    pthread_exit(NULL);
}

void* master_thread(void* arg) {
    struct workers_internal* data = arg;

    bool stop_required = false;

    while (true) {
        pthread_mutex_lock(&data->mtx_req_gen);
        while (data->required_gen <= data->current_gen &&
               !data->stop_required) {
            pthread_cond_wait(&data->cv_req_gen, &data->mtx_req_gen);
        }
        stop_required = data->stop_required;
        pthread_mutex_unlock(&data->mtx_req_gen);

        if (stop_required) {
            break;
        }

        for (int i = 0; i < kSlaveThreadsCount; ++i) {
            pthread_mutex_lock(&data->slave_threads[i].mtx_local_gen);
            while (data->slave_threads[i].local_gen <= data->current_gen &&
                   !data->stop_required) {
                pthread_cond_wait(&data->slave_threads[i].cv_local_gen,
                                  &data->slave_threads[i].mtx_local_gen);
            }
            stop_required = data->stop_required;
            pthread_mutex_unlock(&data->slave_threads[i].mtx_local_gen);
            if (stop_required) {
                pthread_exit(NULL);
            }
        }

        pthread_mutex_lock(&data->mtx_cur_gen);
        ++data->current_gen;
        field_t* temp = data->field;
        data->field = data->next_field;
        data->next_field = temp;
        pthread_cond_broadcast(&data->cv_cur_gen);
        pthread_mutex_unlock(&data->mtx_cur_gen);
    }
    pthread_exit(NULL);
}

const char* get_version() {
    return "0.1_pthread";
}

static field_t second_field;

const char* setup_workers(field_t* field, workers_t* workers) {
    workers->impl = calloc(1, sizeof(struct workers_internal));

    workers->impl->required_gen = 0;
    workers->impl->current_gen = 0;
    workers->impl->field = field;
    init_field(&second_field, field->width, field->height);
    workers->impl->next_field = &second_field;
    workers->impl->stop_required = false;

    pthread_cond_init(&workers->impl->cv_req_gen, NULL);
    pthread_cond_init(&workers->impl->cv_cur_gen, NULL);
    pthread_mutex_init(&workers->impl->mtx_cur_gen, NULL);
    pthread_mutex_init(&workers->impl->mtx_req_gen, NULL);

    int last_max_x = -1;
    int stripe_width = (field->width - 1) / kSlaveThreadsCount + 1;
    for (int i = 0; i < kSlaveThreadsCount; ++i) {
        workers->impl->slave_threads[i].min_x = last_max_x + 1;
        last_max_x = workers->impl->slave_threads[i].max_x = min(field->width - 1,
                                            last_max_x + stripe_width);

        workers->impl->slave_threads[i].local_gen = 0;
        workers->impl->slave_threads[i].shared = workers->impl;

        pthread_cond_init(&workers->impl->slave_threads[i].cv_local_gen, NULL);
        pthread_mutex_init(&workers->impl->slave_threads[i].mtx_local_gen, NULL);
    }

    pthread_create(&workers->impl->master_thread, NULL, master_thread, workers->impl);
    for (int i = 0; i < kSlaveThreadsCount; ++i) {
        pthread_create(&workers->impl->slave_threads[i].thread_descr, NULL, slave_thread,
                       workers->impl->slave_threads + i);
    }

    return NULL;
}

void destroy_workers(workers_t* workers) {
    workers->impl->stop_required = true;

    pthread_cond_broadcast(&workers->impl->cv_cur_gen);
    pthread_cond_broadcast(&workers->impl->cv_req_gen);
    for (int i = 0; i < kSlaveThreadsCount; ++i) {
        pthread_cond_broadcast(&workers->impl->slave_threads[i].cv_local_gen);
    }

    void* ret_val = NULL;
    pthread_join(workers->impl->master_thread, &ret_val);
    for (int i = 0; i < kSlaveThreadsCount; ++i) {
        pthread_join(workers->impl->slave_threads[i].thread_descr, &ret_val);
    }

    pthread_mutex_destroy(&workers->impl->mtx_cur_gen);
    pthread_mutex_destroy(&workers->impl->mtx_req_gen);
    pthread_cond_destroy(&workers->impl->cv_cur_gen);
    pthread_cond_destroy(&workers->impl->cv_req_gen);

    for (int i = 0; i < kSlaveThreadsCount; ++i) {
        pthread_mutex_destroy(&workers->impl->slave_threads[i].mtx_local_gen);
        pthread_cond_destroy(&workers->impl->slave_threads[i].cv_local_gen);
    }

    destroy_field(&second_field);
    free(workers->impl);
}

void dump_field(field_t* field, workers_t* workers) {
    pthread_mutex_lock(&workers->impl->mtx_cur_gen);
    printf("# Current iteration: %d\n", workers->impl->current_gen);
    for (int y = 0; y < workers->impl->field->height; ++y) {
        printf("# ");
        for (int x = 0; x < workers->impl->field->width; ++x) {
            char ch = '_';
            if (*get_cell(workers->impl->field, x, y)) {
                ch = 'O';
            }
            printf("%c", ch);
        }
        printf("\n");
    }
    pthread_mutex_unlock(&workers->impl->mtx_cur_gen);
}

void run(field_t* field, workers_t* workers) {
    int n = 0;
    if (scanf("%d", &n) != 1 || n <= 0) {
        printf("# Positive integer expected\n");
        return;
    }

    pthread_mutex_lock(&workers->impl->mtx_req_gen);
    workers->impl->required_gen += n;
    pthread_cond_signal(&workers->impl->cv_req_gen);
    pthread_mutex_unlock(&workers->impl->mtx_req_gen);
}

void stop(field_t* field, workers_t* workers) {
    pthread_mutex_lock(&workers->impl->mtx_cur_gen);
    pthread_mutex_lock(&workers->impl->mtx_req_gen);
    workers->impl->required_gen = workers->impl->current_gen;
    pthread_cond_signal(&workers->impl->cv_req_gen);
    pthread_mutex_unlock(&workers->impl->mtx_req_gen);
    pthread_mutex_unlock(&workers->impl->mtx_cur_gen);
}

