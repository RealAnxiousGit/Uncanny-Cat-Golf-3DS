// Gameplay for one level. See game.h for where each part comes from.
#include "game.h"

#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f

#define HUE_WEAKEST 0.69f
#define HUE_STRONGEST 0.0f
#define GOAL_RADIUS 16.0f
#define COIN_RADIUS 11.1803f
#define GHOST_HITBOX 12.1655f
#define GHOST_GRAZE 24.0f
#define GHOST_ACCEL 0.35f
#define GHOST_LIMIT 40.0f
#define FGHOST_HITBOX 36.0f // final_ghost.tscn Hitbox radius
#define HORSE_RADIUS 64.0f   // horse_boss.tscn CollisionShape2D (the body the walls stop)
#define HORSE_HITBOX 37.0f   // its Hitbox: a 74 x 74 square
#define HORSE_MASK 256u      // "Boss Walls"
#define CHARGE_TIME 60
#define HGOAL_RADIUS 10.0f   // horse_goal.tscn body (a default CircleShape2D)
#define HGOAL_HITBOX 16.0f

#define SKULL_RADIUS 16.0f
#define KEY_RADIUS 16.0f
#define KEY_ROTATE_SPEED 2.5f
#define BADDIE_DESPAWN 128
#define POPUP_TIME 1.0f

static void transition(Game *g, int st);
static void state_update(Game *g, const GameInput *in);
static void tracks_tick(Game *g);

// the collision shape of body i: a box for blocks and doors, a circle for skulls;
// layers 0 once it's gone (collision.disabled)
static void body_shape(Game *g, int i) {
    Body *b = &g->bodies[i];
    Shape *s = &g->body_shapes[i];
    // dog_golf.tscn collision_layer 65; the winner's hitbox is disabled
    s->layers = !b->alive || b->winner ? 0u : (b->type == OBJ_SKULL || b->type == OBJ_CAR) ? 8u
              : b->type == OBJ_DOG ? 65u : b->type == OBJ_PRISMGATE && b->passable ? 0u : 1u;
    if (b->type == OBJ_SKULL || b->type == OBJ_DOG) {
        s->kind = SHAPE_CIRCLE;
        s->c = b->pos;
        s->r = b->radius;
        s->n = 0;
        s->pts = NULL;
    } else {
        s->kind = SHAPE_POLY;
        s->n = 4;
        s->pts = g->body_pts[i];
        float c = cosf(b->rot), sn = sinf(b->rot);
        const float cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
        for (int k = 0; k < 4; k++) {
            float x = cx[k] * b->half.x, y = cy[k] * b->half.y;
            s->pts[k] = v2(b->pos.x + x * c - y * sn, b->pos.y + x * sn + y * c);
        }
    }
    shape_update_bbox(s);
}

static void body_remove(Game *g, int i) {
    g->bodies[i].alive = false;
    body_shape(g, i);
}

static float randf(float lo, float hi) { return lo + (hi - lo) * (rand() / (float)RAND_MAX); }

// an axis-aligned box (centre, half size) and a circle overlap
static bool box_overlaps(vec2 c, vec2 half, vec2 p, float r) {
    float nx = fmaxf(c.x - half.x, fminf(p.x, c.x + half.x));
    float ny = fmaxf(c.y - half.y, fminf(p.y, c.y + half.y));
    return (p.x - nx) * (p.x - nx) + (p.y - ny) * (p.y - ny) < r * r;
}

static bool same_color(const float a[3], const float b[3]) {
    return fabsf(a[0] - b[0]) < 1e-3f && fabsf(a[1] - b[1]) < 1e-3f && fabsf(a[2] - b[2]) < 1e-3f;
}

// block.gd hit() / key_door.gd hit(), when the moving cat runs into it
static void block_hit(Game *g, int i) {
    Body *b = &g->bodies[i];
    if (b->type == OBJ_KEYDOOR) {
        int found = -1;
        for (int h = 0; h < g->nheld && found < 0; h++)
            if (same_color(g->keys[g->held[h]].color, b->color)) found = h;
        if (found < 0) return;
        g->keys[g->held[found]].alive = false; // key.gd: not in has_keys any more -> remove()
        for (int h = found; h < g->nheld - 1; h++) g->held[h] = g->held[h + 1];
        g->nheld--;
        g->events |= EV_KEYDOOR;
    } else {
        g->events |= EV_DBLOCK;
    }
    body_remove(g, i);
    if (b->dblock_score) {
        g->dblock_multiplier++;
        b->popup_score = b->score * g->dblock_multiplier;
    } else {
        b->popup_score = b->score;
    }
    g->level_bonus += b->popup_score;
    b->popup = POPUP_TIME;
}

// level.gd switch(): switch_tiles[i].enabled = (i == switchState); dog_goal.gd: dog_tiles.enabled = false
static void tiles_apply(Game *g) {
    for (int t = 0; t < g->ntiles; t++)
        if (g->tiles[t].role == 0) g->tiles[t].on = g->tiles[t].idx == g->switch_state;
    for (int t = 0; t < g->ntiles; t++) // fake_goal.gd tiles_to_remove: on until a fake goal is hit
        if (g->tiles[t].role == 2) {
            g->tiles[t].on = true;
            for (int f = 0; f < g->nfakegoals; f++)
                if (g->fakegoals[f].taken && g->fakegoals[f].group == g->tiles[t].group) g->tiles[t].on = false;
        }
    for (int i = 0; i < g->lv.nshapes; i++) {
        Shape *s = &g->lv.shapes[i];
        if (!s->group) continue;
        bool on = true;
        for (int t = 0; t < g->ntiles; t++)
            if (g->tiles[t].group == s->group) on = g->tiles[t].on;
        s->layers = on ? s->base : 0u;
    }
}

// switch_block.gd hit(), from the cat or a dog
static void switch_hit(Game *g) {
    if (g->nswitch > 0) {
        g->switch_state = (g->switch_state + 1) % g->nswitch;
        tiles_apply(g);
        g->switch_pitch = randf(0.8f, 1.2f);
    } else {
        g->switch_pitch = randf(0.7f, 0.8f);
    }
    g->events |= EV_SWITCH;
}

// player.gd / dog_golf.gd: a dog that was bumped heads off along the bump, snapped to 45 degrees
static void dog_push(vec2 from, Body *dog) {
    const float step = 0.78539816f; // snappedf(angle, PI / 4)
    float a = floorf(atan2f(dog->pos.y - from.y, dog->pos.x - from.x) / step + 0.5f) * step;
    float l = v2len(dog->vel);
    dog->vel = v2(cosf(a) * l, sinf(a) * l);
}

// chuckle.tscn: baddie.gd with chaser on (its velocity starts at zero)
static void chuckle_setup(Body *b) {
    b->chaser = b->chuckle = true;
    b->chase_accel = 4;
    b->chase_limit = 200;
    b->vel = v2(0, 0);
    b->pitch_lo = 0.7f;
    b->pitch_hi = 1.3f;
}

// baddie_spawner.gd: on_death -> spawned_baddies.erase
static void baddie_gone(Game *g, Body *b) {
    if (b->owner >= 0 && g->spawners[b->owner].count > 0) g->spawners[b->owner].count--;
    b->owner = -1;
}

// baddie.gd death()
static void baddie_death(Game *g, int i) {
    Body *b = &g->bodies[i];
    body_remove(g, i);
    baddie_gone(g, b);
    b->vel = v2(0, 0);
    g->skull_pitch = randf(b->pitch_lo, b->pitch_hi);
    if (b->type == OBJ_CAR) {
        g->events |= EV_CAR;
        g->thought = TH_CAR;
    } else if (b->chuckle) {
        g->events |= EV_CHUCKLE;
        g->thought = TH_CHUCKLE;
    } else {
        g->events |= EV_SKULL;
        g->thought = TH_SKULL;
    }
    b->popup_score = b->score;
    g->level_bonus += b->score;
    b->popup = POPUP_TIME;
}

