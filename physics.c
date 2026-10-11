#define _POSIX_C_SOURCE 200809L


#include "physics.h"
#include "spikes.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>


static double posmod(double x, double y) {
    double res = fmod(x, y);
    return res < 0 ? res + y : res;
}

static int parse_mask_char(char c)
{
    return (c >= '0' && c <= '9') ? (c - '0') : 0;
}


static int  get_block_at(AppelPhysics *ap, double x, double y);
static void is_solid_at(AppelPhysics *ap, double x, double y, int dir);
static void active_block(AppelPhysics *ap, int dir, double tileX, double tileY);
static void set_flipped(AppelPhysics *ap, PlayerState *ps, int flipped);
static void resolve_collisions(AppelPhysics *ap, bool deep, PlayerState *ps);
static void move_player_x(AppelPhysics *ap, PlayerState *ps);
static void move_player_y(AppelPhysics *ap, PlayerState *ps);
static void can_get_up(AppelPhysics *ap, PlayerState *ps);
static void process_jump(AppelPhysics *ap, PlayerState *ps);
static void handle_player_up_down(AppelPhysics *ap, PlayerState *ps);
static void handle_player_left_right(AppelPhysics *ap, PlayerState *ps);
static void confirm_still_touching_wall(AppelPhysics *ap, PlayerState *ps);
static void position_now(AppelPhysics *ap, PlayerState *ps);
static void check_spike_dangers(AppelPhysics *ap, PlayerState *ps);


void appel_physics_init(AppelPhysics *ap,
                        int          *mapData,
                        double       *mapRotations,
                        void         *MAP_DATA,
                        int           LSX,
                        int           map_len,
                        void        (*setMap)(int, int, PlayerState *))
{
    static const char *maskStrings[] = {
        "          ", "          ", "5555   1h", "5555   1h",
        "5005   1h", "5000   1h", "5050   xh", "5225   3h",
        "       2 ", "2002   3 ", "5555   1h", "    02 4 ",
        "111100 2 ", "5055   1h", "5000   1 ", "****   5 ",
        "100100 2 ", "5555   1h", "5555   1h", "    04 4 ",
        "5555   1h", "5555   1h", "    00 2 ", "    80 6 ",
        "    81 6 ", "       5 ", "       5 ", "          ",
        "          ", "          ", "          ", "    06 4 ",
        "    82 4h", "5555   1h", "0660   6 ", "0660     ",
        "0660     ", "0660     ", "0660     ", "0660     ",
        "          ", "          ", "5665   6h", "5555    h",
        "5555   6h", "    08 4 ", "0600   6 ", "0600     ",
        "0600     ", "0600     ", "5775   6h", "5775   6h",
        "         ", "         ", "         ", "         ",
        "         ", "         ", "    09 4 ", "    84 4 ",
        "    60 7 ", "    10 4 ", "    11 4 ", "       5 ",
        "         ", "         ", "         ", "         ",
        "5555   1h", "    12 5 ", "    00 5 ", "    00 2 ",
        "6600   6 ", "100181 6 ", "2222   3h", "    13 4 ",
        "    00 5 ", "2      3h", "   2   3h", "1111   2h",
        "1111   5h", "5115   2h", "5111   2h", "2002   3h",
        "111100 2 ", "****00 5 ", "5555   1h"
    };

    static const int resolve_init[17] =
        {0,  0,1,  0,-1,  -1,0,  1,0,  -1,-1,  1,-1,  -1,1,  1,1};

    ap->MAP      = mapData;
    ap->MAP_R    = mapRotations;
    ap->MAP_DATA = MAP_DATA;
    ap->LSX      = LSX;
    ap->map_len  = map_len;
    ap->setMap   = setMap;

    for (int i = 0; i < NUM_TILES; i++) {
        const char *s = maskStrings[i];
        int len = (int)strlen(s);
        for (int j = 0; j < 4; j++) {
            ap->MASK[i][j] = (j < len) ? parse_mask_char(s[j]) : 0;
        }
    }

    ap->toverlap      = 0.0;
    ap->mask_char     = 0;
    ap->overlap       = 0.0;
    ap->temp_coords[0] = 0.0;
    ap->temp_coords[1] = 0.0;
    ap->spikes         = NULL;

    memcpy(ap->RESOLVE, resolve_init, sizeof(resolve_init));

}


