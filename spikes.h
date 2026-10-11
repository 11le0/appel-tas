#ifndef SPIKES_H
#define SPIKES_H

#include "physics.h"
#include <stdint.h>
#include <stdbool.h>

#define SB_NUM_PDIR    48
#define SB_NUM_SDIR     4
#define SB_NUM_SHAPE    6
#define SB_NUM_COSTUME  3

#define SB_W 130
#define SB_H 129

typedef struct {
    uint8_t *bits[SB_NUM_PDIR][SB_NUM_SDIR][SB_NUM_SHAPE][SB_NUM_COSTUME];
    int dx_origin, dy_origin;
    bool loaded;
} SpikeData;

bool spikes_load(SpikeData *sb, const char *json_path);
void spikes_free(SpikeData *sb);

bool is_player_touching_spike(SpikeData *sb, AppelPhysics *ap, PlayerState *ps);

#endif
