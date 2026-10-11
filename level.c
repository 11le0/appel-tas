#include "level.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char **tok;
    int    count;
} TokenList;

static TokenList tokenize(const char *s, char delim)
{
    TokenList tl = { NULL, 0 };
    int cap = 64;
    tl.tok = malloc(cap * sizeof(char *));

    const char *start = s;
    for (const char *p = s; ; p++) {
        if (*p == delim || *p == '\0') {
            size_t len = (size_t)(p - start);
            char *seg = malloc(len + 1);
            memcpy(seg, start, len);
            seg[len] = '\0';

            if (tl.count >= cap) {
                cap *= 2;
                tl.tok = realloc(tl.tok, cap * sizeof(char *));
            }
            tl.tok[tl.count++] = seg;

            if (*p == '\0') break;
            start = p + 1;
        }
    }
    return tl;
}

static void tokenlist_free(TokenList *tl)
{
    for (int i = 0; i < tl->count; i++) free(tl->tok[i]);
    free(tl->tok);
    tl->tok = NULL;
    tl->count = 0;
}

static char *char_replace(const char *src, char from, char to)
{
    size_t n = strlen(src);
    char *out = malloc(n + 1);
    for (size_t i = 0; i < n; i++) out[i] = (src[i] == from) ? to : src[i];
    out[n] = '\0';
    return out;
}

static char **read_segment(TokenList *tl, int *cursor, int *seg_count)
{
    int start = *cursor;
    int end = -1;
    for (int i = start; i < tl->count; i++) {
        if (tl->tok[i][0] == '\0') { end = i; break; }
    }

    int stop = (end == -1) ? tl->count : end;
    int n = stop - start;
    char **seg = (n > 0) ? malloc(n * sizeof(char *)) : NULL;
    for (int i = 0; i < n; i++) seg[i] = tl->tok[start + i];

    *cursor = (end == -1) ? tl->count : end + 1;
    *seg_count = n;
    return seg;
}

bool level_load_from_code(const char *code, LevelData *out)
{
    memset(out, 0, sizeof(*out));

    size_t code_len = strlen(code);
    if (code_len < 7) return false;

    const char *body = code + 7;

    char *step1 = char_replace(body, 'C', '-');
    char *step2 = char_replace(step1, 'B', '+');
    free(step1);

    TokenList tokens = tokenize(step2, 'Z');
    free(step2);

    if (tokens.count < 1) { tokenlist_free(&tokens); return false; }

    int size_x = atoi(tokens.tok[0]);
    if (size_x <= 0) { tokenlist_free(&tokens); return false; }

    int cursor = 1;
    int map_data_n, map_r_data_n, map_data2_n, wall_data_n;
    char **map_data    = read_segment(&tokens, &cursor, &map_data_n);
    char **map_r_data  = read_segment(&tokens, &cursor, &map_r_data_n);
    char **map_data2   = read_segment(&tokens, &cursor, &map_data2_n);
    char **wall_data    = read_segment(&tokens, &cursor, &wall_data_n);
    (void)map_data2; (void)wall_data;

    int map_cap = 1024, map_len = 0;
    int *map = malloc(map_cap * sizeof(int));
    for (int i = 0; i + 1 < map_data_n; i += 2) {
        int value = (int)atof(map_data[i]);
        int count = (int)atof(map_data[i + 1]);
        if (count < 0) count = 0;
        while (map_len + count > map_cap) { map_cap *= 2; map = realloc(map, map_cap * sizeof(int)); }
        for (int j = 0; j < count; j++) map[map_len++] = value;
    }

    int mapr_cap = 1024, mapr_len = 0;
    double *map_r = malloc(mapr_cap * sizeof(double));
    for (int i = 0; i + 1 < map_r_data_n; i += 2) {
        const char *vstr = map_r_data[i];
        double value;
        if (strcmp(vstr, "Infinity") == 0 || strchr(vstr, 'e') || strchr(vstr, 'E')) {
            value = 1.0;
        } else {
            value = atof(vstr);
        }
        int count = (int)atof(map_r_data[i + 1]);
        if (count < 0) count = 0;
        while (mapr_len + count > mapr_cap) { mapr_cap *= 2; map_r = realloc(map_r, mapr_cap * sizeof(double)); }
        for (int j = 0; j < count; j++) map_r[mapr_len++] = value;
    }

    free(map_data);
    free(map_r_data);
    free(map_data2);
    free(wall_data);
    tokenlist_free(&tokens);

    if (map_len == 0 || mapr_len != map_len) {
        free(map);
        free(map_r);
        return false;
    }

    out->map        = map;
    out->rotations   = map_r;
    out->map_len     = map_len;
    out->size_x      = size_x;
    return true;
}

void level_free(LevelData *ld)
{
    free(ld->map);
    free(ld->rotations);
    ld->map = NULL;
    ld->rotations = NULL;
    ld->map_len = 0;
}