// dog_golf.gd _physics_process
static void dog_tick(Game *g, int i) {
    Body *b = &g->bodies[i];
    if (b->winner) { // into the goal, shrinking
        b->vel = v2(0, 0);
        vec2 gp = g->doggoals[b->goal].pos;
        b->pos = v2(lerpf(b->pos.x, gp.x, 0.05f), lerpf(b->pos.y, gp.y, 0.05f));
        if (b->dscale > 0.001f) b->dscale -= 0.001f;
        body_shape(g, i);
        return;
    }
    phys_set_exclude(g->lv.nshapes + i);
    Collision c = phys_move_circle(&g->lv, &b->pos, b->radius, v2mul(b->vel, TICK), 9u);
    phys_set_exclude(-1);
    if (c.hit) {
        b->vel = v2bounce(b->vel, c.normal);
        g->events |= EV_DOGBUMP;
        int o = c.shape - g->lv.nshapes;
        if (o >= 0) {
            Body *ob = &g->bodies[o];
            if (ob->type == OBJ_SKULL || ob->type == OBJ_CAR) baddie_death(g, o);
            else if (ob->type == OBJ_DBLOCK) block_hit(g, o); // key doors only open for the cat
            else if (ob->type == OBJ_SWITCH) switch_hit(g);
            else if (ob->type == OBJ_DOG && ob->bounce) dog_push(b->pos, ob);
        }
    }
    body_shape(g, i);
}

// dog_goal.gd _on_body_entered (collision_mask 64: dogs only)
static void doggoals_tick(Game *g) {
    for (int d = 0; d < g->ndoggoals; d++) {
        if (g->doggoals[d].taken) continue;
        for (int i = 0; i < g->nbodies; i++) {
            Body *b = &g->bodies[i];
            if (b->type != OBJ_DOG || b->winner) continue;
            float rr = g->doggoals[d].r + b->radius;
            if (v2len2(v2sub(b->pos, g->doggoals[d].pos)) >= rr * rr) continue;
            b->goal = d;
            b->winner = true;
            body_shape(g, i);
            g->doggoals[d].taken = true;
            for (int t = 0; t < g->ntiles; t++)
                if (g->tiles[t].group == g->doggoals[d].group) g->tiles[t].on = false;
            tiles_apply(g);
            g->events |= EV_DOGWIN;
            g->thought = TH_DOG;
            break;
        }
    }
}

// smiley_ring.gd clear()
static void ring_clear(Game *g, int r, bool win) {
    __typeof__(g->rings[0]) *R = &g->rings[r];
    R->grabbed = 0;
    R->running = false;
    for (int c = 0; c < g->nscoins; c++)
        if (g->scoins[c].ring == r) g->scoins[c].active = false;
    R->won = win;
    if (win) {
        g->level_bonus += R->score;
        R->after = RING_WIN_TIME;
        g->events |= EV_RINGWIN;
    } else {
        R->after = RING_FAIL_TIME;
        g->events |= EV_RINGFAIL;
    }
}

// smiley_ring.gd _physics_process and its Timer
static void rings_tick(Game *g) {
    for (int r = 0; r < g->nrings; r++) {
        __typeof__(g->rings[0]) *R = &g->rings[r];
        if (R->gone) continue;
        if (R->running) {
            R->left -= TICK;
            if (R->grabbed >= R->ncoins) ring_clear(g, r, true);
            else if (R->left <= 0) ring_clear(g, r, false);
        } else if (R->after > 0) {
            R->after -= TICK; // $win / $lose finished: remove(), or reset() when redoable
            if (R->after <= 0 && (R->won || !R->redoable)) R->gone = true;
        }
    }
}

// baddie.gd _physics_process: straight line, bouncing off walls and other baddies
static void bodies_tick(Game *g) {
    for (int i = 0; i < g->nbodies; i++) {
        Body *b = &g->bodies[i];
        if (b->popup > 0) b->popup -= TICK;
        if (b->type == OBJ_KEYDOOR) { // the lock fades while a matching key is held
            bool yay = false;
            for (int h = 0; h < g->nheld; h++) yay |= same_color(g->keys[g->held[h]].color, b->color);
            b->lock_a = lerpf(b->lock_a, yay ? 0.0f : 1.0f, 0.1f);
        }
        if (b->type == OBJ_DOG) {
            dog_tick(g, i);
            continue;
        }
        if ((b->type != OBJ_SKULL && b->type != OBJ_CAR) || !b->alive) continue;
        float far = b->ignorey ? fmaxf(fabsf(b->pos.x), g->lv.bounds * 0.5f) : fmaxf(fabsf(b->pos.x), fabsf(b->pos.y));
        if (far > g->lv.bounds + BADDIE_DESPAWN) {
            body_remove(g, i);
            baddie_gone(g, b);
            continue;
        }
        if (b->type == OBJ_CAR) { // collision_mask 0: drives through everything
            b->pos = v2add(b->pos, v2mul(b->vel, TICK));
            body_shape(g, i);
            continue;
        }
        if (b->chaser) { // baddie.gd: chase the cat
            b->vel = v2add(b->vel, v2mul(v2norm(v2sub(g->pos, b->pos)), b->chase_accel));
            float l = v2len(b->vel);
            if (l > b->chase_limit) b->vel = v2mul(b->vel, b->chase_limit / l);
        }
        phys_set_exclude(g->lv.nshapes + i);
        Collision c = phys_move_circle(&g->lv, &b->pos, b->radius, v2mul(b->vel, TICK), 9u);
        phys_set_exclude(-1);
        if (c.hit) b->vel = v2bounce(b->vel, c.normal);
        body_shape(g, i);
    }
}

// a slot for a new body: a car or skull that is gone (and done showing its score), or a new one
static int body_slot(Game *g) {
    for (int i = 0; i < g->nbodies; i++) {
        const Body *b = &g->bodies[i];
        if (!b->alive && (b->type == OBJ_CAR || b->type == OBJ_SKULL) && b->popup <= 0 && b->owner < 0) return i;
    }
    return g->nbodies < MAX_BODIES ? g->nbodies++ : -1;
}

// baddie_spawner.gd _physics_process
static void spawners_tick(Game *g) {
    for (int s = 0; s < g->nspawners; s++) {
        __typeof__(g->spawners[0]) *sp = &g->spawners[s];
        sp->countdown--;
        if (sp->countdown > 0 || sp->count >= sp->max) continue;
        int i = body_slot(g);
        if (i < 0) continue;
        Body *b = &g->bodies[i];
        memset(b, 0, sizeof(*b));
        b->type = sp->skull ? OBJ_SKULL : OBJ_CAR;
        b->alive = true;
        b->owner = s;
        b->pos = sp->pos;
        b->vel = v2mul(v2norm(sp->dir), sp->speed);
        b->score = sp->score;
        b->spr = sp->car_spr;
        b->spr2 = -1;
        b->pitch_lo = b->pitch_hi = 1;
        b->half = v2(8, 8);  // car.tscn RectangleShape2D 16x16
        b->radius = SKULL_RADIUS;
        b->ignorey = sp->ignorey;
        if (sp->enemy == 2) chuckle_setup(b);
        body_shape(g, i);
        sp->count++;
        sp->countdown = sp->rate;
    }
}

// police_event.gd _on_body_entered: a new spawner (or a single car) at e_position
static void police_event(Game *g) {
    const ObjDef *o = g->police;
    g->police_done = true;
    g->level_bonus += (int)obj_param(o, "score_value", 0);
    g->events |= EV_POLICE;
    g->thought = TH_POLICE;
    vec2 at = v2(obj_param(o, "e_x", 0), obj_param(o, "e_y", 0));
    vec2 dir = v2(obj_param(o, "dir_x", 0), obj_param(o, "dir_y", 0));
    float speed = obj_param(o, "speed", 0);
    int car_spr = (int)obj_param(o, "spr2", -1);
    int enemy = (int)obj_param(o, "enemy", 0);
    bool skull = enemy != 0;
    if (obj_param(o, "has_spawner", 0) != 0) {
        if (g->nspawners >= MAX_SPAWNERS) return;
        __typeof__(g->spawners[0]) *sp = &g->spawners[g->nspawners++];
        memset(sp, 0, sizeof(*sp));
        sp->pos = at;
        sp->dir = dir;
        sp->speed = speed;
        sp->rotate = obj_param(o, "rotate", 0);
        sp->score = (int)obj_param(o, "b_score_value", 0);
        sp->rate = (int)obj_param(o, "spawn_rate", 0);
        sp->countdown = (int)obj_param(o, "spawn_countdown", 0);
        sp->max = (int)obj_param(o, "max_baddies", 0);
        sp->spr = (int)obj_param(o, "spr", -1);
        sp->car_spr = car_spr;
        sp->skull = skull;
        sp->enemy = enemy;
        return;
    }
    int i = body_slot(g);
    if (i < 0) return;
    Body *b = &g->bodies[i];
    memset(b, 0, sizeof(*b));
    b->type = skull ? OBJ_SKULL : OBJ_CAR;
    b->alive = true;
    b->owner = -1;
    b->pos = at;
    b->vel = v2mul(v2norm(dir), speed);
    b->score = (int)obj_param(o, "b_score_value", 0);
    b->spr = car_spr;
    b->spr2 = -1;
    b->pitch_lo = b->pitch_hi = 1;
    b->half = v2(8, 8);
    b->radius = SKULL_RADIUS;
    if (enemy == 2) chuckle_setup(b);
    body_shape(g, i);
}

