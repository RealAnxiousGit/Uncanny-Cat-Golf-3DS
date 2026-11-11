// Gameplay for one level. See game.h for where each part comes from.
#include "game.h"

#include <string.h>

#define HUE_WEAKEST 0.69f
#define HUE_STRONGEST 0.0f
#define GOAL_RADIUS 16.0f
#define COIN_RADIUS 11.1803f
#define GHOST_HITBOX 12.1655f
#define GHOST_GRAZE 24.0f
#define GHOST_ACCEL 0.35f
#define GHOST_LIMIT 40.0f

static void transition(Game *g, int st);
static void state_update(Game *g, const GameInput *in);

void game_start(Game *g, const LevelDef *lv, int mode, const GameSettings *set) {
    LevelDef keep = *lv;
    int ndemo = g->ndemo;
    DemoShot demo[MAX_DEMO_SHOTS];
    memcpy(demo, g->demo, sizeof(demo));
    memset(g, 0, sizeof(*g));
    g->ndemo = ndemo;
    memcpy(g->demo, demo, sizeof(demo));
    g->lv = keep;
    g->mode = mode;
    g->set = *set;
    g->par = lv->par;
    g->bump_pitch = 1.0f;
    g->launch_pitch = 1.0f;
    g->sprite_visible = true;
    g->buffered_frame = 0xFFFF0000u;
    g->frame = 100000; // Godot counts physics frames since start-up, never near 0
    for (int i = 0; i < lv->nobjs; i++) {
        const ObjDef *o = &lv->objs[i];
        switch (o->type) {
        case OBJ_PLAYER:
            g->pos = o->pos;
            break;
        case OBJ_GOAL:
            g->has_goal = true;
            g->goal_pos = o->pos;
            g->goal_r = GOAL_RADIUS * o->sx;
            break;
        case OBJ_GHOST:
            if (g->nghosts < MAX_GHOSTS) {
                g->ghosts[g->nghosts].pos = o->pos;
                g->ghosts[g->nghosts].alive = true;
                g->nghosts++;
            }
            break;
        case OBJ_COIN:
            if (g->ncoins < MAX_COINS) {
                g->coins[g->ncoins].pos = o->pos;
                g->coins[g->ncoins].r = COIN_RADIUS * o->sx;
                g->coins[g->ncoins].score = (int)obj_param(o, "score_value", 0);
                g->coins[g->ncoins].mult = obj_param(o, "coin_multiply", 0) != 0;
                g->coins[g->ncoins].multval = (int)obj_param(o, "coin_multiply_value", 0);
                g->ncoins++;
            }
            break;
        }
    }
    g->thought = TH_IDLE;
    // PSpawn.enter
    g->pstate = PS_SPAWN;
    g->sprite_scale = 0.003f;
    g->spawn_first_frame = true;
    g->events |= EV_JOIN;
}

static float hue_to_rgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

static void from_hsv(float h, float s, float v, float a, float out[4]) {
    h = h - floorf(h);
    int i = (int)(h * 6);
    float f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    float r, gg, b;
    switch (i % 6) {
    case 0: r = v; gg = t; b = p; break;
    case 1: r = q; gg = v; b = p; break;
    case 2: r = p; gg = v; b = t; break;
    case 3: r = p; gg = q; b = v; break;
    case 4: r = t; gg = p; b = v; break;
    default: r = v; gg = p; b = q; break;
    }
    out[0] = r; out[1] = gg; out[2] = b; out[3] = a;
    (void)hue_to_rgb;
}

void game_aim_color(const Game *g, float out[4]) {
    float len = v2len(g->hit);
    if (len < DRAG_CANCEL) {
        from_hsv(0, 1, 0.5f, 1, out);
        return;
    }
    float hue = (len - DRAG_CANCEL) / (MAX_DRAG - DRAG_CANCEL);
    from_hsv(lerpf(HUE_WEAKEST, HUE_STRONGEST, hue), 0.68f, 1, 0.5f, out);
}

static void friction(Game *g) {
    float l = v2len(g->vel);
    if (l <= 0) return;
    float w = MAX_DRAG * powf(FRIC_COEFF / l, FRIC_POW);
    g->vel = v2add(g->vel, v2mul(v2sub(v2(0, 0), g->vel), w));
}