static int get_block_at(AppelPhysics *ap, double x, double y)
{
    int sx = (int)floor(x / 60.0);
    int sy = (int)floor(y / 60.0);

    int idx = sx + sy * ap->LSX;

    if (idx < 0 || idx >= ap->map_len) {
        fprintf(stderr,
            "get_block_at: idx %d out of range [0,%d) for x=%.3f y=%.3f "
            "(matches a JS TypeError on out-of-bounds MAP/MASK access)\n",
            idx, ap->map_len, x, y);
        abort();
    }

    int tile = ap->MAP[idx];
    if (tile < 0 || tile >= NUM_TILES) {
        fprintf(stderr,
            "get_block_at: tile %d out of range [0,%d) at idx %d "
            "(matches a JS TypeError on out-of-bounds MASK access)\n",
            tile, NUM_TILES, idx);
        abort();
    }

    const int *mask = ap->MASK[tile];

    double rx = x - sx * 60.0;
    double ry = y - sy * 60.0;

    int mid = (rx < 30)
        ? (ry < 30 ? 1 : 2)
        : (ry < 30 ? 0 : 3);

    int rot = ((int)(mid - ap->MAP_R[idx])) & 3;
    return mask[rot];
}


static void is_solid_at(AppelPhysics *ap, double x, double y, int dir)
{
    ap->mask_char = get_block_at(ap, x, y);
    if (ap->mask_char < 5) return;

    double temp;
    if (dir == 1) {
        temp = posmod(x, 30.0);
    } else if (dir > 0) {
        temp = 30.0 - posmod(y, 30.0);
    } else if (dir == -1) {
        temp = 30.0 - posmod(x, 30.0);
    } else {
        temp = posmod(y, 30.0);
    }

    if (temp > ap->overlap) {
        ap->overlap = temp;
    }
}


static void active_block(AppelPhysics *ap, int dir, double tileX, double tileY)
{
    int tileIdx = (int)floor(tileX / 60.0) + (int)floor(tileY / 60.0) * ap->LSX;

    if (ap->mask_char != 7) return;

    int rot = ((((int)ap->MAP_R[tileIdx] + 1) % 4) + 4) % 4;
    int d   = ((dir % 4) + 4) % 4;
    if (rot != d) return;

    double temp = (ap->MAP[tileIdx] == 50) ? 5.0 : -5.0;

    if (dir == 2) {
        ap->friction_dx = temp;
        return;
    }
    if (dir == 1) {
        ap->friction_dy = temp;
        ap->friction    = 1.0;
        return;
    }
    if (dir == 0) {
        ap->friction_dx = -temp;
        return;
    }
    if (dir == -1) {
        ap->friction_dy = -temp;
        ap->friction    = 1.0;
        return;
    }
}


static void touching_wall_dx(AppelPhysics *ap, PlayerState *ps, int dir)
{
    if (ps->player_state == 1 || ps->is_falling < 3) return;

    ap->overlap = 0.0;
    is_solid_at(ap, ps->PLAYER_X + dir * -30.0, ps->PLAYER_Y + 8.0, dir);
    is_solid_at(ap, ps->PLAYER_X + dir * -30.0, ps->PLAYER_Y - 8.0, dir);

    if (ap->overlap == 0.0) {
        is_solid_at(ap, ps->PLAYER_X + dir *  4.0, ps->PLAYER_Y + 4.0, dir);
        if (ap->overlap > 0.0) {
            ap->overlap = 0.0;
            is_solid_at(ap, ps->PLAYER_X + dir * 4.0, ps->PLAYER_Y - 4.0, dir);

            if (ap->overlap > 0.0 && fabs(ps->PLAYER_SX) > 3.0) {
                if ((dir != ps_get_flipped(ps)) || ps->player_state == 3) {
                    ps->player_state = 1;
                    set_flipped(ap, ps, dir);
                }
                ps_set_wall(ps, dir);
            }
        }
    }
}


static void full_overlap(AppelPhysics *ap,
                         double x, double y, int dir, double dxy2)
{
    double tx = x;
    double ty = y;
    ap->toverlap = 0.0;

    for (int i = 0; i < 4; i++) {
        ap->overlap = 0.0;

        is_solid_at(ap, tx, ty, dir);
        if (dir & 1) {
            is_solid_at(ap, tx, ty + dxy2, dir);
        } else {
            is_solid_at(ap, tx + dxy2, ty, dir);
        }

        if (ap->overlap == 0.0) return;

        ap->overlap  += 0.01;
        ap->toverlap += ap->overlap;

        if (dir > 0) {
            if (dir == 1) { tx -= ap->overlap; }
            else          { ty += ap->overlap; }
        } else {
            if (dir == -1) { tx += ap->overlap; }
            else           { ty -= ap->overlap; }
        }
    }
}


