#ifndef PLAYERSTATE_JSON_H
#define PLAYERSTATE_JSON_H

#include <stdbool.h>
#include "physics.h"

bool playerstate_load_json(const char *json_text, PlayerState *ps);

#endif
