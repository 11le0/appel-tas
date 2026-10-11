
#include "spikes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


static int pdir_to_index(double deg) {
    int idx = (int)lround((deg + 172.5) / 7.5);
    if (idx < 0) idx = 0;
    if (idx >= SB_NUM_PDIR) idx = SB_NUM_PDIR - 1;
    return idx;
}

static int sdir_deg_to_table_idx(int deg) {
    switch (deg) {
        case -90: return 0;
        case   0: return 1;
        case  90: return 2;
        case 180: return 3;
        default:  return -1;
    }
}

static int maprot_to_table_idx(int rot) {
    int deg = (90 * rot) % 360;
    if (deg > 180) deg -= 360;
    return sdir_deg_to_table_idx(deg);
}

static int shape_to_index(int tile_id) {
    static const int shapes[SB_NUM_SHAPE] = {7, 9, 74, 77, 78, 83};
    for (int i = 0; i < SB_NUM_SHAPE; i++) if (shapes[i] == tile_id) return i;
    return -1;
}

static int8_t g_tile_to_shape[NUM_TILES];
static bool   g_tile_to_shape_ready = false;

static void ensure_tile_to_shape_table(void) {
    if (g_tile_to_shape_ready) return;
    for (int i = 0; i < NUM_TILES; i++) g_tile_to_shape[i] = -1;
    for (int i = 0; i < SB_NUM_SHAPE; i++) {
        static const int shapes[SB_NUM_SHAPE] = {7, 9, 74, 77, 78, 83};
        if (shapes[i] >= 0 && shapes[i] < NUM_TILES) g_tile_to_shape[shapes[i]] = (int8_t)i;
    }
    g_tile_to_shape_ready = true;
}

static int costume_index(const PlayerState *ps) {
    bool isCrouch = (ps->player_state == 2);
    if (!isCrouch) return 0;
    bool isOnWall = (ps_get_wall(ps) != WALL_NONE);
    return isOnWall ? 2 : 1;
}

static inline int floor_div60(int a) {
    if (a >= 0) return a / 60;
    return -(((-a) + 59) / 60);
}


static uint8_t *decode_rle_to_bitpacked(const char *enc, int total_bits) {
    if (!enc || !*enc) return NULL;
    const char *bar = strchr(enc, '|');
    if (!bar) return NULL;

    int cur_bit = atoi(enc);
    const char *p = bar + 1;

    int nbytes = (total_bits + 7) / 8;
    uint8_t *packed = calloc((size_t)nbytes, 1);

    long pos = 0;
    while (*p && pos < total_bits) {
        char *end;
        long run = strtol(p, &end, 10);
        if (end == p) break;
        if (cur_bit == 1) {
            for (long i = 0; i < run && pos + i < total_bits; i++) {
                long bitpos = pos + i;
                packed[bitpos >> 3] |= (uint8_t)(1u << (bitpos & 7));
            }
        }
        pos += run;
        cur_bit ^= 1;
        p = end;
    }
    return packed;
}

static inline bool bitpacked_get(const uint8_t *packed, int idx) {
    return (packed[idx >> 3] >> (idx & 7)) & 1;
}


static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',') p++;
    return p;
}

static char *read_json_string(const char **p) {
    const char *s = *p;
    if (*s != '"') return NULL;
    s++;
    const char *start = s;
    while (*s && *s != '"') s++;
    int len = (int)(s - start);
    char *out = malloc(len + 1);
    memcpy(out, start, len);
    out[len] = '\0';
    *p = s + 1;
    return out;
}