// slot_machine.gd check_slots(), once every reel has stopped
static bool happy_slot(int f) { return f == 0 || f == 1 || f == 2 || f == 5 || f == 6; }

static void check_slots(Game *g) {
    g->slots_stopped++;
    if (g->nslots != 3 || g->slots_stopped != 3 || !g->has_slot_machine) return;
    int a = g->slots[0].frame, b = g->slots[1].frame, c = g->slots[2].frame;
    static const int RESULT[10] = {SLOT_JACKPOT, SLOT_HAPPY, SLOT_OK, SLOT_QUESTION, SLOT_PHONE,
                                   SLOT_CHERRY, SLOT_EYE, SLOT_BLUE, SLOT_SAD, SLOT_SKULL};
    static const int BONUS[10] = {10000, 4000, 2000, 0, 2000, 5000, 2000, 2000, 1500, 2000};
    if (a == b && a == c) {
        g->slot_result = RESULT[a];
        g->level_bonus += a == 3 ? 1 + rand() % 10000 : BONUS[a]; // randi_range(1, 10000)
        if (a == 6) g->slot_eyes = true;
        if (a == 7) g->blue = 0.0001f; // blue_anim.play("Blue")
        if (a == 9)                     // the hidden skull spawners wake up
            for (int s = 0; s < g->nspawners; s++)
                if (g->spawners[s].max == 0) g->spawners[s].max = g->skull_num, g->spawners[s].hidden = false;
    } else if (a == 0 && a == b && c != a) {
        g->slot_result = SLOT_CLOSE, g->level_bonus += 1000;
    } else if (happy_slot(a) && happy_slot(b) && happy_slot(c)) {
        g->slot_result = SLOT_GOOD, g->level_bonus += 3000;
    } else if (a == b && c != a) {
        g->slot_result = SLOT_BAD, g->level_bonus += 500;
    } else {
        g->slot_result = SLOT_LOSE, g->level_bonus += 1;
    }
    g->events |= EV_SLOTS;
}

// slot.gd _physics_process: the reel steps one symbol every rate+1 frames until stopped
static void slots_tick(Game *g) {
    for (int i = 0; i < g->nslots; i++) {
        if (!g->slots[i].spinning) continue;
        if (++g->slots[i].cur > g->slots[i].rate) {
            g->slots[i].frame = (g->slots[i].frame + 1) % 10;
            g->slots[i].cur = 0;
        }
    }
    if (g->blue > 0 && g->blue < 1) g->blue = fminf(1, g->blue + TICK);
}

