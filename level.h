#ifndef LEVEL_H
#define LEVEL_H

#include <stdbool.h>

typedef struct LevelData {
    int    *map;
    double *rotations;
    int   map_len;
    int   size_x;
} LevelData;

bool level_load_from_code(const char *code, LevelData *out);
void level_free(LevelData *ld);

#endif