static void set_flipped(AppelPhysics *ap, PlayerState *ps, int flipped)
{
    (void)ap;

    int  temp     = (flipped & 3) + 1;
    bool isCrouch = (ps->player_state == 2);
    bool isOnWall = (ps_get_wall(ps) != WALL_NONE);

    ps_set_psz(ps, temp, (ps_get_wall(ps) == WALL_NONE && isCrouch) ? 7 : 17);
    temp = (temp & 3) + 1;
    ps_set_psz(ps, temp, 13);
    temp = (temp & 3) + 1;
    ps_set_psz(ps, temp, (isOnWall && isCrouch) ? 7 : 17);
    temp = (temp & 3) + 1;
    ps_set_psz(ps, temp, 13);

    ps_set_flipped(ps, flipped);
}


static void set_flipped_safe(AppelPhysics *ap, PlayerState *ps, int dir)
{
    set_flipped(ap, ps, dir);

    bool   safe_found = false;
    int    safe       = 0;
    double safed      = 64.0;

    full_overlap(ap,
                 ps->PLAYER_X - ps_get_psz(ps, 4),
                 ps->PLAYER_Y + ps_get_psz(ps, 1),
                 0, ps_get_psz(ps, 4) + ps_get_psz(ps, 2));
    if (ap->toverlap > 0.0 && ap->toverlap < safed) {
        safe_found = true; safe = 0; safed = ap->toverlap;
    }

    full_overlap(ap,
                 ps->PLAYER_X - ps_get_psz(ps, 4),
                 ps->PLAYER_Y - ps_get_psz(ps, 3),
                 2, ps_get_psz(ps, 4) + ps_get_psz(ps, 2));
    if (ap->toverlap > 0.0 && ap->toverlap < safed) {
        safe_found = true; safe = 2; safed = ap->toverlap;
    }

    full_overlap(ap,
                 ps->PLAYER_X - ps_get_psz(ps, 4),
                 ps->PLAYER_Y - ps_get_psz(ps, 3),
                 -1, ps_get_psz(ps, 1) + ps_get_psz(ps, 3));
    if (ap->toverlap > 0.0 && ap->toverlap < safed) {
        safe_found = true; safe = -1; safed = ap->toverlap;
    }

    full_overlap(ap,
                 ps->PLAYER_X + ps_get_psz(ps, 2),
                 ps->PLAYER_Y - ps_get_psz(ps, 3),
                 1, ps_get_psz(ps, 1) + ps_get_psz(ps, 3));
    if (ap->toverlap > 0.0 && ap->toverlap < safed) {
        safe_found = true; safe = 1; safed = ap->toverlap;
    }

    if (safe_found) {
        if (safe > 0) {
            if (safe > 1) { ps->PLAYER_Y += safed; }
            else          { ps->PLAYER_X -= safed; }
        } else {
            if (safe > -1) { ps->PLAYER_Y -= safed; }
            else           { ps->PLAYER_X += safed; }
        }
        resolve_collisions(ap, false, ps);
    }
}


static void make_upright(AppelPhysics *ap, PlayerState *ps)
{
    if (ps_get_flipped(ps) == 0) return;
    ps->player_state = 1;
    set_flipped_safe(ap, ps, 0);
    ap->overlap = 0.0;
}


static void check_square(AppelPhysics *ap, double x, double y, PlayerState *ps)
{
    double left_x   = x - ps_get_psz(ps, 4);
    double right_x  = x + ps_get_psz(ps, 2);
    double top_y    = y - ps_get_psz(ps, 3);
    double bottom_y = y + ps_get_psz(ps, 1);

    if (get_block_at(ap, left_x,  top_y) >= 5 ||
        get_block_at(ap, right_x, top_y) >= 5) {
        return;
    }

    ap->overlap = 0.0;
    is_solid_at(ap, left_x,  bottom_y, 0); if (ap->overlap != 0.0) return;
    is_solid_at(ap, right_x, bottom_y, 0); if (ap->overlap != 0.0) return;

    if (get_block_at(ap, left_x,  y) >= 5 ||
        get_block_at(ap, right_x, y) >= 5) {
        return;
    }

    ap->mask_char = -1;
}


static void resolve(AppelPhysics *ap,
                    double x, double y, int deep, PlayerState *ps)
{
    const int *res = ap->RESOLVE;

    for (int i2 = 1; i2 <= deep; i2++) {
        for (int i3 = 1; i3 < 16; i3 += 2) {
            ps->PLAYER_X = x + res[i3]     * (double)i2;
            ps->PLAYER_Y = y + res[i3 + 1] * (double)i2;

            check_square(ap, ps->PLAYER_X, ps->PLAYER_Y, ps);
            if (ap->mask_char == -1) return;
        }
    }

    ps->PLAYER_X    = x;
    ps->PLAYER_Y    = y;
    ps->PLAYER_DEATH = true;
}