// key.gd: held keys circle the cat
static void keys_tick(Game *g) {
    g->key_rotation_offset = v2(
        g->key_rotation_offset.x * cosf(TICK * KEY_ROTATE_SPEED) - g->key_rotation_offset.y * sinf(TICK * KEY_ROTATE_SPEED),
        g->key_rotation_offset.x * sinf(TICK * KEY_ROTATE_SPEED) + g->key_rotation_offset.y * cosf(TICK * KEY_ROTATE_SPEED));
    for (int h = 0; h < g->nheld; h++) {
        Key *k = &g->keys[g->held[h]];
        float a = 2 * PI_F * h / g->nheld;
        vec2 off = v2(g->key_rotation_offset.x * cosf(a) - g->key_rotation_offset.y * sinf(a),
                      g->key_rotation_offset.x * sinf(a) + g->key_rotation_offset.y * cosf(a));
        k->vel = v2add(k->vel, v2mul(v2sub(v2add(g->pos, off), k->pos), k->accel));
        k->vel = v2mul(k->vel, 0.35f);                 // FRICTION
        k->accel = fminf(5.0f, k->accel + 0.01f);      // ACCEL_MAX, ACCEL_RAMP
        k->pos = v2add(k->pos, v2mul(k->vel, 0.75f));  // SPEED
        if (k->alpha > 0.5f) k->alpha -= 0.05f;
    }
    for (int i = 0; i < g->nkeys; i++)
        if (g->keys[i].popup > 0) g->keys[i].popup -= TICK;
}

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
            g->goal_ry = GOAL_RADIUS * o->sy;
            break;
        case OBJ_GHOST:
        case OBJ_FGHOST:
            if (g->nghosts < MAX_GHOSTS) {
                __typeof__(g->ghosts[0]) *gh = &g->ghosts[g->nghosts++];
                gh->pos = o->pos;
                gh->alive = true;
                gh->scale = 1;
                gh->hit = GHOST_HITBOX;
                gh->met = (int)obj_param(o, "met", -1);
                gh->mslot = (int)obj_param(o, "mslot", -1);
                if (o->type == OBJ_FGHOST) { // active only once a fake goal says so
                    gh->final = gh->inactive = true;
                    gh->scale = o->sx;
                    gh->hit = FGHOST_HITBOX * o->sx;
                }
            }
            break;
        case OBJ_HORSE:
            if (!g->horse.on) {
                __typeof__(g->horse) *h = &g->horse;
                h->on = true;
                h->pos = o->pos;
                h->scale = o->sx;
                h->health = h->start_health = (int)obj_param(o, "health", 10);
                h->base_speed = obj_param(o, "base_speed", 150);
                h->attack_time = h->attack_timer = (int)obj_param(o, "attack_time", 0);
                h->target_delay = (int)obj_param(o, "target_delay", 0);
                h->dir = v2(obj_param(o, "dir_x", 0), obj_param(o, "dir_y", 0));
                h->vel = v2mul(v2norm(h->dir), h->base_speed);
                h->end_of_life = obj_param(o, "end_of_life", 0) != 0;
                h->spr = (int)obj_param(o, "spr", -1);
                h->sx = h->sy = 0.25f;
                h->fade = 1;
            }
            break;
        case OBJ_HGOAL:
            if (g->nhgoals < MAX_HGOALS) {
                __typeof__(g->hgoals[0]) *hg = &g->hgoals[g->nhgoals++];
                hg->pos = o->pos;
                hg->scale = o->sx;
                hg->r = HGOAL_HITBOX * o->sx;
                hg->next = (int)obj_param(o, "next", -1);
                hg->spr = (int)obj_param(o, "spr", -1);
                hg->active = obj_param(o, "active", 0) != 0;
                hg->sx = 0.376471f, hg->sy = 0.367816f;
            }
            break;
        case OBJ_MINE:
            if (g->nmines < MAX_MINES) {
                __typeof__(g->mines[0]) *m = &g->mines[g->nmines++];
                m->pos = o->pos;
                m->r = obj_param(o, "radius", 16) * o->sx;
                m->spr = (int)obj_param(o, "spr", -1);
            }
            break;
        case OBJ_METRONOME:
            if (g->nmetros < MAX_METROS) {
                __typeof__(g->metros[0]) *M = &g->metros[g->nmetros++];
                M->m = (int)obj_param(o, "m", 0);
                M->every = (int)obj_param(o, "every", 2);
                if (M->every < 1) M->every = 1;
                M->n = (int)obj_param(o, "n", 0);
                M->snd = (int)obj_param(o, "snd", 0);
                M->vol = obj_param(o, "vol", 1);
                M->pitch = obj_param(o, "pitch", 1);
                M->visual = obj_param(o, "visual", 1) != 0;
                M->tile = obj_param(o, "tile", 0) != 0;
            }
            break;
        case OBJ_BOSSAPPEAR:
            g->winfake_target = o->pos;
            break;
        case OBJ_PORTAL:
            if (g->nportals < MAX_PORTALS) {
                __typeof__(g->portals[0]) *pt = &g->portals[g->nportals++];
                pt->pos = o->pos;
                pt->half = v2(obj_param(o, "size_x", 16) * o->sx * 0.5f, obj_param(o, "size_y", 16) * o->sy * 0.5f);
                pt->target = (int)obj_param(o, "target", -1);
                pt->spr = (int)obj_param(o, "spr", -1);
            }
            break;
        case OBJ_FAKEGOAL:
            if (g->nfakegoals < MAX_FAKEGOALS) {
                __typeof__(g->fakegoals[0]) *f = &g->fakegoals[g->nfakegoals++];
                f->pos = o->pos;
                f->r = obj_param(o, "radius", 16) * o->sx;
                f->score = (int)obj_param(o, "score_value", 500);
                f->spr = (int)obj_param(o, "spr", -1);
                f->spr2 = (int)obj_param(o, "spr2", -1);
                f->group = (int)obj_param(o, "group", 0);
                f->ghost = obj_param(o, "ghost", 0) != 0;
                f->alt = obj_param(o, "alt", 0) != 0;
            }
            break;
        case OBJ_DBLOCK:
        case OBJ_KEYDOOR:
        case OBJ_SKULL:
        case OBJ_CAR:
        case OBJ_SWITCH:
        case OBJ_DOG:
        case OBJ_PRISMGATE:
            if (g->nbodies < MAX_BODIES) {
                Body *b = &g->bodies[g->nbodies++];
                b->type = o->type;
                b->owner = -1;
                b->alive = true;
                b->pos = o->pos;
                b->half = v2(obj_param(o, "size_x", 32) * o->sx * 0.5f, obj_param(o, "size_y", 32) * o->sy * 0.5f);
                b->color[0] = obj_param(o, "color_r", 1);
                b->color[1] = obj_param(o, "color_g", 1);
                b->color[2] = obj_param(o, "color_b", 1);
                b->score = (int)obj_param(o, "score_value", 0);
                b->dblock_score = obj_param(o, "dblock_score", 0) != 0;
                b->spr = (int)obj_param(o, "spr", -1);
                b->spr2 = (int)obj_param(o, "spr2", -1);
                b->pitch_lo = obj_param(o, "pitch_lo", 1);
                b->pitch_hi = obj_param(o, "pitch_hi", 1);
                b->lock_a = 1;
                b->rot = o->rot;
                b->radius = SKULL_RADIUS * o->sx;
                if (o->type == OBJ_CAR) b->half = v2(8 * o->sx, 8 * o->sy), b->rot = 0; // only the picture turns
                if (o->type == OBJ_SKULL || o->type == OBJ_CAR || o->type == OBJ_DOG) // velocity = speed * direction.normalized()
                    b->vel = v2mul(v2norm(v2(obj_param(o, "dir_x", 0), obj_param(o, "dir_y", 0))),
                                   obj_param(o, "speed", 0));
                if (o->type == OBJ_SKULL && obj_param(o, "chaser", 0) != 0) {
                    b->chaser = true; // baddie.gd: a chaser's velocity isn't set in _ready()
                    b->chase_accel = obj_param(o, "chase_accel", 4);
                    b->chase_limit = obj_param(o, "chase_limit", 200);
                    b->vel = v2(0, 0);
                }
                b->chuckle = obj_param(o, "chuckle", 0) != 0;
                if (o->type == OBJ_PRISMGATE) {
                    b->prisms = (int)obj_param(o, "prisms", 0);
                    b->passable = set->total_prisms >= b->prisms;
                    b->half = v2(obj_param(o, "size_x", 64) * o->sx * 0.5f, obj_param(o, "size_y", 64) * o->sy * 0.5f);
                }
                if (o->type == OBJ_DOG) {
                    b->radius = obj_param(o, "radius", 15.8f) * o->sx;
                    b->bounce = obj_param(o, "bounce", 1) != 0;
                    b->dscale = 0.064f;
                }
            }
            break;
        case OBJ_KEY:
            if (g->nkeys < MAX_KEYS) {
                Key *k = &g->keys[g->nkeys++];
                k->pos = o->pos;
                k->alive = true;
                k->accel = -0.2f; // ACCEL_START
                k->alpha = 1;
                k->color[0] = obj_param(o, "color_r", 1);
                k->color[1] = obj_param(o, "color_g", 1);
                k->color[2] = obj_param(o, "color_b", 1);
                k->score = (int)obj_param(o, "score_value", 0);
                k->spr = (int)obj_param(o, "spr", -1);
            }
            break;
        case OBJ_POLICE:
            g->police = o;
            break;
        case OBJ_LIGHTS:
            g->lights_score = (int)obj_param(o, "score_value", 0);
            g->lights_goal = obj_param(o, "goal", 0) != 0;
            break;
        case OBJ_ASHBABY:
            g->ash_score = (int)obj_param(o, "score_value", 0);
            break;
        case OBJ_TILES:
            if (g->ntiles < MAX_TILES) {
                __typeof__(g->tiles[0]) *t = &g->tiles[g->ntiles++];
                t->group = (int)obj_param(o, "group", 0);
                t->role = (int)obj_param(o, "role", 0);
                t->idx = (int)obj_param(o, "idx", 0);
                t->spr = (int)obj_param(o, "spr", -1);
                t->on = obj_param(o, "on", 1) != 0;
                t->over = obj_param(o, "over", 0) != 0;
                if (t->role == 0) g->nswitch++;
            }
            break;
        case OBJ_DOGGOAL:
            if (g->ndoggoals < MAX_DOGGOALS) {
                __typeof__(g->doggoals[0]) *d = &g->doggoals[g->ndoggoals++];
                d->pos = o->pos;
                d->r = obj_param(o, "radius", 16) * o->sx;
                d->group = (int)obj_param(o, "group", 0);
                d->spr = (int)obj_param(o, "spr", -1);
            }
            break;
        case OBJ_RING:
            if (g->nrings < MAX_RINGS) {
                __typeof__(g->rings[0]) *r = &g->rings[g->nrings++];
                r->pos = o->pos;
                r->r = obj_param(o, "radius", 24) * o->sx;
                r->time_given = obj_param(o, "time_given", 0);
                r->score = (int)obj_param(o, "score_value", 0);
                r->spr = (int)obj_param(o, "spr", -1);
                r->bell = obj_param(o, "ring_bell", 1) != 0;
                r->redoable = obj_param(o, "redoable", 0) != 0;
                r->color[0] = obj_param(o, "color_r", 1);
                r->color[1] = obj_param(o, "color_g", 1);
                r->color[2] = obj_param(o, "color_b", 1);
            }
            break;
        case OBJ_SCOIN:
            if (g->nscoins < MAX_SCOINS) {
                __typeof__(g->scoins[0]) *c = &g->scoins[g->nscoins++];
                c->pos = o->pos;
                c->r = obj_param(o, "radius", 10) * o->sx;
                c->ring = (int)obj_param(o, "ring", -1);
                c->spr = (int)obj_param(o, "spr", -1);
            }
            break;
        case OBJ_SLOT:
            if (g->nslots < MAX_SLOTS) {
                __typeof__(g->slots[0]) *s = &g->slots[g->nslots++];
                s->pos = o->pos;
                s->rate = (int)obj_param(o, "rate", 10);
                s->coin = (int)obj_param(o, "coin", -1);
                s->spr = (int)obj_param(o, "spr", -1);
                s->nframes = (int)obj_param(o, "frames", 0);
                s->spinning = true;
            }
            break;
        case OBJ_SLOTMACHINE:
            g->has_slot_machine = true;
            g->slot_machine_pos = o->pos;
            g->skull_num = (int)obj_param(o, "skull_num", 30);
            break;
        case OBJ_JUMPPAD:
            if (g->npads < MAX_PADS) {
                __typeof__(g->pads[0]) *p = &g->pads[g->npads++];
                p->pos = o->pos;
                p->r = obj_param(o, "radius", 16) * o->sx;
                p->dir = obj_param(o, "direction", 0);
                p->speed_granted = obj_param(o, "speed_granted", 0);
                p->spr = (int)obj_param(o, "spr", -1);
            }
            break;
        case OBJ_SPAWNER:
            if (g->nspawners < MAX_SPAWNERS) {
                __typeof__(g->spawners[0]) *sp = &g->spawners[g->nspawners++];
                sp->pos = o->pos;
                sp->dir = v2(obj_param(o, "dir_x", 0), obj_param(o, "dir_y", 0));
                sp->speed = obj_param(o, "speed", 0);
                sp->rotate = obj_param(o, "rotate", 0);
                sp->score = (int)obj_param(o, "score_value", 0);
                sp->rate = (int)obj_param(o, "spawn_rate", 0);
                sp->countdown = (int)obj_param(o, "spawn_countdown", 0);
                sp->max = (int)obj_param(o, "max_baddies", 0);
                sp->spr = (int)obj_param(o, "spr", -1);
                sp->car_spr = (int)obj_param(o, "spr2", -1);
                sp->skull = obj_param(o, "enemy", 0) != 0;
                sp->enemy = (int)obj_param(o, "enemy", 0);
                sp->ignorey = obj_param(o, "ignorey", 0) != 0;
                sp->hidden = o->hidden;
            }
            break;
        case OBJ_TOKEN:
        case OBJ_COIN:
        case OBJ_NUKE:
            if (g->ncoins < MAX_COINS) {
                g->coins[g->ncoins].nuke = o->type == OBJ_NUKE;
                g->coins[g->ncoins].nuke_spawners = obj_param(o, "spawners", 0) != 0;
                g->coins[g->ncoins].pos = o->pos;
                g->coins[g->ncoins].r = (o->type != OBJ_COIN ? obj_param(o, "radius", 16) : COIN_RADIUS) * o->sx;
                g->coins[g->ncoins].does_speed = obj_param(o, "does_speed", 0) != 0;
                g->coins[g->ncoins].set_speed = obj_param(o, "set_speed", 0);
                g->coins[g->ncoins].spr = o->type != OBJ_COIN ? (int)obj_param(o, "spr", -1) : -1;
                g->coins[g->ncoins].score = (int)obj_param(o, "score_value", 0);
                g->coins[g->ncoins].mult = obj_param(o, "coin_multiply", 0) != 0;
                g->coins[g->ncoins].multval = (int)obj_param(o, "coin_multiply_value", 0);
                g->ncoins++;
            }
            break;
        }
    }
    for (int c = 0; c < g->nscoins; c++)
        if (g->scoins[c].ring >= 0 && g->scoins[c].ring < g->nrings) g->rings[g->scoins[c].ring].ncoins++;
    tiles_apply(g); // level.gd _ready(): switch(); also undoes the last try's switches
    for (int i = 0; i < g->nbodies; i++) body_shape(g, i);
    phys_set_dynamic(g->body_shapes, g->nbodies);
    g->sand_claimer = -1;
    g->jump_pad = -1;
    g->wind_key = -1;
    tracks_tick(g); // the animations' first frame is in place before the cat drops in
    g->key_rotation_offset = v2(48, 0);
    g->thought = TH_NONE; // the level start's Idle thought is played by the app
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
    if (g->horse.on) {
        g->horse.vel = v2(0, 0);
        g->horse.mode = HORSE_LOSS;
        for (int i = 0; i < g->nhgoals; i++) g->hgoals[i].gone = true;
    }
    g->active = false;
    g->lose = true;
    g->death_type = type;
    transition(g, PS_LOSE);
    g->sprite_visible = false;
    if (type == DEATH_WALL) g->events |= EV_DEATH_WALL;
    else if (type == DEATH_BACKROOMS) g->events |= EV_BACKROOMS;
    else if (type == DEATH_OB) g->events |= EV_OB;
    else if (type == DEATH_WATER) g->events |= EV_WATER_DEATH;
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
        if (g->moving_type == 0) g->coin_multiplier = 0;
        g->moving_type = 0;
        g->burst_buffer = 0;
        g->moving_first_frame = true;
        break;
    case PS_WIN:
        g->hit_visible = false;
        g->vel = v2(0, 0);
        break;
    case PS_WINFAKE: // PWinFake.enter
        g->winfake_count = 0;
        g->hit_visible = false;
        g->vel = v2(0, 0);
        break;
    case PS_CUSTOM:
        break;
    case PS_LOSE:
        g->vel = v2(0, 0);
        g->add_vel = v2(0, 0);
        break;
    case PS_ATTRACT:
        g->events |= EV_PICKUP;
        g->demo_timer = 0;
        break;
    case PS_JUMP: // PJump.enter: collision_layer 0, collision_mask = jump_collision_mask
        g->jump_timer = 0;
        g->hit_visible = false;
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

    case PS_WINFAKE: // PWinFake.update: drift into the event and shrink, then on to the next level
        g->winfake_count++;
        if (g->winfake_count > 60 * 7) g->progress = PROG_NORMAL;
        if (g->winfake_count > 120) break;
        g->pos = v2add(g->pos, v2mul(v2sub(g->winfake_target, g->pos), 0.005f));
        if (g->sprite_scale > 0.0005f) g->sprite_scale -= 0.0005f;
        break;
    case PS_WIN:
        g->pos = v2add(g->pos, v2mul(v2sub(g->goal_target, g->pos), 0.05f));
        if (g->sprite_scale > 0.001f) g->sprite_scale -= 0.001f;
        break;

    case PS_JUMP: // PJump.update: the cat grows, then shrinks back, then rolls on
        g->jump_timer++;
        if (g->jump_timer > JUMP_TIME) {
            g->sprite_scale = 0.064f;
            g->moving_type = 1;
            transition(g, PS_MOVING);
            return;
        }
        g->sprite_scale = lerpf(g->sprite_scale, 0.15f, g->jump_timer > JUMP_TIME / 2 ? -0.13f : 0.13f);
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
    if (g->portal_sick) {
        if (g->portal_sick_timer <= 0) g->portal_sick = false;
        else g->portal_sick_timer--;
    }
    if (in->click_pressed) {
        g->buffered_frame = 0;
        g->click_pos = in->mouse;
    }
    state_update(g, in);

    if (g->pstate == PS_CUSTOM || g->pstate == PS_WINFAKE) return; // held by something else
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

    Collision c = phys_move_circle(&g->lv, &g->pos, PLAYER_RADIUS, v2mul(g->vel, TICK),
                                   g->pstate == PS_JUMP ? JUMP_MASK : PLAYER_MASK);
    if (c.hit) {
        bool hit_moving_wall = false;
        int body = c.shape - g->lv.nshapes; // >= 0: a block, door or skull
        if (g->pstate == PS_MOVING) {
            if (body >= 0 && (g->bodies[body].type == OBJ_SKULL || g->bodies[body].type == OBJ_CAR)) {
                hit_moving_wall = true;
                baddie_death(g, body);
                g->events |= EV_BUMP;
            } else if (body >= 0 && g->bodies[body].type == OBJ_SWITCH) {
                switch_hit(g);
                g->events |= EV_BUMP;
            } else if (body >= 0 && (g->bodies[body].type == OBJ_DOG || g->bodies[body].type == OBJ_PRISMGATE)) {
                g->events |= EV_BUMP; // prism_gate.gd hit(): nothing
            } else if (body >= 0) {
                block_hit(g, body);
                g->events |= EV_BUMP;
            } else {
                bool in_wall = phys_point_solid(&g->lv, g->pos, 1u);
                if (in_wall) g->wall_death_count++;
                g->events |= EV_BUMP;
                g->last_bump_shape = c.shape;
                g->bump_pitch += 0.05f;
            }
        } else {
            g->events |= EV_PUSH;
        }
        g->vel = v2bounce(g->vel, c.normal);
        if (hit_moving_wall) g->pos = v2add(g->pos, v2mul(g->vel, TICK));
        float av2 = v2len2(g->add_vel), vv2 = v2len2(g->vel);
        if (100 * av2 > vv2 && vv2 > 0) g->vel = v2mul(g->vel, sqrtf(100 * av2 / vv2));
        if (body >= 0 && g->bodies[body].type == OBJ_DOG && g->bodies[body].bounce) dog_push(g->pos, &g->bodies[body]);
    }
}

