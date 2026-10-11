#include "playerstate_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char *p;
} JParser;

static void skip_ws(JParser *jp) {
    while (*jp->p && isspace((unsigned char)*jp->p)) jp->p++;
}

static char *parse_jstring(JParser *jp)
{
    if (*jp->p != '"') return NULL;
    jp->p++;
    size_t cap = 64, len = 0;
    char *out = malloc(cap);
    while (*jp->p && *jp->p != '"') {
        char c = *jp->p++;
        if (c == '\\' && *jp->p) {
            char e = *jp->p++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                default:  c = e;    break;
            }
        }
        if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
        out[len++] = c;
    }
    if (*jp->p == '"') jp->p++;
    out[len] = '\0';
    return out;
}

static void skip_value(JParser *jp)
{
    skip_ws(jp);
    if (*jp->p == '{' || *jp->p == '[') {
        char open = *jp->p, close = (open == '{') ? '}' : ']';
        int depth = 0;
        bool in_str = false;
        do {
            char c = *jp->p;
            if (!c) return;
            if (in_str) {
                if (c == '\\' && jp->p[1]) { jp->p += 2; continue; }
                if (c == '"') in_str = false;
            } else {
                if (c == '"') in_str = true;
                else if (c == open) depth++;
                else if (c == close) depth--;
            }
            jp->p++;
        } while (depth > 0);
    } else if (*jp->p == '"') {
        char *s = parse_jstring(jp);
        free(s);
    } else {
        while (*jp->p && *jp->p != ',' && *jp->p != '}' && *jp->p != ']' && !isspace((unsigned char)*jp->p)) jp->p++;
    }
}

static int parse_number_array(JParser *jp, double *out, int max)
{
    int n = 0;
    skip_ws(jp);
    if (*jp->p != '[') { skip_value(jp); return 0; }
    jp->p++;
    skip_ws(jp);
    while (*jp->p && *jp->p != ']') {
        char *end;
        double v = strtod(jp->p, &end);
        if (end == jp->p) {
            skip_value(jp);
        } else {
            if (n < max) out[n++] = v;
            jp->p = end;
        }
        skip_ws(jp);
        if (*jp->p == ',') { jp->p++; skip_ws(jp); }
    }
    if (*jp->p == ']') jp->p++;
    return n;
}

bool playerstate_load_json(const char *json_text, PlayerState *ps)
{
    JParser jp = { json_text };
    skip_ws(&jp);
    if (*jp.p != '{') return false;
    jp.p++;

    for (;;) {
        skip_ws(&jp);
        if (*jp.p == '}') { jp.p++; break; }
        if (*jp.p != '"') return false;

        char *key = parse_jstring(&jp);
        skip_ws(&jp);
        if (*jp.p != ':') { free(key); return false; }
        jp.p++;
        skip_ws(&jp);

        if (strcmp(key, "PLAYER_X") == 0) {
            ps->PLAYER_X = strtod(jp.p, (char **)&jp.p);
        } else if (strcmp(key, "PLAYER_Y") == 0) {
            ps->PLAYER_Y = strtod(jp.p, (char **)&jp.p);
        } else if (strcmp(key, "PLAYER_SX") == 0) {
            ps->PLAYER_SX = strtod(jp.p, (char **)&jp.p);
        } else if (strcmp(key, "PLAYER_SY") == 0) {
            ps->PLAYER_SY = strtod(jp.p, (char **)&jp.p);
        } else if (strcmp(key, "PLAYER_DEATH") == 0) {
            ps->PLAYER_DEATH = (strncmp(jp.p, "true", 4) == 0) ? 1 : 0;
            skip_value(&jp);
        } else if (strcmp(key, "is_jumping") == 0) {
            ps->is_jumping = (uint8_t)strtol(jp.p, (char **)&jp.p, 10);
        } else if (strcmp(key, "is_falling") == 0) {
            long v = strtol(jp.p, (char **)&jp.p, 10);
            if (v > 255) v = 255;
            if (v < 0) v = 0;
            ps->is_falling = (uint8_t)v;
        } else if (strcmp(key, "flipped") == 0) {
            long v = strtol(jp.p, (char **)&jp.p, 10);
            ps_set_flipped(ps, (int)v);
        } else if (strcmp(key, "player_state") == 0) {
            long v = strtol(jp.p, (char **)&jp.p, 10);
            ps->player_state = (unsigned)v;
        } else if (strcmp(key, "player_wall") == 0) {
            if (strncmp(jp.p, "null", 4) == 0) {
                ps_set_wall(ps, WALL_NONE);
                jp.p += 4;
            } else {
                long v = strtol(jp.p, (char **)&jp.p, 10);
                ps_set_wall(ps, (int)v);
            }
        } else if (strcmp(key, "direction") == 0) {
            double v = strtod(jp.p, (char **)&jp.p);
            ps_set_direction(ps, v);
        } else if (strcmp(key, "KEY_LEFT") == 0) {
            ps->KEY_LEFT = (unsigned)strtol(jp.p, (char **)&jp.p, 10);
        } else if (strcmp(key, "KEY_RIGHT") == 0) {
            ps->KEY_RIGHT = (unsigned)strtol(jp.p, (char **)&jp.p, 10);
        } else if (strcmp(key, "KEY_DOWN") == 0) {
            ps->KEY_DOWN = (unsigned)strtol(jp.p, (char **)&jp.p, 10);
        } else if (strcmp(key, "KEY_UP") == 0) {
            ps->KEY_UP = (unsigned)strtol(jp.p, (char **)&jp.p, 10);
        } else if (strcmp(key, "wasInSpikeTileLastFrame") == 0) {
            ps->wasInSpikeTileLastFrame = (strncmp(jp.p, "true", 4) == 0) ? 1 : 0;
            skip_value(&jp);
        } else if (strcmp(key, "PSZ") == 0) {
            double vals[8];
            int n = parse_number_array(&jp, vals, 8);
            for (int i = 1; i <= 4 && i < n; i++) {
                ps_set_psz(ps, i, (int)vals[i]);
            }
        } else {
            skip_value(&jp);
        }

        free(key);
        skip_ws(&jp);
        if (*jp.p == ',') { jp.p++; continue; }
        if (*jp.p == '}') { jp.p++; break; }
        return false;
    }

    return true;
}
