#include "hashset.h"

#include <stdlib.h>
#include <string.h>

typedef struct Slot {
    bool     occupied;
    StateKey key;
} Slot;

struct HashSet {
    Slot   *slots;
    size_t  capacity;
    size_t  count;
};

static uint64_t fnv1a(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

HashSet *hashset_create(size_t initial_capacity_pow2)
{
    HashSet *hs = malloc(sizeof(HashSet));
    if (initial_capacity_pow2 < 16) initial_capacity_pow2 = 16;
    size_t cap = 16;
    while (cap < initial_capacity_pow2) cap <<= 1;

    hs->slots = calloc(cap, sizeof(Slot));
    hs->capacity = cap;
    hs->count = 0;
    return hs;
}

void hashset_free(HashSet *hs)
{
    if (!hs) return;
    free(hs->slots);
    free(hs);
}

void hashset_clear(HashSet *hs)
{
    memset(hs->slots, 0, hs->capacity * sizeof(Slot));
    hs->count = 0;
}

size_t hashset_count(const HashSet *hs) { return hs->count; }

static bool insert_into(Slot *slots, size_t capacity, const StateKey *key, size_t *out_count)
{
    uint64_t h = fnv1a(key, sizeof(StateKey));
    size_t mask = capacity - 1;
    size_t idx = (size_t)(h & mask);

    for (;;) {
        Slot *s = &slots[idx];
        if (!s->occupied) {
            s->occupied = true;
            s->key = *key;
            if (out_count) (*out_count)++;
            return true;
        }
        if (memcmp(&s->key, key, sizeof(StateKey)) == 0) {
            return false;
        }
        idx = (idx + 1) & mask;
    }
}

static void hashset_grow(HashSet *hs)
{
    size_t old_cap = hs->capacity;
    Slot *old_slots = hs->slots;

    size_t new_cap = old_cap * 2;
    Slot *new_slots = calloc(new_cap, sizeof(Slot));

    for (size_t i = 0; i < old_cap; i++) {
        if (old_slots[i].occupied) {
            insert_into(new_slots, new_cap, &old_slots[i].key, NULL);
        }
    }

    free(old_slots);
    hs->slots = new_slots;
    hs->capacity = new_cap;
}

bool hashset_insert_if_absent(HashSet *hs, const StateKey *key)
{
    if ((hs->count + 1) * 10 >= hs->capacity * 7) {
        hashset_grow(hs);
    }
    return insert_into(hs->slots, hs->capacity, key, &hs->count);
}