// metronome.gd: one TriggerList entry on or off (a tile layer, or ghosts)
static void metro_set(Game *g, const __typeof__(g->metros[0]) *M, int slot, bool on) {
    if (M->tile) {
        for (int t = 0; t < g->ntiles; t++)
            if (g->tiles[t].role == 3 && g->tiles[t].idx == M->m * 16 + slot) g->tiles[t].on = on;
    } else {
        for (int i = 0; i < g->nghosts; i++)
            if (g->ghosts[i].met == M->m && g->ghosts[i].mslot == slot) g->ghosts[i].frozen = !on;
    }
}

// the level's AnimationPlayers that move the game: wind (additional_velocity) and a goal on a path
static void tracks_tick(Game *g) {
    for (int k = 0; k < g->lv.ntracks; k++) {
        const Track2 *tr = &g->lv.tracks[k];
        float t = g->anim_t;
        if (tr->loop && tr->length > 0) t = fmodf(t, tr->length);
        else if (t > tr->length) t = tr->length;
        int i = 0;
        while (i + 1 < tr->n && tr->t[i + 1] <= t) i++;
        if (tr->kind == TRACK_WIND) {
            if (i != g->wind_key) {
                g->wind_key = i;
                if (g->pstate != PS_LOSE) g->add_vel = v2(tr->x[i], tr->y[i]);
            }
        } else if (tr->kind == TRACK_GOAL && !g->victory) { // moving_goal.gd: it stops once the cat is in
            vec2 p = v2(tr->x[i], tr->y[i]);
            if (i + 1 < tr->n && !tr->discrete && tr->t[i + 1] > tr->t[i]) {
                float f = (t - tr->t[i]) / (tr->t[i + 1] - tr->t[i]);
                if (f > 0) p = v2(lerpf(tr->x[i], tr->x[i + 1], f), lerpf(tr->y[i], tr->y[i + 1], f));
            }
            g->goal_pos = p;
        }
    }
    g->anim_t += TICK;
}

