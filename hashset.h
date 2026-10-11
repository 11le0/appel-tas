#ifndef HASHSET_H
#define HASHSET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include "physics.h"

typedef struct __attribute__((packed)) StateKey {
    float    x, y;
    int16_t  sx, sy;
    int16_t  direction;
    uint8_t  is_jumping;
    uint8_t  is_falling;
    uint8_t  misc1;
    uint8_t  misc2;
} StateKey;

static inline int16_t sk_clamp_i16(double v)
{
    if (v > 32767.0) return 32767;
    if (v < -32768.0) return -32768;
    return (int16_t)lround(v);
}

static inline void state_key_build(const PlayerState *ps, StateKey *k)
{
    k->x = ps->PLAYER_X;
    k->y = ps->PLAYER_Y;
    k->sx = sk_clamp_i16(ps->PLAYER_SX * 256.0);
    k->sy = sk_clamp_i16(ps->PLAYER_SY * 256.0);
    k->direction = sk_clamp_i16(ps->direction * 10.0);
    k->is_jumping = ps->is_jumping;
    k->is_falling = ps->is_falling;
    k->misc1 = (uint8_t)(ps->flipped_raw | (ps->wall_raw << 2));
    k->misc2 = (uint8_t)(ps->player_state | (ps->KEY_UP << 3));
}

typedef struct HashSet HashSet;

HashSet *hashset_create(size_t initial_capacity_pow2);
void     hashset_free(HashSet *hs);
void     hashset_clear(HashSet *hs);
size_t   hashset_count(const HashSet *hs);

bool hashset_insert_if_absent(HashSet *hs, const StateKey *key);

#endif
