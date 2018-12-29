#include <interface.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

void init_field(field_t* field, int width, int height) {
    field->width = width;
    field->height = height;
    field->buffer = calloc(field->width * field->height, sizeof(bool));
}

const char* setup_field(int argc, char* argv[], field_t* field) {
    const char* filename = "game.config";
    if (argc >= 2) {
        filename = argv[1];
    }

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        return strerror(errno);
    }

    int num_of_cells = 0;
    int width = 0, height = 0;
    if (fscanf(f, "%d%d%d", &width, &height, &num_of_cells) != 3) {
        fclose(f);
        return "Ill-formed configuration file";
    }

    init_field(field, width, height);

    for (int i = 0; i < num_of_cells; ++i) {
        int x = 0, y = 0;
        if (fscanf(f, "%d%d", &x, &y) != 2) {
            free(field->buffer);
            fclose(f);
            return "Ill-formed configuration file";
        }
        *get_cell(field, x, y) = true;
    }

    fclose(f);
    return NULL;
}

void destroy_field(field_t* field) {
    free(field->buffer);
    field->width = field->height = 0;
}