static void metronome_tick(Game *g, const GameInput *in) {
    if (in->beat < 0) return;
    for (int m = 0; m < g->nmetros; m++) {
        __typeof__(g->metros[0]) *M = &g->metros[m];
        if (!M->started) { // _ready(): _last_beat = the current beat
            M->started = true;
            M->last = in->beat;
            if (M->visual && g->mode != MODE_DEMO) g->thought = TH_METRONOME;
            continue;
        }
        if (in->beat == M->last) continue;
        M->last = in->beat;
        M->counted++;
        if (M->counted <= 6 && M->visual && g->mode != MODE_DEMO) g->thought = TH_METRONOME;
        if (M->counted % M->every == 0) { // _trigger()
            if (M->snd) g->metro_trig |= 1u << m;
            g->events |= EV_METRO_TRIG;
            if (M->n > 0) {
                for (int i = 0; i < M->n; i++) metro_set(g, M, i, false);
                metro_set(g, M, M->cur, true);
                M->cur = (M->cur + 1) % M->n;
                tiles_apply(g);
            }
        }
        if (M->visual) {
            g->metro_beat_pitch = (M->counted + 3) % 4 == 0 ? 1.2f : 1.0f;
            g->events |= EV_METRO_BEAT;
            M->swing = !M->swing;
            if (M->counted > 14) M->visual = false; // poof
        }
    }
}

// horse_boss.gd hit()
static void horse_hit(Game *g) {
    __typeof__(g->horse) *h = &g->horse;
    if (!h->on) return;
    h->health--;
    g->events |= EV_HHURT | EV_HBOOM;
    if (g->mode != MODE_DEMO) g->thought = TH_HORSE;
    if (h->health == 0) {
        g->active = false;
        for (int i = 0; i < g->nghosts; i++) g->ghosts[i].alive = false;
        for (int i = 0; i < g->nhgoals; i++) g->hgoals[i].gone = true;
        g->vel = v2(0, 0);
        g->hit_visible = false;
        transition(g, PS_CUSTOM);
        h->vel = v2(0, 0);
        h->sx = h->sy = 0.25f;
        h->sat = 0;
        h->mode = HORSE_WIN;
        h->shaker += 1000;
        if (h->end_of_life) { // die_forever(): 12 s of explosions, then the level is won
            h->mode = HORSE_DEFEAT;
            h->defeat_t = 0;
            g->events |= EV_HDIE;
        }
        return;
    }
    h->shaker += 450;
}

// boss_level.gd win(), with the cat held where it is
static void boss_win(Game *g) {
    win(g);
    g->pstate = PS_CUSTOM;
    g->sprite_scale = 0.064f;
}

static void horse_tick(Game *g, const GameInput *in) {
    __typeof__(g->horse) *h = &g->horse;
    if (!h->on) return;
    h->shaker = lerpf(h->shaker, 0, 0.1f); // constantVisuals()
    switch (h->mode) {
    case HORSE_NORMAL:
        if (h->attack_timer <= h->target_delay && !h->targetted) {
            h->dir = v2sub(g->pos, h->pos);
            h->targetted = true;
        }
        if (h->attack_timer > 0) {
            h->attack_timer--;
            if (h->attack_timer < CHARGE_TIME) {
                h->vel = v2(lerpf(h->vel.x, 0, 0.03f), lerpf(h->vel.y, 0, 0.03f));
                h->sx = lerpf(h->sx, 0.3f, 0.03f);
                h->sy = lerpf(h->sy, 0.2f, 0.03f);
                h->sat = fminf(1, h->sat + 0.005f);
                h->shaker += 20;
            } else {
                float l = v2len(h->vel);
                if (l > h->base_speed) h->vel = v2mul(v2norm(h->vel), lerpf(l, h->base_speed, 0.03f));
                if (h->attack_timer == CHARGE_TIME) g->events |= EV_HCHARGEUP;
            }
        } else {
            h->targetted = false;
            h->vel = v2mul(v2norm(h->dir), h->base_speed * 3);
            h->attack_timer = h->attack_time;
            h->sx = h->sy = 0.25f;
            h->sat = 0;
            g->events |= EV_HCHARGE;
        }
        break;
    case HORSE_WIN: // a click sends it up; once it is off the top, the level is won
        if (in->click_pressed) h->vel.y = -600;
        h->vel.y -= 2;
        if (h->pos.y < -g->lv.bounds - 128) {
            h->on = false;
            boss_win(g);
            return;
        }
        break;
    case HORSE_DEFEAT:
        h->defeat_t++;
        if (h->defeat_t % 15 == 0) h->shaker += 250, g->events |= EV_HBOOMQ;
        if (h->defeat_t > 600) h->fade = fmaxf(0, 1 - (h->defeat_t - 600) / 120.0f);
        if (h->defeat_t >= 720) { // died_for_good -> win
            h->on = false;
            g->pstate = PS_CUSTOM;
            boss_win(g);
            return;
        }
        break;
    default:
        break;
    }
    if (h->mode == HORSE_WIN && h->defeat_t == 0 && (g->frame & 7) == 0) h->shaker += 250, g->events |= EV_HBOOMQ;
    vec2 motion = v2mul(h->vel, TICK * h->scale);
    if (h->mode == HORSE_WIN || h->mode == HORSE_DEFEAT) { // its body's shape is off: nothing stops it
        h->pos = v2add(h->pos, motion);
    } else {
        Collision c = phys_move_circle(&g->lv, &h->pos, HORSE_RADIUS * h->scale, motion, HORSE_MASK);
        if (c.hit) {
            g->events |= EV_HBOUNCE;
            h->vel = v2bounce(h->vel, c.normal);
        }
    }
    // horse_goal.gd _physics_process
    for (int i = 0; i < g->nhgoals; i++) {
        __typeof__(g->hgoals[0]) *hg = &g->hgoals[i];
        if (hg->gone) continue;
        Collision c = phys_move_circle(&g->lv, &hg->pos, HGOAL_RADIUS * hg->scale, v2mul(hg->vel, TICK * hg->scale),
                                       HORSE_MASK);
        if (!hg->charging) continue;
        if (v2len2(hg->vel) > 0) hg->rot = atan2f(hg->vel.y, hg->vel.x);
        hg->sx += 0.002f;
        hg->sy -= 0.001f;
        vec2 dir = v2norm(v2sub(h->pos, hg->pos));
        hg->vel = v2mul(v2limit(v2add(hg->vel, v2mul(dir, 10.0f)), 800.0f), 0.99f);
        if (c.hit) {
            hg->sx = 0.376f, hg->sy = 0.368f;
            hg->vel = v2bounce(hg->vel, c.normal);
        }
    }
    for (int i = 0; i < g->nmines; i++) { // a mine the horse runs over hurts it
        __typeof__(g->mines[0]) *m = &g->mines[i];
        if (m->boom > 0) m->boom -= TICK * 2;
        if (m->spent || h->mode != HORSE_NORMAL) continue;
        float rr = m->r + HORSE_RADIUS * h->scale;
        bool in = v2len2(v2sub(m->pos, h->pos)) < rr * rr;
        if (in && !m->hin) {
            m->spent = true;
            m->boom = 1;
            g->events |= EV_MINE;
            horse_hit(g);
        }
        m->hin = in;
    }
}

