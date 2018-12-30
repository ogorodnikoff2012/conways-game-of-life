#pragma once

#include <stdbool.h>

const char* get_version();

typedef struct {
    int width, height;
    bool* buffer;
} field_t;

struct workers_internal;

typedef struct {
    struct workers_internal* impl;
} workers_t;

static inline bool* get_cell(field_t* field, int x, int y) {
    return field->buffer + x * field->height + y;
}

const char* setup_field(int argc, char* argv[], field_t* field);
void init_field(field_t* field, int width, int height);
void destroy_field(field_t* field);

const char* setup_workers(field_t* field, workers_t* workers);
void destroy_workers(workers_t* workers);

void dump_field(field_t*, workers_t*);
void run       (field_t*, workers_t*);
void stop      (field_t*, workers_t*);

void run_controller_loop(field_t*, workers_t*);
void stop_emulation(workers_t*);
