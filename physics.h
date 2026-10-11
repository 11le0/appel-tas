#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#define NUM_TILES 87
#define WALL_NONE -999

typedef struct PlayerState {
    double PLAYER_X, PLAYER_Y;
    double PLAYER_SX, PLAYER_SY;

    uint8_t PSZ_packed;
    double  direction;
    unsigned flipped_raw   : 2;
    unsigned wall_raw      : 2;

    uint8_t is_jumping;
    uint8_t is_falling;

    unsigned player_state : 3;

    unsigned KEY_LEFT  : 2;
    unsigned KEY_RIGHT : 2;
    unsigned KEY_DOWN  : 2;
    unsigned KEY_UP    : 2;

    unsigned PLAYER_DEATH             : 1;
    unsigned wasInSpikeTileLastFrame  : 1;
} PlayerState;

static inline int8_t ps_get_psz(const PlayerState *s, int i) {
    static const int8_t VALS[4] = {7, 13, 17, 0};
    if (i <= 0 || i > 4) return 0;
    unsigned idx = (s->PSZ_packed >> ((i - 1) * 2)) & 0x3u;
    return VALS[idx];
}
static inline void ps_set_psz(PlayerState *s, int i, int val) {
    if (i <= 0 || i > 4) return;
    unsigned idx = (val == 7) ? 0u : (val == 13) ? 1u : (val == 17) ? 2u : 3u;
    unsigned shift = (unsigned)(i - 1) * 2u;
    s->PSZ_packed = (uint8_t)((s->PSZ_packed & ~(0x3u << shift)) | (idx << shift));
}

static inline double ps_get_direction(const PlayerState *s) {
    return s->direction;
}
static inline void ps_set_direction(PlayerState *s, double deg) {
    s->direction = deg;
}

static inline int ps_get_flipped(const PlayerState *s) {
    return (int)s->flipped_raw - 1;
}
static inline void ps_set_flipped(PlayerState *s, int v) {
    if (v < -1) v = -1;
    if (v > 1) v = 1;
    s->flipped_raw = (unsigned)(v + 1);
}

static inline int ps_get_wall(const PlayerState *s) {
    switch (s->wall_raw) {
        case 1: return -1;
        case 2: return 0;
        case 3: return 1;
        default: return WALL_NONE;
    }
}
static inline void ps_set_wall(PlayerState *s, int v) {
    unsigned raw;
    if (v == WALL_NONE)      raw = 0u;
    else if (v == -1)        raw = 1u;
    else if (v == 0)         raw = 2u;
    else         raw = 3u;
    s->wall_raw = raw;
}

typedef struct AppelPhysics {
    int *MAP;
    double *MAP_R;
    void *MAP_DATA;
    int LSX;
    int map_len;
    int MASK[NUM_TILES][4];
    double toverlap;
    int mask_char;
    double overlap;
    double temp_coords[2];
    double friction;
    double friction_dx;
    double friction_dy;
    int RESOLVE[17];
    void (*setMap)(int, int, PlayerState *);

    void *spikes;
} AppelPhysics;

typedef struct ReplayResult {
    char **inputs;
    int count;
} ReplayResult;

void appel_physics_init(AppelPhysics *ap, int *mapData, double *mapRotations, void *MAP_DATA, int LSX, int map_len, void (*setMap)(int, int, PlayerState *));
bool appel_is_flag_at(AppelPhysics *ap, double x, double y);
int appel_key_code(const char *keys);
void appel_tick(AppelPhysics *ap, PlayerState *ps, int inputKeys);
PlayerState appel_create_default_player_state(AppelPhysics *ap, double x, double y, int map_len);
ReplayResult appel_decode_replay(const char *replayCode);
void appel_free_replay(ReplayResult *r);

#endif