static void ghosts_tick(Game *g) {
    g->portal_rot += 0.1f;
    if (g->nuke_flash > 0) g->nuke_flash -= TICK * 2;
    for (int i = 0; i < g->nghosts; i++) {
        if (!g->ghosts[i].alive || g->ghosts[i].inactive || g->ghosts[i].frozen) continue;
        vec2 dir = v2norm(v2sub(g->pos, g->ghosts[i].pos));
        float sc = g->ghosts[i].scale; // final_ghost.gd: acceleration * scale, speed_limit * scale.x
        g->ghosts[i].vel = v2limit(v2add(g->ghosts[i].vel, v2mul(dir, GHOST_ACCEL * sc)), GHOST_LIMIT * sc);
        g->ghosts[i].pos = v2add(g->ghosts[i].pos, v2mul(g->ghosts[i].vel, TICK));
    }
}

// jump_pad.gd player_jump (call_deferred: after the frame's signals)
static void player_jump(Game *g, int p) {
    float d = g->pads[p].dir;
    vec2 dir = v2(cosf(d), sinf(d));
    if (v2len(g->vel) <= STOP_SPEED) g->vel = v2mul(dir, STOP_SPEED);
    float speed = g->pads[p].speed_granted <= STOP_SPEED ? v2len(g->vel) : g->pads[p].speed_granted;
    g->vel = v2mul(dir, speed + 10.0f); // JUMP_BOOST
    g->pos = g->pads[p].pos;
}

