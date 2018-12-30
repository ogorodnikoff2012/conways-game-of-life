#include <interface.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

static inline int min(int x, int y) {
    return x < y ? x : y;
}

typedef enum {
    kInitialHeightTag,
    kInitialSizeTag,
    kInitialDataTag,
    kStopRequiredTag,
    kDataTag,
    kCmdTag,
} tag_t;

typedef struct {
    int from, to;
} range_t;

struct workers_internal {
    field_t* field;
    field_t* next_field;
    range_t* ranges;
    int ranges_cnt;

    int cur_gen, req_gen;
};

const char* get_version() {
    return "0.1_mpi";
}

static field_t second_field;

static int get_slaves_count() {
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    return world_size - 2;
}

static inline int get_slave_rank(int slave_index) {
    return slave_index + 2;
}

static inline int get_master_rank() {
    return 1;
}

static inline int get_io_rank() {
    return 0;
}

const char* setup_workers(field_t* field, workers_t* workers) {
    workers->impl = calloc(1, sizeof(struct workers_internal));
    init_field(&second_field, field->width, field->height);
    workers->impl->field = field;
    workers->impl->next_field = &second_field;

    workers->impl->cur_gen = 0;
    workers->impl->req_gen = 0;

    int num_of_slaves = get_slaves_count();
    workers->impl->ranges = calloc(num_of_slaves, sizeof(range_t));
    workers->impl->ranges_cnt = num_of_slaves;

    int last_max_x = -1;
    int stripe_width = (field->width - 1) / num_of_slaves + 1;

    for (int i = 0; i < num_of_slaves; ++i) {
        workers->impl->ranges[i].from = last_max_x + 1;
        last_max_x = workers->impl->ranges[i].to
                   = min(last_max_x + stripe_width, field->width - 1);

        MPI_Send(&field->height, 1, MPI_INT, get_slave_rank(i),
                 kInitialHeightTag, MPI_COMM_WORLD);
        MPI_Send(workers->impl->ranges + i, 2, MPI_INT, get_slave_rank(i),
                 kInitialSizeTag, MPI_COMM_WORLD);
        MPI_Send(get_cell(field, workers->impl->ranges[i].from, 0),
                (workers->impl->ranges[i].to - workers->impl->ranges[i].from + 1)
                * field->height, MPI_INT, get_slave_rank(i),
                kInitialDataTag, MPI_COMM_WORLD);
    }

    return NULL;
}