bool spikes_load(SpikeData *sb, const char *json_path) {
    memset(sb, 0, sizeof(*sb));
    sb->dx_origin = 65;
    sb->dy_origin = 64;
    ensure_tile_to_shape_table();

    char *buf = read_file(json_path);
    if (!buf) return false;

    const char *p = strchr(buf, '{');
    if (!p) { free(buf); return false; }
    p++;

    bool saw_table_key = false;
    int entries_loaded = 0;

    while (*p) {
        p = skip_ws(p);
        if (*p == '}' || *p == '\0') break;
        char *key = read_json_string(&p);
        if (!key) break;
        p = skip_ws(p);
        if (*p == ':') p++;
        p = skip_ws(p);

        if (strcmp(key, "width") == 0 || strcmp(key, "height") == 0 ||
            strcmp(key, "dx_origin") == 0 || strcmp(key, "dy_origin") == 0) {
            char *end;
            long v = strtol(p, &end, 10);
            if (strcmp(key, "dx_origin") == 0) sb->dx_origin = (int)v;
            if (strcmp(key, "dy_origin") == 0) sb->dy_origin = (int)v;
            p = end;
        } else if (strcmp(key, "table") == 0) {
            saw_table_key = true;
            p = skip_ws(p);
            if (*p == '{') p++;
            while (*p) {
                p = skip_ws(p);
                if (*p == '}') { p++; break; }
                char *tkey = read_json_string(&p);
                if (!tkey) break;
                p = skip_ws(p);
                if (*p == ':') p++;
                p = skip_ws(p);
                char *tval = read_json_string(&p);

                double pdir; int sdir, sid; char costume[24];
                if (sscanf(tkey, "%lf|%d|%d|%23[^|]", &pdir, &sdir, &sid, costume) == 4) {
                    int pi = pdir_to_index(pdir);
                    int si = sdir_deg_to_table_idx(sdir);
                    int shi = shape_to_index(sid);
                    int ci;
                    if (strcmp(costume, "Wall Crouch") == 0)      ci = 2;
                    else if (strcmp(costume, "Crouch") == 0)      ci = 1;
                    else                                          ci = 0;
                    if (pi >= 0 && si >= 0 && shi >= 0) {
                        sb->bits[pi][si][shi][ci] =
                            decode_rle_to_bitpacked(tval, SB_W * SB_H);
                        if (sb->bits[pi][si][shi][ci]) entries_loaded++;
                    }
                }
                free(tkey);
                free(tval);
            }
        } else {
            while (*p && *p != ',' && *p != '}') p++;
        }
        free(key);
    }

    free(buf);

    if (!saw_table_key || entries_loaded == 0) {
        spikes_free(sb);
        return false;
    }

    sb->loaded = true;
    return true;
}

void spikes_free(SpikeData *sb) {
    if (!sb) return;
    for (int a = 0; a < SB_NUM_PDIR; a++)
        for (int b = 0; b < SB_NUM_SDIR; b++)
            for (int c = 0; c < SB_NUM_SHAPE; c++)
                for (int d = 0; d < SB_NUM_COSTUME; d++)
                    free(sb->bits[a][b][c][d]);
    memset(sb, 0, sizeof(*sb));
}

static inline bool check_tile(SpikeData *sb, AppelPhysics *ap,
                               int tx, int ty, int px, int py,
                               int pi, int ci, int rows_count)
{
    if (tx < 0 || tx >= ap->LSX || ty < 0 || ty >= rows_count) return false;
    int idx = tx + ty * ap->LSX;
    int tile = ap->MAP[idx];
    if (tile < 0 || tile >= NUM_TILES) return false;

    int shi = g_tile_to_shape[tile];
    if (shi < 0) return false;
    int si = maprot_to_table_idx((int)ap->MAP_R[idx]);
    if (si < 0) return false;

    uint8_t *packed = sb->bits[pi][si][shi][ci];
    if (!packed) return false;

    int centerX = tx * 60 + 30;
    int centerY = ty * 60 + 30;
    int dx = px - centerX;
    int dy = py - centerY;

    int col = dx + sb->dx_origin;
    int row = dy + sb->dy_origin;
    if (col < 0 || col >= SB_W || row < 0 || row >= SB_H) return false;

    return bitpacked_get(packed, row * SB_W + col);
}

bool is_player_touching_spike(SpikeData *sb, AppelPhysics *ap, PlayerState *ps) {
    if (!sb->loaded) return false;

    int px = (int)lround(ps->PLAYER_X);
    int py = (int)lround(ps->PLAYER_Y);

    int psz1 = ps_get_psz(ps, 1);
    int psz2 = ps_get_psz(ps, 2);
    int psz3 = ps_get_psz(ps, 3);
    int psz4 = ps_get_psz(ps, 4);

    int pLeft   = px - psz4 - sb->dx_origin;
    int pRight  = px + psz2 + sb->dx_origin;
    int pTop    = py - psz3 - sb->dy_origin;
    int pBottom = py + psz1 + sb->dy_origin;

    int lo, hi;
    lo = pLeft < pRight ? pLeft : pRight;
    hi = pLeft < pRight ? pRight : pLeft;
    int txMin = floor_div60(lo);
    int txMax = floor_div60(hi);

    lo = pTop < pBottom ? pTop : pBottom;
    hi = pTop < pBottom ? pBottom : pTop;
    int tyMin = floor_div60(lo);
    int tyMax = floor_div60(hi);

    int pi = pdir_to_index(ps_get_direction(ps));
    int ci = costume_index(ps);
    int rows_count = ap->LSX ? ap->map_len / ap->LSX : 0;

    for (int ty = tyMin; ty <= tyMax; ty++) {
        for (int tx = txMin; tx <= txMax; tx++) {
            if (check_tile(sb, ap, tx, ty, px, py, pi, ci, rows_count)) return true;
        }
    }
    return false;
}