static void resolve_collisions(AppelPhysics *ap, bool deep, PlayerState *ps)
{
    check_square(ap, ps->PLAYER_X, ps->PLAYER_Y, ps);
    if (ap->mask_char == -1) return;

    resolve(ap, ps->PLAYER_X, ps->PLAYER_Y, deep ? 10 : 4, ps);
}


static void touching_wall_dy(AppelPhysics *ap, PlayerState *ps, int dy)
{
    ps->PLAYER_SY = 0.0;

    if (dy == 1 && ps->KEY_UP > 0) {
        ps->PLAYER_SY = 4.0;
        if (ap->friction_dy > 0.0) ps->PLAYER_SY += ap->friction_dy;
        ps->is_falling  = 0;
        ps_set_wall(ps, 0);
        set_flipped(ap, ps, 0);
    } else {
        if (dy == -1 && ap->friction_dy < 0.0) {
            ps->PLAYER_SY += ap->friction_dy;
        }
    }
}


static void move_player_x(AppelPhysics *ap, PlayerState *ps)
{    
    double rem_x = ps->PLAYER_X;
    int    dir   = (ps->PLAYER_SX > 0) ? 1 : -1;

    if (ps->PLAYER_SX > 0) {
        ps->PLAYER_X += ps->PLAYER_SX + ps_get_psz(ps, 2);
    } else {
        ps->PLAYER_X += ps->PLAYER_SX - ps_get_psz(ps, 4);
    }

    ap->overlap = 0.0;
    is_solid_at(ap, ps->PLAYER_X, ps->PLAYER_Y + ps_get_psz(ps, 1), dir);
    active_block(ap, dir, ps->PLAYER_X, ps->PLAYER_Y + ps_get_psz(ps, 1));
    is_solid_at(ap, ps->PLAYER_X, ps->PLAYER_Y,               dir);
    is_solid_at(ap, ps->PLAYER_X, ps->PLAYER_Y - ps_get_psz(ps, 3), dir);
    active_block(ap, dir, ps->PLAYER_X, ps->PLAYER_Y - ps_get_psz(ps, 3));

    if (ap->overlap > 0.0) {
        if (ap->overlap > fabs(ps->PLAYER_SX) + 4.0) {
            double saved_x    = ps->PLAYER_X;
            ps->PLAYER_X      = rem_x + ps->PLAYER_SX;
            resolve_collisions(ap, false, ps);

            if (ps->PLAYER_DEATH) {
                ps->PLAYER_X   = saved_x;
                ps->PLAYER_DEATH = true;
            } else {
                return;
            }
        }

        if (ps->PLAYER_SX > 0) {
            ps->PLAYER_X -= 0.01 + ap->overlap;
            touching_wall_dx(ap, ps, 1);
        } else {
            ps->PLAYER_X += 0.01 + ap->overlap;
            touching_wall_dx(ap, ps, -1);
        }

        ap->overlap = 0.0;
        is_solid_at(ap, ps->PLAYER_X, ps->PLAYER_Y + ps_get_psz(ps, 1), dir);
        is_solid_at(ap, ps->PLAYER_X, ps->PLAYER_Y,               dir);
        is_solid_at(ap, ps->PLAYER_X, ps->PLAYER_Y - ps_get_psz(ps, 3), dir);

        if (ap->overlap > 0.0) ps->PLAYER_DEATH = true;

        ps->PLAYER_SX *= 0.5;
    }

    ps->PLAYER_X += (ps->PLAYER_SX > 0)
        ? -(double)ps_get_psz(ps, 2)
        :  (double)ps_get_psz(ps, 4);

    if (ps->PLAYER_X < 14.0) {
        ps->PLAYER_X  = 14.0;
        ps->PLAYER_SX = 0.0;
    }
}


static void move_player_y(AppelPhysics *ap, PlayerState *ps)
{
    double saved_dy = ps->PLAYER_SY;
    int    dir      = (ps->PLAYER_SY > 0) ? 0 : 2;

    if (ps->PLAYER_SY > 0) {
        ps->PLAYER_Y += ps->PLAYER_SY + ps_get_psz(ps, 1);
    } else {
        ps->PLAYER_Y += ps->PLAYER_SY - ps_get_psz(ps, 3);
    }

    ap->overlap     = 0.0;
    ap->friction    = 0.0;
    ap->friction_dx = 0.0;
    ap->friction_dy = 0.0;

    is_solid_at(ap, ps->PLAYER_X + ps_get_psz(ps, 2), ps->PLAYER_Y, dir);
    active_block(ap, dir, ps->PLAYER_X + ps_get_psz(ps, 2), ps->PLAYER_Y);
    is_solid_at(ap, ps->PLAYER_X - ps_get_psz(ps, 4), ps->PLAYER_Y, dir);
    active_block(ap, dir, ps->PLAYER_X - ps_get_psz(ps, 4), ps->PLAYER_Y);

    if (ap->overlap > 0.0) {
        double overlapAdjust = 0.01 + ap->overlap;

        if (ps->PLAYER_SY > 0) {
            ps->PLAYER_Y -= overlapAdjust;
            touching_wall_dy(ap, ps, 1);

            if (ps->player_state == 1 || ps->player_state == 3) {
                ps->player_state = 0;
            }
        } else {
            ps->PLAYER_Y += overlapAdjust;
            touching_wall_dy(ap, ps, -1);

            ps->is_falling  = 0;
            ps->is_jumping  = 0;
            ps_set_wall(ps, WALL_NONE);

            if (ps->player_state == 3) {
                ps->player_state = 0;
            }
        }
    }

    ps->PLAYER_Y -= (saved_dy > 0)
        ?  (double)ps_get_psz(ps, 1)
        : -(double)ps_get_psz(ps, 3);
}