// Area2D overlaps, checked after everything moved (Godot flushes them after the physics step)
static void areas_tick(Game *g) {
    if (g->pstate == PS_WIN || g->pstate == PS_LOSE || g->pstate == PS_CUSTOM || g->pstate == PS_WINFAKE) return;
    if (g->pstate == PS_JUMP) {
        // collision_layer 0 in the air: every area lets the cat go, and notices it again on landing
        for (int z = 0; z < MAX_ZONES; z++) g->zone_in[z] = false;
        for (int p = 0; p < g->npads; p++) g->pads[p].in = false;
        g->sand_claimer = -1;
        return;
    }
    for (int i = 0; i < g->ncoins; i++) {
        if (g->coins[i].taken) continue;
        float rr = g->coins[i].r + PLAYER_RADIUS;
        if (v2len2(v2sub(g->coins[i].pos, g->pos)) < rr * rr) {
            g->coins[i].taken = true;
            g->coins[i].popup = 1.0f;
            bool token = g->coins[i].spr >= 0;
            g->events |= g->coins[i].nuke ? EV_NUKE : token ? EV_BOOST : EV_COIN;
            if (g->coins[i].nuke) { // the baddies are freed (no death, no score); spawners may refill
                for (int b = 0; b < g->nbodies; b++)
                    if (g->bodies[b].alive && (g->bodies[b].type == OBJ_SKULL || g->bodies[b].type == OBJ_CAR)) {
                        g->bodies[b].owner = -1;
                        body_remove(g, b);
                    }
                if (g->coins[i].nuke_spawners)
                    for (int s = 0; s < g->nspawners; s++) g->spawners[s].count = 0;
                g->nuke_flash = 1;
            }
            if (g->coins[i].does_speed) g->vel = v2mul(v2norm(g->vel), g->coins[i].set_speed);
            for (int s = 0; s < g->nslots; s++) // slot.gd stop(): collect_connect.body_entered
                if (g->slots[s].coin == i && g->slots[s].spinning) {
                    g->slots[s].spinning = false;
                    check_slots(g);
                }
            if (g->coins[i].mult) {
                g->level_bonus += g->coins[i].score + g->coins[i].multval * g->coin_multiplier;
                if (g->coin_multiplier <= 9) g->coin_multiplier++;
                if (g->coin_multiplier >= 6 && !token) g->thought = TH_MONEY;
            } else {
                g->level_bonus += g->coins[i].score;
                if (!token) g->thought = TH_MONEY; // tokens have no do_thought
            }
        }
    }
    // smiley_ring.gd collect(): the ring's coins come out and its timer starts
    for (int r = 0; r < g->nrings; r++) {
        __typeof__(g->rings[0]) *R = &g->rings[r];
        if (R->gone || R->running || R->after > 0) continue;
        float rr = R->r + PLAYER_RADIUS;
        if (v2len2(v2sub(R->pos, g->pos)) >= rr * rr) continue;
        R->running = true;
        R->left = R->time_given;
        for (int c = 0; c < g->nscoins; c++)
            if (g->scoins[c].ring == r) g->scoins[c].active = true;
        if (R->bell) g->events |= EV_RING;
    }
    // smiley_coin.gd collect()
    for (int c = 0; c < g->nscoins; c++) {
        __typeof__(g->scoins[0]) *S = &g->scoins[c];
        if (!S->active || S->ring < 0) continue;
        float rr = S->r + PLAYER_RADIUS;
        if (v2len2(v2sub(S->pos, g->pos)) >= rr * rr) continue;
        __typeof__(g->rings[0]) *R = &g->rings[S->ring];
        g->scoin_pitch = 1.0f + R->grabbed * 0.05f;
        g->events |= EV_SCOIN;
        R->grabbed++;
        S->count = R->grabbed;
        S->popup = POPUP_TIME;
        S->active = false;
    }
    // jump_pad.gd _on_body_entered
    for (int p = 0; p < g->npads; p++) {
        float rr = g->pads[p].r + PLAYER_RADIUS;
        bool in = v2len2(v2sub(g->pads[p].pos, g->pos)) < rr * rr;
        if (in && !g->pads[p].in) g->jump_pad = p;
        g->pads[p].in = in;
    }
    // key.gd collect(): the key joins player.has_keys
    for (int i = 0; i < g->nkeys; i++) {
        Key *k = &g->keys[i];
        if (!k->alive || k->held) continue;
        float rr = KEY_RADIUS + PLAYER_RADIUS;
        if (v2len2(v2sub(k->pos, g->pos)) < rr * rr && g->nheld < MAX_KEYS) {
            k->held = true;
            g->held[g->nheld++] = i;
            g->events |= EV_KEY;
            k->popup_score = k->score;
            g->level_bonus += k->score;
            k->popup = POPUP_TIME;
        }
    }
    // sand_pit.gd and water_death.gd (Area2D zones)
    for (int z = 0; z < g->lv.nzones && z < MAX_ZONES; z++) {
        const Shape *s = &g->lv.zones[z];
        bool in = phys_circle_overlaps(s, g->pos, PLAYER_RADIUS);
        bool entered = in && !g->zone_in[z], exited = !in && g->zone_in[z];
        g->zone_in[z] = in;
        if (s->layers == ZONE_WATER) {
            if (entered && g->set.can_swim) { // water_death.gd: swimming (P-14's sewer)
                g->events |= EV_SPLASH;
                g->thought = TH_SWIM;
            } else if (entered) {
                g->events |= EV_SPLASH;
                death(g, DEATH_WATER);
                return;
            }
        } else if (s->layers == ZONE_BOSSAPPEAR) {
            if (entered) { // boss_appear_event.gd: the fake stage clear
                transition(g, PS_WINFAKE);
                g->active = false;
                g->events |= EV_MUTE;
                return;
            }
        } else if (s->layers == ZONE_LIGHTS) {
            if (entered && !g->lights_done) { // the course goes dark, the cats go grey
                g->lights_done = g->lights_out = true;
                g->level_bonus += g->lights_score;
                g->events |= EV_LIGHTS;
                g->thought = TH_LIGHTS;
            }
        } else if (s->layers == ZONE_ASHBABY) {
            if (entered && !g->ash_done) {
                g->ash_done = true;
                g->level_bonus += g->ash_score;
                if (g->mode == MODE_NORMAL) g->events |= EV_ASHBABY; // not in demos (level.mode NORMAL only)
            }
        } else if (s->layers == ZONE_POLICE) {
            if (entered && g->police && !g->police_done) police_event(g);
        } else if (s->layers == ZONE_SAND) {
            if (entered && g->sand_claimer == -1) {
                g->sand_claimer = z;
                g->thought = TH_SAND;
            }
            if (exited && g->sand_claimer == z) g->sand_claimer = -1;
            if (in && (g->sand_claimer == z || g->sand_claimer == -1)) {
                g->sand_claimer = z;
                g->vel = v2(lerpf(g->vel.x, 0, 0.15f), lerpf(g->vel.y, 0, 0.15f));
            }
        }
    }
    // horse_goal.gd: the cat sets a goal off; a charging goal that reaches the horse hurts it
    for (int i = 0; i < g->nhgoals; i++) {
        __typeof__(g->hgoals[0]) *hg = &g->hgoals[i];
        if (hg->gone || !hg->active) continue;
        float rr = hg->r + PLAYER_RADIUS;
        bool pin = v2len2(v2sub(hg->pos, g->pos)) < rr * rr;
        bool p_entered = pin && !hg->pin;
        hg->pin = pin;
        bool hin = false, h_entered = false, already = false;
        if (g->horse.on && g->horse.mode != HORSE_WIN && g->horse.mode != HORSE_DEFEAT) {
            float hr = hg->r + HORSE_RADIUS * g->horse.scale;
            hin = v2len2(v2sub(hg->pos, g->horse.pos)) < hr * hr;
        }
        h_entered = hin && !hg->hin;
        hg->hin = hin;
        if (p_entered && !hg->charging) {
            hg->charging = true;
            g->events |= EV_HLAUNCH;
            already = hin;
        }
        if (hg->charging && (h_entered || already)) {
            if (hg->next >= 0 && hg->next < g->nhgoals) g->hgoals[hg->next].active = true;
            hg->gone = true;
            horse_hit(g);
            if (g->pstate == PS_CUSTOM) return;
        }
    }
    for (int i = 0; i < g->nmines; i++) { // land_mine.gd: the cat dies on one (WALL_STUCK)
        __typeof__(g->mines[0]) *m = &g->mines[i];
        if (m->spent) continue;
        float rr = m->r + PLAYER_RADIUS;
        if (v2len2(v2sub(m->pos, g->pos)) < rr * rr) {
            m->spent = true;
            m->boom = 1;
            g->events |= EV_MINE;
            death(g, DEATH_WALL);
            return;
        }
    }
    // horse_boss.gd Hitbox: a 74 x 74 square
    if (g->horse.on && g->horse.mode == HORSE_NORMAL) {
        float hb = HORSE_HITBOX * g->horse.scale;
        if (box_overlaps(g->horse.pos, v2(hb, hb), g->pos, PLAYER_RADIUS)) {
            death(g, DEATH_HORSE);
            return;
        }
    }
    // portal.gd teleport(): on entering, unless the cat just came through one
    for (int p = 0; p < g->nportals; p++) {
        __typeof__(g->portals[0]) *pt = &g->portals[p];
        bool in = box_overlaps(pt->pos, pt->half, g->pos, PLAYER_RADIUS);
        bool entered = in && !pt->in;
        pt->in = in;
        if (entered && !g->portal_sick && pt->target >= 0 && pt->target < g->nportals) {
            g->portal_sick_timer = 2;
            g->portal_sick = true;
            g->pos = g->portals[pt->target].pos;
            g->events |= EV_PORTAL;
            if (g->mode != MODE_DEMO) g->thought = TH_PORTAL;
        }
    }
    // fake_goal.gd _on_body_entered (once: the hitbox turns off)
    for (int f = 0; f < g->nfakegoals; f++) {
        __typeof__(g->fakegoals[0]) *fg = &g->fakegoals[f];
        if (fg->taken) continue;
        float rr = fg->r + PLAYER_RADIUS;
        if (v2len2(v2sub(fg->pos, g->pos)) >= rr * rr) continue;
        fg->taken = true;
        tiles_apply(g);
        if (fg->ghost && g->mode != MODE_DEMO) {
            for (int i = 0; i < g->nghosts; i++)
                if (g->ghosts[i].final) g->ghosts[i].inactive = false;
            g->events |= EV_FGHOST;
        }
        if (fg->alt) {
            g->alt_floor = true;
            g->events |= EV_FAKEALT;
        }
        g->vel = v2(0, 0);
        g->level_bonus += fg->score;
        fg->popup = 1.0f;
        g->events |= EV_FAKEGOAL;
    }
    // prism_gate.gd: its break hitbox (size + 2) shatters it, or says the playthrough is too poor
    for (int i = 0; i < g->nbodies; i++) {
        Body *b = &g->bodies[i];
        if (b->type != OBJ_PRISMGATE) continue;
        if (b->bulge > 0) b->bulge -= TICK * 3.2f;
        if (!b->alive) continue;
        bool in = box_overlaps(b->pos, v2(b->half.x + 1, b->half.y + 1), g->pos, PLAYER_RADIUS);
        bool entered = in && !b->touch;
        b->touch = in;
        if (!entered) continue;
        if (b->passable) {
            body_remove(g, i);
            g->events |= EV_SHATTER;
        } else {
            g->events |= EV_TOOPOOR;
            b->bulge = 1;
        }
    }
    for (int i = 0; i < g->nghosts; i++) {
        if (!g->ghosts[i].alive || g->ghosts[i].frozen) continue; // a node with processing off leaves physics
        float d2 = v2len2(v2sub(g->ghosts[i].pos, g->pos));
        float rr = g->ghosts[i].hit + PLAYER_RADIUS;
        if (d2 < rr * rr) {
            death(g, DEATH_NORMAL);
            return;
        }
        if (g->ghosts[i].final) continue; // final_ghost.tscn: its graze detector has no shape
        float gr = GHOST_GRAZE + PLAYER_RADIUS;
        bool graze = d2 < gr * gr;
        if (g->ghosts[i].graze && !graze) g->events |= EV_GRAZE;
        g->ghosts[i].graze = graze;
    }
    if (g->has_goal) {
        float rx = g->goal_r + PLAYER_RADIUS, ry = g->goal_ry + PLAYER_RADIUS;
        vec2 d = v2sub(g->pos, g->goal_pos);
        if ((d.x * d.x) / (rx * rx) + (d.y * d.y) / (ry * ry) < 1) {
            g->goal_target = g->goal_pos;
            win(g);
        }
    }
    if (g->jump_pad >= 0) {
        int p = g->jump_pad;
        g->jump_pad = -1;
        if (!g->victory && !g->lose) {
            transition(g, PS_JUMP);
            g->events |= EV_JUMP;
            g->thought = TH_JUMP;
            player_jump(g, p);
        }
    }
}

void game_tick(Game *g, const GameInput *in) {
    cur_in = in;
    g->events = 0;
    g->metro_trig = 0;
    g->thought = TH_NONE;
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

    phys_set_dynamic(g->body_shapes, g->nbodies);
    spawners_tick(g);
    slots_tick(g);
    player_tick(g, in);
    keys_tick(g);
    bodies_tick(g);
    doggoals_tick(g);
    horse_tick(g, in);
    tracks_tick(g);
    metronome_tick(g, in);
    ghosts_tick(g);
    rings_tick(g);
    areas_tick(g);
    for (int i = 0; i < g->ncoins; i++)
        if (g->coins[i].popup > 0) g->coins[i].popup -= TICK;
    for (int i = 0; i < g->nscoins; i++)
        if (g->scoins[i].popup > 0) g->scoins[i].popup -= TICK;
    for (int i = 0; i < g->nfakegoals; i++)
        if (g->fakegoals[i].popup > 0) g->fakegoals[i].popup -= TICK;
    if (g->active) g->time += TICK;
    g->frame++;
}