void run_slave_loop() {
    int height;
    range_t range;
    MPI_Recv(&height, 1, MPI_INT, get_master_rank(), kInitialHeightTag,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&range, 2, MPI_INT, get_master_rank(), kInitialSizeTag,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    field_t field, next_field;
    init_field(&field, range.to - range.from + 3, height);
    init_field(&next_field, range.to - range.from + 3, height);

    MPI_Recv(get_cell(&field, 1, 0), (range.to - range.from + 1) * height, MPI_INT,
             get_master_rank(), kInitialDataTag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    int stop_required = 0;
    while (true) {
        MPI_Recv(&stop_required, 1, MPI_INT, get_master_rank(), kStopRequiredTag,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (stop_required == 1) {
            break;
        }

        MPI_Recv(get_cell(&field, 0, 0), height, MPI_INT, get_master_rank(),
                 kDataTag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(get_cell(&field, field.width - 1, 0), height, MPI_INT,
                get_master_rank(), kDataTag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int x = 1; x < field.width - 1; ++x) {
            for (int y = 0; y < height; ++y) {
                int alive_negihbors = 0;
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        int nx = x + dx;
                        int ny = (y + dy + height) % height;
                        if (*get_cell(&field, nx, ny)) {
                            ++alive_negihbors;
                        }
                    }
                }

                *get_cell(&next_field, x, y) = (alive_negihbors == 3) ||
                    (alive_negihbors == 2 && *get_cell(&field, x, y));
            }
        }

        bool* temp = field.buffer;
        field.buffer = next_field.buffer;
        next_field.buffer = temp;

        MPI_Send(get_cell(&field, 1, 0), (range.to - range.from + 1) * height,
                 MPI_INT, get_master_rank(), kInitialDataTag, MPI_COMM_WORLD);
    }

    destroy_field(&field);
    destroy_field(&next_field);
}

static void master_dump_field(struct workers_internal* data) {
    printf("# Current iteration: %d\n", data->cur_gen);
    for (int y = 0; y < data->field->height; ++y) {
        printf("# ");
        for (int x = 0; x < data->field->width; ++x) {
            char ch = '_';
            if (*get_cell(data->field, x, y)) {
                ch = 'O';
            }
            printf("%c", ch);
        }
        printf("\n");
    }
}

static void master_run(struct workers_internal* data) {
    int n;
    MPI_Recv(&n, 1, MPI_INT, get_io_rank(), kDataTag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    data->req_gen += n;
}

static void master_stop(struct workers_internal* data) {
    data->req_gen = min(data->req_gen, data->cur_gen + 1);
}

void run_master_loop(struct workers_internal* data) {
    int stop_required = 0;

    int responses_left = 0;
    char cmd;

    MPI_Request request;
    MPI_Status status;
    int flag;

    while (!(stop_required && responses_left == 0)) {
        MPI_Irecv(&cmd, 1, MPI_BYTE, get_io_rank(), kCmdTag, MPI_COMM_WORLD, &request);
        flag = 0;
        MPI_Test(&request, &flag, &status);
        if (flag) {
            switch (cmd) {
                case 'D': /* Dump */
                    master_dump_field(data);
                    break;
                case 'R': /* Run */
                    master_run(data);
                    break;
                case 'S': /* Stop */
                    master_stop(data);
                    break;
                case 'H': /* Halt */
                    stop_required = 1;
                    break;
            }
            MPI_Send(&flag, 1, MPI_BYTE, get_io_rank(), kCmdTag, MPI_COMM_WORLD);
        }

        if (responses_left == 0 && !stop_required && data->cur_gen < data->req_gen) {
            responses_left = data->ranges_cnt;

            field_t* temp = data->field;
            data->field = data->next_field;
            data->next_field = temp;
            ++data->cur_gen;

            for (int i = 0; i < data->ranges_cnt; ++i) {
                MPI_Send(&stop_required, 1, MPI_INT, get_slave_rank(i),
                         kStopRequiredTag, MPI_COMM_WORLD);

                int left_x = (data->ranges[i].from - 1 + data->field->width) %
                             data->field->width;
                MPI_Send(get_cell(data->field, left_x, 0), data->field->height, MPI_INT,
                        get_slave_rank(i), kDataTag, MPI_COMM_WORLD);

                int right_x = (data->ranges[i].to + 1 + data->field->width) %
                             data->field->width;
                MPI_Send(get_cell(data->field, right_x, 0), data->field->height, MPI_INT,
                        get_slave_rank(i), kDataTag, MPI_COMM_WORLD);
            }
        }

        for (int i = 0; i < data->ranges_cnt; ++i) {
            MPI_Irecv(get_cell(data->next_field, data->ranges[i].from, 0),
                      (data->ranges[i].to - data->ranges[i].from + 1) *
                      data->field->height, MPI_INT, get_slave_rank(i), kDataTag,
                      MPI_COMM_WORLD, &request);
            flag = 0;
            MPI_Test(&request, &flag, &status);
            if (flag) {
                --responses_left;
            }
        }
    }
}

void run_controller_loop(field_t* field, workers_t* workers) {
    if (field == NULL) {
        run_slave_loop();
    } else {
        run_master_loop(workers->impl);
    }
}

void destroy_workers(workers_t* workers) {
    int num_of_slaves = get_slaves_count();
    int stop = 1;
    for (int i = 0; i < num_of_slaves; ++i) {
        MPI_Send(&stop, 1, MPI_INT, get_slave_rank(i), kStopRequiredTag, MPI_COMM_WORLD);
    }

    destroy_field(&second_field);
    free(workers->impl->ranges);
    free(workers->impl);
}

static void send_command(char cmd, int arg) {
    MPI_Send(&cmd, 1, MPI_BYTE, get_master_rank(), kCmdTag, MPI_COMM_WORLD);
    if (cmd == 'R') {
        MPI_Send(&arg, 1, MPI_INT, get_master_rank(), kDataTag, MPI_COMM_WORLD);
    }
    MPI_Recv(&cmd, 1, MPI_BYTE, get_master_rank(), kCmdTag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

void dump_field(field_t* field, workers_t* workers) {
    send_command('D', 0);
}

void run(field_t* field, workers_t* workers) {
    int n;
    if (scanf("%d", &n) != 1 || n <= 0) {
        printf("# A positive integer required!\n");
        return;
    }
    send_command('R', n);
}

void stop(field_t* field, workers_t* workers) {
    send_command('S', 0);
}

void stop_emulation(workers_t* workers) {
    send_command('H', 0);
}