static void can_get_up(AppelPhysics *ap, PlayerState *ps)
{
    ap->overlap = 0.0;
    double px   = ps->PLAYER_X;
    double py   = ps->PLAYER_Y;

    if (ps_get_flipped(ps) == 0) {
        if (ps_get_wall(ps) == 0) {
            double checkY = (py - ps_get_psz(ps, 3)) - 8.0;
            is_solid_at(ap, px - ps_get_psz(ps, 4), checkY, 0);
            is_solid_at(ap, px + ps_get_psz(ps, 2), checkY, 0);
        } else {
            double checkY = py + ps_get_psz(ps, 1) + 8.0;
            is_solid_at(ap, px - ps_get_psz(ps, 4), checkY, 0);
            is_solid_at(ap, px + ps_get_psz(ps, 2), checkY, 0);
        }
    } else if (ps_get_flipped(ps) == 1) {
        double checkX = px - ps_get_psz(ps, 4) - 8.0;
        is_solid_at(ap, checkX, py + ps_get_psz(ps, 1),  1);
        is_solid_at(ap, checkX, py - ps_get_psz(ps, 3),  1);
    } else if (ps_get_flipped(ps) == -1) {
        double checkX = px + ps_get_psz(ps, 2) + 8.0;
        is_solid_at(ap, checkX, py + ps_get_psz(ps, 1), -1);
        is_solid_at(ap, checkX, py - ps_get_psz(ps, 3), -1);
    }
}


static void position_now(AppelPhysics *ap, PlayerState *ps)
{
    (void)ap;

    double direction = ps_get_direction(ps);

    if (ps->player_state == 3) {
        direction += 22.5 * ps_get_flipped(ps);
        direction  = posmod(direction, 360.0);
        if (direction > 180.0) direction -= 360.0;
        ps_set_direction(ps, direction);
        return;
    }

    if (ps->player_state == 1) {
        double targetDir = (ps_get_flipped(ps) + 1) * 90.0;
        double temp      = fmod((targetDir - direction + 180.0), 360.0) - 180.0;

        if (fabs(temp) < 22.5) {
            ps->player_state = 0;
        } else {
            bool decrement = (temp < 0.0) && !(direction == 0.0);
            double step = decrement ? -30.0 : 30.0;
            direction += step;
            direction  = posmod(direction, 360.0);
            if (direction > 180.0) direction -= 360.0;
            ps_set_direction(ps, direction);
            return;
        }
    }

    direction = (ps_get_flipped(ps) + 1) * 90.0;
    if (direction > 180.0) direction -= 360.0;
    ps_set_direction(ps, direction);
}


static void confirm_still_touching_wall(AppelPhysics *ap, PlayerState *ps)
{
    double px   = ps->PLAYER_X;
    double py   = ps->PLAYER_Y;
    int    wall = ps_get_wall(ps);

    ap->overlap = 0.0;

    if (wall == 0) {
        is_solid_at(ap, px, py + ps_get_psz(ps, 1) + 1.0, 0);
    } else if (wall == 1) {
        double checkX = px + ps_get_psz(ps, 2) + 1.0;
        is_solid_at(ap, checkX, py,                  0);
        active_block(ap, wall, checkX, py);
        is_solid_at(ap, checkX, py - ps_get_psz(ps, 3),    0);
        active_block(ap, wall, checkX, py - ps_get_psz(ps, 3));
    } else if (wall == -1) {
        is_solid_at(ap, px - ps_get_psz(ps, 4) - 1.0, py, 0);
        active_block(ap, wall, px - ps_get_psz(ps, 4) - 1.0, py);
    }

    if (ap->overlap == 0.0) {
        if (ps->player_state == 2) {
            if      (wall ==  1) ps->PLAYER_X += 10.0;
            else if (wall == -1) ps->PLAYER_X -= 10.0;
            else if (wall ==  0) ps->PLAYER_Y += 10.0;
        }
        ps_set_wall(ps, WALL_NONE);
        set_flipped(ap, ps, ps_get_flipped(ps));
    }
}


