#include <interface.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define PTHREAD 1
#define OPENMP  2
#define MPI     3

#define TRY(call) {                                             \
    const char* err_msg_internal_ = NULL;                       \
    err_msg_internal_ = ( call );                               \
    if (err_msg_internal_ != NULL) {                            \
        handle_error(err_msg_internal_, __FILE__, __LINE__);    \
    }                                                           \
}

#if BACKEND == OPENMP
#include <omp.h>
#endif

#if BACKEND == MPI
#include <mpi.h>
#endif

typedef void(*handler_t)(field_t*, workers_t*);

typedef struct {
    const char* name;
    const char* description;
    handler_t handler;
} command_t;

void print_help(field_t*, workers_t*);

void handle_error(const char* msg, const char* file, int line) {
    fprintf(stderr, "An error occured in file %s, line %d: %s\n", file, line, msg);
    exit(1);
}

const command_t kCommands[] = {
    {"help", "print this text", print_help},
    {"dump", "print current field state", dump_field},
    {"run",  "run #N iterations", run},
    {"stop", "break calculations", stop},
    {"exit", "close program", NULL},
};

const int kCommandsCount = sizeof(kCommands) / sizeof(command_t);

void print_help(field_t* field, workers_t* workers) {
    printf("# Available commands:\n");
    for (int i = 0; i < kCommandsCount; ++i) {
        printf("# %-8s - %s\n", kCommands[i].name, kCommands[i].description);
    }
}

void print_title() {
    printf("########################################\n"
           "##       Conway's Game of Life        ##\n"
           "##   (c) Vladimir Ogorodnikov, 2018   ##\n"
           "########################################\n");
    printf("# Version: %s\n", get_version());
    printf("# To get help, type `help` command\n");
}

void run_io_loop(field_t* field, workers_t* workers) {
    bool exit_required = false;
    const int buffer_size = 100;
    char buffer[buffer_size + 1];

    for (int i = 0; i <= buffer_size; ++i) {
        buffer[i] = '\0';
    }

    while (!exit_required) {
        printf(">> ");
        fflush(stdout);
        if (scanf("%100s", buffer) == EOF) {
            break;
        }

        bool command_found = false;
        for (int i = 0; i < kCommandsCount && !command_found; ++i) {
            if (strcmp(kCommands[i].name, buffer) == 0) {
                command_found = true;
                if (kCommands[i].handler) {
                    kCommands[i].handler(field, workers);
                } else {
                    exit_required = true;
                }
            }
        }
        if (!command_found) {
            printf("# Unknown command: %s\n", buffer);
        }
    }
    printf("# Bye!\n");
}

#if BACKEND == MPI
int main(int argc, char* argv[]) {
    MPI_Init(NULL, NULL);

    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (world_size < 3) {
        printf("Not enough processes, halting!\n");
        MPI_Finalize();
        return 1;
    }

    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    if (world_rank == 0) {
        print_title();
        run_io_loop(NULL, NULL);
        stop_emulation(NULL);
    } else if (world_rank == 1) {
        field_t field;
        TRY(setup_field(argc, argv, &field));
        workers_t workers;
        TRY(setup_workers(&field, &workers));
        run_controller_loop(&field, &workers);
        destroy_workers(&workers);
        destroy_field(&field);
    } else {
        run_controller_loop(NULL, NULL);
    }

    MPI_Finalize();
    return 0;
}
#else
int main(int argc, char* argv[]) {
    print_title();
    field_t field;
    TRY(setup_field(argc, argv, &field));
    workers_t workers;
    TRY(setup_workers(&field, &workers));

#if BACKEND == OPENMP

#pragma omp parallel default(shared) num_threads(4)
{
#pragma omp sections
    {
    #pragma omp section
    {
        run_io_loop(&field, &workers);
        stop_emulation(&workers);
    }

    #pragma omp section
    run_controller_loop(&field, &workers);
    }
}

#elif BACKEND == PTHREAD
    run_io_loop(&field, &workers);
#elif BACKEND != MPI
#error "BACKEND is not selected!"
#endif
    destroy_workers(&workers);
    destroy_field(&field);
    return 0;
}
#endif