static vec2 drag_vector(const Game *g, vec2 mouse) {
    vec2 h = v2mul(v2sub(g->click_pos, mouse), g->set.power_sensitivity);
    if (g->set.invert_shot) h = v2mul(h, -1);
    return v2limit(h, MAX_DRAG);
}

static void set_launch_pitch(Game *g) {
    float power = v2len(g->vel);
    float w = (power - LAUNCH_POWER_MIN) / (LAUNCH_POWER_MAX - LAUNCH_POWER_MIN);
    g->launch_pitch = lerpf(0.9f, 1.1f, w);
}

// ---------------------------------------------------------------- level events
static void death(Game *g, int type) {
    g->active = false;
    g->lose = true;
    g->death_type = type;
    transition(g, PS_LOSE);
    g->sprite_visible = false;
    if (type == DEATH_WALL) g->events |= EV_DEATH_WALL;
    else if (type == DEATH_BACKROOMS) g->events |= EV_BACKROOMS;
    else if (type == DEATH_OB) g->events |= EV_OB;
    else g->events |= EV_CAUGHT;
    g->thought = type == DEATH_BACKROOMS ? TH_BACKROOMS : TH_DEATH;
}

static void win(Game *g) {
    g->active = false;
    g->victory = true;
    for (int i = 0; i < g->nghosts; i++) g->ghosts[i].alive = false;
    transition(g, PS_WIN);
    g->thought = TH_WIN;
    g->events |= EV_WIN;
    if (g->mode == MODE_DEMO) return;
    int sb = 5000 + 500 * (g->par - g->strokes);
    g->stroke_bonus = sb > 0 ? sb : 0;
    float tb = 2500.0f - 100.0f * (g->time - g->lv.on_time);
    g->time_bonus = tb > 0 ? (int)tb : 0;
    g->final_score = g->stroke_bonus + g->time_bonus + g->level_bonus;
    g->rank = 0;
    while (g->rank < 5 && g->final_score >= g->lv.ranks[g->rank + 1]) g->rank++;
}

// ---------------------------------------------------------------- player states
static void state_enter(Game *g, const GameInput *in) {
    switch (g->pstate) {
    case PS_IDLE:
        g->events |= EV_PICKUP;
        g->hit_visible = false;
        if (in->click_down) {
            g->events |= EV_GRAB;
        } else if (g->frame - g->buffered_frame < SHOT_BUFFER_FRAMES) {
            g->buffered_frame = 0xFFFF0000u;
            vec2 old = g->vel;
            g->vel = v2mul(g->buffered_shot, DRAG_MULTIPLIER);
            if (v2len(g->vel) < LAUNCH_POWER_MIN) {
                g->vel = old;
                g->events |= EV_CANCEL;
                return;
            }
            g->strokes++;
            set_launch_pitch(g);
            transition(g, PS_MOVING);
            g->events |= EV_LAUNCH;
            g->thought = TH_MOVING;
        }
        break;
    case PS_MOVING:
        g->coin_multiplier = 0;
        g->burst_buffer = 0;
        g->moving_first_frame = true;
        break;
    case PS_WIN:
        g->hit_visible = false;
        g->vel = v2(0, 0);
        break;
    case PS_LOSE:
        g->vel = v2(0, 0);
        g->add_vel = v2(0, 0);
        break;
    case PS_ATTRACT:
        g->events |= EV_PICKUP;
        g->demo_timer = 0;
        break;
    }
}

static const GameInput *cur_in;

static void transition(Game *g, int st) {
    g->pstate = st;
    state_enter(g, cur_in);
    if (g->pstate == st) state_update(g, cur_in);
}