static void start_wall_jump(AppelPhysics *ap, PlayerState *ps)
{
    ps->is_jumping   = 101;
    ps->player_state = 3;

    int playerWall = ps_get_wall(ps);
    set_flipped(ap, ps, -playerWall);

    ps->PLAYER_SY  = 20.0;
    ps->PLAYER_SX  = -10.0 * playerWall;
    ps_set_wall(ps, WALL_NONE);
    ps->is_falling = 10;
    ps->KEY_UP     = 2;
}


static void process_jump(AppelPhysics *ap, PlayerState *ps)
{
    int KEY_UP = ps->KEY_UP;

    if (KEY_UP == 1) {
        int playerWall = ps_get_wall(ps);

        if (playerWall != WALL_NONE &&
            (playerWall == 1 || playerWall == -1)) {
            start_wall_jump(ap, ps);
        } else {
            int is_jumping = ps->is_jumping;
            if (is_jumping != 100) {
                int is_falling = ps->is_falling;
                int flipped    = ps_get_flipped(ps);

                if (is_falling < 3 || (flipped == 1 && is_falling < 5)) {
                    ap->overlap = 0.0;
                    double px      = ps->PLAYER_X;
                    double py      = ps->PLAYER_Y;
                    double check_y = py + 15.0;

                    is_solid_at(ap, px - ps_get_psz(ps, 4), check_y, -99);
                    is_solid_at(ap, px + ps_get_psz(ps, 2), check_y, -99);

                    if (ap->overlap == 0.0 && ps->PLAYER_SY < 16.0) {
                        ps->KEY_UP     = 2;
                        ps->is_jumping = is_jumping + 1;
                        ps->PLAYER_SY  = 16.0;
                    }
                }
            }
        }
    }

    {
        int is_jumping = ps->is_jumping;
        if (is_jumping > 0 && is_jumping < 5) {
            if (ps->PLAYER_SY < 16.0) ps->PLAYER_SY = 16.0;
            ps->is_jumping = is_jumping + 1;
            ps->KEY_UP     = 2;
        }
    }
}


static void handle_player_up_down(AppelPhysics *ap, PlayerState *ps)
{
    ps->PLAYER_SY -= 1.7;
    if (ps->PLAYER_SY < -30.0) ps->PLAYER_SY = -30.0;

    int playerWall = ps_get_wall(ps);
    if ((playerWall == 1 || playerWall == -1) &&
        (ps->PLAYER_SY < 0.0 || ap->friction != 0.0))
    {
        if (ap->friction != 0.0) ps->PLAYER_SY += 1.7;
        ps->PLAYER_SY += (ap->friction_dy - ps->PLAYER_SY) * 0.3;
    }

    if (ps->KEY_UP > 0) {
        process_jump(ap, ps);
    } else {
        int is_jumping = ps->is_jumping;
        if (is_jumping > 0 && is_jumping < 100) ps->is_jumping = 100;

        if (ps->PLAYER_SY > 0.0) {
            if (ps->is_falling > 1 && ps->player_state == 0) {
                ps->PLAYER_SY -= 1.0;
            }
        } else {
            if (ps->player_state == 4) ps->player_state = 0;
        }
    }

    int playerStateVal = ps->player_state;
    if (ps->KEY_DOWN > 0) {
        if (playerStateVal != 1 && playerStateVal != 3) {
            ps->player_state = 2;
            set_flipped(ap, ps, ps_get_flipped(ps));
            resolve_collisions(ap, false, ps);
        }
    } else if (playerStateVal == 2) {
        can_get_up(ap, ps);
        if (ap->overlap == 0.0) {
            ps->player_state = 0;
            set_flipped(ap, ps, ps_get_flipped(ps));
            resolve_collisions(ap, false, ps);
        }
    }

    if (ps->is_falling < 255) ps->is_falling += 1;
    move_player_y(ap, ps);
}


static void handle_player_left_right(AppelPhysics *ap, PlayerState *ps)
{
    double maxSpeed = (ps->player_state == 2) ? 1.2 : 2.5;

    {
        if (!(ps->player_state == 3 && ps->is_falling < 25)) {

            if (ps->player_state == 3 && ps->is_falling < 35) {
                maxSpeed *= (ps->is_falling - 25) * 0.1;
            }

            int playerWall = ps_get_wall(ps);
            if (playerWall != WALL_NONE) {
                if (playerWall != 0) {
                    if (ps->KEY_LEFT == 1) {
                        ps->PLAYER_SX -= maxSpeed;
                        ps->KEY_LEFT   = 2;
                    }
                    if (ps->KEY_RIGHT == 1) {
                        ps->PLAYER_SX += maxSpeed;
                        ps->KEY_RIGHT  = 2;
                    }
                }
            } else {
                if (ps->KEY_LEFT > 0) {
                    ps->PLAYER_SX -= maxSpeed;
                    ps->KEY_LEFT   = 2;
                }
                if (ps->KEY_RIGHT > 0) {
                    ps->PLAYER_SX += maxSpeed;
                    ps->KEY_RIGHT  = 2;
                }
            }

            if (ps->player_state == 3 && ps->is_falling < 30) {
                ps->PLAYER_SX += ap->friction_dx
                    - ps->PLAYER_SX * 0.02 * (ps->is_falling - 20);
            } else {
                double absSx = fabs(ap->friction_dx - ps->PLAYER_SX);
                if (absSx < 1.0) {
                    ps->PLAYER_SX = ap->friction_dx;
                } else if (absSx > 2.0 ||
                           ps->KEY_RIGHT > 0 ||
                           ps->KEY_LEFT  > 0) {
                    ps->PLAYER_SX += (ap->friction_dx - ps->PLAYER_SX) * 0.2;
                } else {
                    ps->PLAYER_SX = ap->friction_dx;
                }
            }
        }
    }

    move_player_x(ap, ps);
}


bool appel_is_flag_at(AppelPhysics *ap, double x, double y)
{
    int ix  = (int)floor(x);
    int iy  = (int)floor(y);
    int tx  = ix / 60;
    int ty  = iy / 60;
    int idx = tx + ty * ap->LSX;
    return ap->MAP[idx] == 63;
}


static void check_spike_dangers(AppelPhysics *ap, PlayerState *ps)
{
    bool isInSpikeTile =
        get_block_at(ap, ps->PLAYER_X + ps_get_psz(ps, 2) - 1, ps->PLAYER_Y) == 2 ||
        get_block_at(ap, ps->PLAYER_X - (ps_get_psz(ps, 4) - 1), ps->PLAYER_Y) == 2 ||
        get_block_at(ap, ps->PLAYER_X, ps->PLAYER_Y + ps_get_psz(ps, 1) - 1) == 2 ||
        get_block_at(ap, ps->PLAYER_X, ps->PLAYER_Y - (ps_get_psz(ps, 3) - 1)) == 2;

    if (ps->wasInSpikeTileLastFrame) {
        if (ap->spikes &&
            is_player_touching_spike((SpikeData *)ap->spikes, ap, ps)) {
            ps->PLAYER_DEATH = true;
        }
    }

    ps->wasInSpikeTileLastFrame = isInSpikeTile;
}


int appel_key_code(const char *keys)
{
    int sum = 0;
    for (; *keys; keys++) {
        switch (*keys) {
            case 'D': sum += 1; break;
            case 'A': sum += 2; break;
            case 'S': sum += 4; break;
            case 'W': sum += 8; break;
        }
    }
    return sum;
}


void appel_tick(AppelPhysics *ap, PlayerState *ps, int inputKeys) {    
    if (inputKeys & 8) { if (ps->KEY_UP    < 1) ps->KEY_UP    = 1; }
    else                 ps->KEY_UP    = 0;

    ps->KEY_DOWN = (inputKeys & 4) ? 1 : 0;

    if (inputKeys & 2) { if (ps->KEY_LEFT  < 1) ps->KEY_LEFT  = 1; }
    else                 ps->KEY_LEFT  = 0;

    if (inputKeys & 1) { if (ps->KEY_RIGHT < 1) ps->KEY_RIGHT = 1; }
    else                 ps->KEY_RIGHT = 0;

    resolve_collisions(ap, false, ps);
    check_spike_dangers(ap, ps);
    handle_player_up_down(ap, ps);
    handle_player_left_right(ap, ps);

    int playerWall = ps_get_wall(ps);
    if (playerWall != WALL_NONE) {
        ps->is_falling = (playerWall == 0) ? 10 : 0;
        confirm_still_touching_wall(ap, ps);
    }

    int playerStateVal = ps->player_state;
    if (playerStateVal != 1 && playerStateVal != 3) {
        if (ps_get_flipped(ps) != 0 && ps_get_wall(ps) == WALL_NONE) {
            make_upright(ap, ps);
        }
    }

    position_now(ap, ps);
    
    if (ps->PLAYER_Y < -20.0) ps->PLAYER_DEATH = true;
}