static void state_update(Game *g, const GameInput *in) {
    switch (g->pstate) {
    case PS_SPAWN:
        g->sprite_scale += 0.001f;
        if (g->sprite_scale >= 0.064f) {
            g->sprite_scale = 0.064f;
            g->active = true;
            transition(g, g->mode == MODE_DEMO ? PS_ATTRACT : PS_IDLE);
            return;
        }
        if (g->set.buffer_shot && in->click_released && !g->spawn_first_frame) {
            g->buffered_shot = drag_vector(g, in->mouse);
            g->buffered_frame = g->frame;
        }
        g->spawn_first_frame = false;
        break;

    case PS_IDLE: {
        g->vel = v2add(g->vel, g->add_vel);
        if ((g->vel.x != 0 || g->vel.y != 0) && g->add_vel.x == 0 && g->add_vel.y == 0) friction(g);
        if (in->click_pressed) g->events |= EV_GRAB;
        g->hit = drag_vector(g, in->mouse);
        if (in->click_down) g->hit_visible = true;
        if (in->click_released) {
            g->hit_visible = false;
            vec2 old = g->vel;
            g->vel = v2mul(g->hit, DRAG_MULTIPLIER);
            if (v2len(g->vel) < LAUNCH_POWER_MIN) {
                g->vel = old;
                g->events |= EV_CANCEL;
                return;
            }
            g->strokes++;
            set_launch_pitch(g);
            transition(g, PS_MOVING);
            g->events |= EV_LAUNCH;
            g->thought = TH_MOVING;
            return;
        }
        break;
    }

    case PS_MOVING: {
        g->vel = v2add(g->vel, g->add_vel);
        bool no_add = g->add_vel.x == 0 && g->add_vel.y == 0;
        if (v2len(g->vel) <= STOP_SPEED && no_add) {
            g->vel = v2(0, 0);
            transition(g, g->mode == MODE_DEMO ? PS_ATTRACT : PS_IDLE);
            return;
        }
        if (v2len(g->vel) - v2len(g->add_vel) == 0) return;
        friction(g);
        if (g->set.buffer_shot && in->click_released && !g->moving_first_frame) {
            g->buffered_shot = drag_vector(g, in->mouse);
            g->buffered_frame = g->frame;
        }
        g->moving_first_frame = false;
        g->burst_buffer++;
        break;
    }

    case PS_WIN:
        g->pos = v2add(g->pos, v2mul(v2sub(g->goal_target, g->pos), 0.05f));
        if (g->sprite_scale > 0.001f) g->sprite_scale -= 0.001f;
        break;

    case PS_ATTRACT:
        g->vel = v2add(g->vel, g->add_vel);
        if ((g->vel.x != 0 || g->vel.y != 0) && g->add_vel.x == 0 && g->add_vel.y == 0) friction(g);
        if (g->strokes < g->ndemo) {
            const DemoShot *s = &g->demo[g->strokes];
            if (g->demo_timer > s->wait_time) {
                g->vel = v2mul(v2norm(s->dir), s->power);
                g->strokes++;
                set_launch_pitch(g);
                transition(g, PS_MOVING);
                g->events |= EV_LAUNCH;
                return;
            }
            g->demo_timer++;
        }
        break;
    }
}

static void player_tick(Game *g, const GameInput *in) {
    if (in->click_pressed) {
        g->buffered_frame = 0;
        g->click_pos = in->mouse;
    }
    state_update(g, in);

    if (g->pstate != PS_LOSE && g->pstate != PS_WIN) {
        g->wall_death_timer++;
        if (g->wall_death_count >= WALL_DEATH_TIME - 1) {
            death(g, DEATH_WALL);
            return;
        }
        if (g->wall_death_timer > WALL_DEATH_TIME) {
            g->wall_death_count = 0;
            g->wall_death_timer = 0;
            g->bump_pitch = 1.0f;
        }
    }
    if (g->pstate == PS_LOSE || g->pstate == PS_WIN) {
        if (g->pstate == PS_WIN) return; // hitbox disabled, moved by the state
    }

    Collision c = phys_move_circle(&g->lv, &g->pos, PLAYER_RADIUS, v2mul(g->vel, TICK), PLAYER_MASK);
    if (c.hit) {
        if (g->pstate == PS_MOVING) {
            bool in_wall = phys_point_solid(&g->lv, g->pos, 1u);
            if (in_wall) g->wall_death_count++;
            g->events |= EV_BUMP;
            g->last_bump_shape = c.shape;
            g->bump_pitch += 0.05f;
        } else {
            g->events |= EV_PUSH;
        }
        g->vel = v2bounce(g->vel, c.normal);
        float av2 = v2len2(g->add_vel), vv2 = v2len2(g->vel);
        if (100 * av2 > vv2 && vv2 > 0) g->vel = v2mul(g->vel, sqrtf(100 * av2 / vv2));
    }
}