PlayerState appel_create_default_player_state(AppelPhysics *ap,
                                              double        x,
                                              double        y,
                                              int           map_len)
{
    PlayerState ps;
    memset(&ps, 0, sizeof(ps));

    ps_set_psz(&ps, 1, 17); ps_set_psz(&ps, 2, 13);
    ps_set_psz(&ps, 3, 17); ps_set_psz(&ps, 4, 13);
    ps.is_falling   = 255;
    ps_set_wall(&ps, WALL_NONE);

    ps_set_direction(&ps, 90.0);

    int spawnIDX = -1;
    for (int i = 0; i < map_len; i++) {
        if (ap->MAP[i] == 76) { spawnIDX = i; break; }
    }

    if (spawnIDX > 0) {
        ps.PLAYER_X = (double)((spawnIDX % ap->LSX) * 60) + 30.0;
        ps.PLAYER_Y = (double)(spawnIDX / ap->LSX) * 60.0 + 17.0;
    } else {
        double newY = y;
        while (get_block_at(ap, x, newY - 30.0) > 4) newY += 30.0;
        ps.PLAYER_X = x;
        ps.PLAYER_Y = newY;
    }

    return ps;
}


static const char REPLAY_DELIM[] = "\xC7\x87";

static char **split_string(const char *src, const char *delim, int *count)
{
    *count = 0;
    int   cap   = 64;
    char **parts = malloc(cap * sizeof(char *));
    if (!parts) return NULL;

    size_t dlen = strlen(delim);
    const char *p = src;

    while (1) {
        const char *found = strstr(p, delim);
        size_t      seglen = found ? (size_t)(found - p) : strlen(p);
        char *seg = malloc(seglen + 1);
        if (!seg) {  break; }
        memcpy(seg, p, seglen);
        seg[seglen] = '\0';

        if (*count >= cap) {
            cap *= 2;
            char **tmp = realloc(parts, cap * sizeof(char *));
            if (!tmp) { free(seg); break; }
            parts = tmp;
        }
        parts[(*count)++] = seg;

        if (!found) break;
        p = found + dlen;
    }
    return parts;
}

static void free_string_array(char **arr, int count)
{
    for (int i = 0; i < count; i++) free(arr[i]);
    free(arr);
}

ReplayResult appel_decode_replay(const char *replayCode)
{
    ReplayResult bad = { NULL, 0 };

    size_t len = strlen(replayCode);
    if (len < 8) return bad;
    const char *body = replayCode + 8;

    int    raw_count = 0;
    char **raw       = split_string(body, REPLAY_DELIM, &raw_count);
    if (!raw) return bad;

    if (raw_count <= 4) { free_string_array(raw, raw_count); return bad; }
    char **data      = raw + 4;
    int    data_count = raw_count - 4;

    int inputCount = atoi(data[data_count - 1]) - 1;
    if (inputCount < 0) {
        free_string_array(raw, raw_count);
        return bad;
    }

    char **inputs = calloc(inputCount, sizeof(char *));
    if (!inputs) { free_string_array(raw, raw_count); return bad; }
    for (int i = 0; i < inputCount; i++) {
        inputs[i] = malloc(8);
        if (inputs[i]) inputs[i][0] = '\0';
    }

    char keyCombos[16][8];
    const char keyChars[4]    = {'D','A','S','W'};
    const int  keyBits[4]     = {  1,  2,  4,  8};
    for (int i = 0; i < 16; i++) {
        int pos = 0;
        for (int k = 0; k < 4; k++) {
            if (i & keyBits[k]) keyCombos[i][pos++] = keyChars[k];
        }
        keyCombos[i][pos] = '\0';
    }

    for (int i = 0; i + 1 < data_count; i += 2) {
        int start    = atoi(data[i]) - 1;
        int keyValue = atoi(data[i + 1]);

        const char *keys = (keyValue >= 0 && keyValue < 16)
            ? keyCombos[keyValue] : "";

        int nextStart;
        if (i + 2 < data_count) {
            nextStart = atoi(data[i + 2]) - 1;
        } else {
            nextStart = INT_MAX;
        }

        int end;
        if (nextStart == INT_MAX - 1 || i + 2 >= data_count) {
            end = start + 100;
        } else {
            end = nextStart < inputCount ? nextStart : inputCount;
        }
        if (end > inputCount) end = inputCount;

        for (int j = start; j < end && j >= 0; j++) {
            if (inputs[j]) {
                free(inputs[j]);
                inputs[j] = strdup(keys);
            }
        }
    }

    free_string_array(raw, raw_count);

    ReplayResult result = { inputs, inputCount };
    return result;
}

void appel_free_replay(ReplayResult *r)
{
    if (!r) return;
    for (int i = 0; i < r->count; i++) free(r->inputs[i]);
    free(r->inputs);
    r->inputs = NULL;
    r->count  = 0;
}