static void ghosts_tick(Game *g) {
    for (int i = 0; i < g->nghosts; i++) {
        if (!g->ghosts[i].alive) continue;
        vec2 dir = v2norm(v2sub(g->pos, g->ghosts[i].pos));
        g->ghosts[i].vel = v2limit(v2add(g->ghosts[i].vel, v2mul(dir, GHOST_ACCEL)), GHOST_LIMIT);
        g->ghosts[i].pos = v2add(g->ghosts[i].pos, v2mul(g->ghosts[i].vel, TICK));
    }
}

// Area2D overlaps, checked after everything moved (Godot flushes them after the physics step)
static void areas_tick(Game *g) {
    if (g->pstate == PS_WIN || g->pstate == PS_LOSE) return;
    for (int i = 0; i < g->ncoins; i++) {
        if (g->coins[i].taken) continue;
        float rr = g->coins[i].r + PLAYER_RADIUS;
        if (v2len2(v2sub(g->coins[i].pos, g->pos)) < rr * rr) {
            g->coins[i].taken = true;
            g->coins[i].popup = 1.0f;
            g->events |= EV_COIN;
            if (g->coins[i].mult) {
                g->level_bonus += g->coins[i].score + g->coins[i].multval * g->coin_multiplier;
                if (g->coin_multiplier <= 9) g->coin_multiplier++;
            } else {
                g->level_bonus += g->coins[i].score;
            }
        }
    }
    for (int i = 0; i < g->nghosts; i++) {
        if (!g->ghosts[i].alive) continue;
        float d2 = v2len2(v2sub(g->ghosts[i].pos, g->pos));
        float rr = GHOST_HITBOX + PLAYER_RADIUS;
        if (d2 < rr * rr) {
            death(g, DEATH_NORMAL);
            return;
        }
        float gr = GHOST_GRAZE + PLAYER_RADIUS;
        bool graze = d2 < gr * gr;
        if (g->ghosts[i].graze && !graze) g->events |= EV_GRAZE;
        g->ghosts[i].graze = graze;
    }
    if (g->has_goal) {
        float rr = g->goal_r + PLAYER_RADIUS;
        if (v2len2(v2sub(g->goal_pos, g->pos)) < rr * rr) {
            g->goal_target = g->goal_pos;
            win(g);
        }
    }
}

void game_tick(Game *g, const GameInput *in) {
    cur_in = in;
    g->events = 0;
    g->progress = PROG_NONE;

    // level.gd _physics_process
    if (g->mode != MODE_DEMO) {
        if (in->click_pressed && g->lose) g->progress = PROG_RESET;
        if (in->restart_pressed) {
            if (g->mode == MODE_NORMAL && g->victory) g->progress = PROG_WIN_RESET;
            else if (g->active || g->lose) g->progress = PROG_RESET;
        }
        if (g->active && in->pause_pressed) g->events |= EV_PAUSE;
        if ((g->active || g->lose) && !(g->lv.flags & LF_UNSKIPPABLE)) {
            if (in->skip_down) {
                if (++g->skipcounter > SKIPCOUNT) g->progress = PROG_SKIP;
            } else {
                g->skipcounter = 0;
            }
        }
    }
    if (!(g->lv.flags & LF_NEVER_OB) && g->active) {
        float m = fmaxf(fabsf(g->pos.x), fabsf(g->pos.y));
        if (m > g->lv.bounds + OB_RADIUS) death(g, (g->lv.flags & LF_ALWAYS_OB) ? DEATH_OB : DEATH_BACKROOMS);
    }

    player_tick(g, in);
    ghosts_tick(g);
    areas_tick(g);
    for (int i = 0; i < g->ncoins; i++)
        if (g->coins[i].popup > 0) g->coins[i].popup -= TICK;
    if (g->active) g->time += TICK;
    g->frame++;
}
