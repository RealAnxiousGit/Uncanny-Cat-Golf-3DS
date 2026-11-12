// PC gimmick check: for each level of a world, tries one shot in every direction (and a
// second shot after a key pickup) and reports which gimmicks the port's game code reacts to.
// Usage: gimtest <romfs dir> <world> [level number, 1-based]
// Ghosts are switched off so a slow roll is not cut short by the uncanny.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/game.h"

static char names[32][16];
static int count;

static void load_index(const char *romfs, int world) {
    char path[512], line[2048];
    snprintf(path, sizeof(path), "%s/levels/index.txt", romfs);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "no %s\n", path); exit(1); }
    while (fgets(line, sizeof(line), f)) {
        int w, n, off;
        if (sscanf(line, "world %d %d %n", &w, &n, &off) < 2 || w != world) continue;
        char *p = line + off;
        for (int i = 0; i < n && i < 32; i++) {
            int used;
            sscanf(p, "%15s%n", names[i], &used);
            p += used;
        }
        count = n;
    }
    fclose(f);
}

typedef struct {
    uint64_t events;
    bool lose, win, sand;
    int death, frames, keys_held, bodies_alive;
    vec2 end;
    float sand_speed_in, sand_speed_next; // speed on the sand tick and the tick after
    float boost_speed;                    // speed right after a token
    float jump_scale;                     // biggest sprite scale while jumping
    bool jump_landed;                     // back to rolling after the jump
    int cars_max;                         // most cars on the course at once
    float chase_max;                      // fastest chaser
    int switch_state, groups_on, group_shapes_on; // tile layers at the end
    int scoins;                           // smiley coins taken
    bool dog_moved;
} Run;

static int alive_bodies(const Game *g) {
    int n = 0;
    for (int i = 0; i < g->nbodies; i++) // blocks and doors only: spawned baddies come and go
        n += g->bodies[i].alive && (g->bodies[i].type == OBJ_DBLOCK || g->bodies[i].type == OBJ_KEYDOOR);
    return n;
}

// plays shots[0..n) from the level start, each fired when the cat is at rest
static Run play_at(const LevelDef *lv, const DemoShot *shots, int n, const vec2 *start) {
    static Game g;
    memset(&g, 0, sizeof(g));
    g.ndemo = n;
    memcpy(g.demo, shots, sizeof(DemoShot) * n);
    GameSettings set = {1.0f, false, true};
    game_start(&g, lv, MODE_DEMO, &set);
    for (int i = 0; i < g.nghosts; i++) g.ghosts[i].alive = false;
    if (start) g.pos = *start;
    GameInput in;
    memset(&in, 0, sizeof(in));
    Run r;
    memset(&r, 0, sizeof(r));
    int last_shot_frame = 0;
    bool sand_next = false;
    for (r.frames = 0; r.frames < 3000 && !g.victory && !g.lose; r.frames++) {
        int sc = g.sand_claimer;
        game_tick(&g, &in);
        r.events |= g.events;
        if (sand_next) { r.sand_speed_next = sqrtf(v2len2(g.vel)); sand_next = false; }
        if (sc < 0 && g.sand_claimer >= 0 && !r.sand) {
            r.sand = true;
            r.sand_speed_in = sqrtf(v2len2(g.vel));
            sand_next = true;
        }
        if (g.events & EV_LAUNCH) last_shot_frame = r.frames;
        if ((g.events & EV_BOOST) && !(r.events & EV_JUMP)) r.boost_speed = sqrtf(v2len2(g.vel));
        if (g.pstate == PS_JUMP && g.sprite_scale > r.jump_scale) r.jump_scale = g.sprite_scale;
        if ((r.events & EV_JUMP) && g.pstate == PS_MOVING) r.jump_landed = true;
        int cars = 0;
        for (int i = 0; i < g.nbodies; i++) cars += g.bodies[i].alive && (g.bodies[i].type == OBJ_CAR || g.bodies[i].owner >= 0);
        if (cars > r.cars_max) r.cars_max = cars;
        for (int i = 0; i < g.nbodies; i++) {
            const Body *b = &g.bodies[i];
            if (b->alive && b->chaser && v2len(b->vel) > r.chase_max) r.chase_max = v2len(b->vel);
            if (b->type == OBJ_DOG && v2len2(b->vel) > 0) r.dog_moved = true;
        }
        if (g.events & EV_SCOIN) r.scoins++;
        bool ring_busy = false; // let a started ring finish
        for (int i = 0; i < g.nrings; i++) ring_busy |= g.rings[i].running || g.rings[i].after > 0;
        if (!ring_busy && g.strokes >= n && r.frames - last_shot_frame > 30 && g.vel.x == 0 && g.vel.y == 0) break;
    }
    r.lose = g.lose;
    r.win = g.victory;
    r.death = g.death_type;
    r.keys_held = g.nheld;
    r.bodies_alive = alive_bodies(&g);
    r.end = g.pos;
    r.switch_state = g.switch_state;
    for (int t = 0; t < g.ntiles; t++) r.groups_on += g.tiles[t].on;
    for (int i = 0; i < g.lv.nshapes; i++) r.group_shapes_on += g.lv.shapes[i].group && g.lv.shapes[i].layers;
    return r;
}

static Run play(const LevelDef *lv, const DemoShot *shots, int n) { return play_at(lv, shots, n, NULL); }

static const float POWERS[] = {300, 500, 700, 900, 1200};
#define NP (int)(sizeof(POWERS) / sizeof(POWERS[0]))

static DemoShot shot(int a, int p) {
    DemoShot s;
    s.wait_time = 10;
    s.power = POWERS[p];
    s.dir = v2(cosf(a * 3.14159265f / 180), sinf(a * 3.14159265f / 180));
    return s;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: gimtest <romfs> <world> [level]\n"); return 2; }
    const char *romfs = argv[1];
    int world = atoi(argv[2]), only = argc > 3 ? atoi(argv[3]) : 0;
    load_index(romfs, world);
    int fails = 0;
    for (int l = 0; l < count; l++) {
        if (only && l + 1 != only) continue;
        char path[600];
        snprintf(path, sizeof(path), "%s/levels/w%d/%s.txt", romfs, world, names[l]);
        LevelDef lv;
        if (!level_load(&lv, path)) { printf("%s: cannot load\n", path); fails++; continue; }
        int nobj[OBJ_COUNT] = {0}, nsand = 0, nwater = 0;
        for (int i = 0; i < lv.nobjs; i++) nobj[lv.objs[i].type]++;
        for (int i = 0; i < lv.nzones; i++) {
            nsand += lv.zones[i].layers == ZONE_SAND;
            nwater += lv.zones[i].layers == ZONE_WATER;
        }
        for (int i = 0; i < lv.nzones; i++)
            if (lv.zones[i].layers == ZONE_WATER)
                printf("      water zone %d: kind %d, %d points, box %.0f,%.0f .. %.0f,%.0f\n", i, lv.zones[i].kind,
                       lv.zones[i].n, lv.zones[i].x0, lv.zones[i].y0, lv.zones[i].x1, lv.zones[i].y1);
        printf("%-5s dblock %d keydoor %d key %d skull %d sand %d water %d token %d jumppad %d spawner %d car %d\n",
               lv.id, nobj[OBJ_DBLOCK], nobj[OBJ_KEYDOOR], nobj[OBJ_KEY], nobj[OBJ_SKULL], nsand, nwater,
               nobj[OBJ_TOKEN], nobj[OBJ_JUMPPAD], nobj[OBJ_SPAWNER], nobj[OBJ_CAR]);
        // spawners with max_baddies 0 (2-17's slot-machine skulls) wait for the jackpot
        int live_spawners = 0;
        for (int i = 0; i < lv.nobjs; i++)
            live_spawners += lv.objs[i].type == OBJ_SPAWNER && obj_param(&lv.objs[i], "max_baddies", 0) > 0;
        bool want_token = nobj[OBJ_TOKEN], want_pad = nobj[OBJ_JUMPPAD], want_cars = live_spawners || nobj[OBJ_CAR];
        bool got_token = false, got_pad = false, got_car = false, saw_cars = false;
        bool want_db = nobj[OBJ_DBLOCK], want_door = nobj[OBJ_KEYDOOR] && nobj[OBJ_KEY], want_key = nobj[OBJ_KEY],
             want_skull = nobj[OBJ_SKULL], want_sand = nsand, want_water = nwater;
        bool got_db = false, got_key = false, got_door = false, got_skull = false, got_sand = false, got_water = false;
        // World 3
        int ngroup_shapes = 0;
        for (int i = 0; i < lv.nshapes; i++) ngroup_shapes += lv.shapes[i].group != 0;
        if (nobj[OBJ_TILES] || nobj[OBJ_SWITCH] || nobj[OBJ_DOG] || nobj[OBJ_RING] || nobj[OBJ_SCOIN])
            printf("      tiles %d (%d shapes) switch %d dog %d doggoal %d ring %d scoin %d\n", nobj[OBJ_TILES],
                   ngroup_shapes, nobj[OBJ_SWITCH], nobj[OBJ_DOG], nobj[OBJ_DOGGOAL], nobj[OBJ_RING], nobj[OBJ_SCOIN]);
        int nchasers = 0;
        for (int i = 0; i < lv.nobjs; i++)
            nchasers += (lv.objs[i].type == OBJ_SKULL && obj_param(&lv.objs[i], "chaser", 0)) ||
                        (lv.objs[i].type == OBJ_SPAWNER && obj_param(&lv.objs[i], "enemy", 0) == 2);
        bool want_switch = nobj[OBJ_SWITCH], want_ring = nobj[OBJ_RING], want_dog = nobj[OBJ_DOG],
             want_chase = nchasers > 0;
        bool got_switch = false, got_ring = false, got_ringwin = false, got_dogwin = false, got_chase = false,
             got_chuckle = false, dog_moves = false;
        if (nobj[OBJ_TILES] && !ngroup_shapes) printf("   tiles  : NO SHAPES IN THE GROUPS\n"), fails++;
        if (nobj[OBJ_SCOIN] && !nobj[OBJ_RING]) printf("   scoin  : NO RING\n"), fails++;
        for (int i = 0; i < lv.nobjs; i++)
            if (lv.objs[i].type == OBJ_SCOIN && obj_param(&lv.objs[i], "ring", -1) < 0)
                printf("   scoin  : coin %d HAS NO RING\n", i), fails++;
        DemoShot key_shot = {0};
        for (int p = 0; p < NP; p++)
            for (int a = 0; a < 360; a += 3) {
                DemoShot s = shot(a, p);
                Run r = play(&lv, &s, 1);
                if (want_db && !got_db && (r.events & EV_DBLOCK)) {
                    got_db = true;
                    printf("   dblock : angle %3d power %4.0f -> hit, blocks left %d\n", a, s.power, r.bodies_alive);
                }
                if (want_token && !got_token && (r.events & EV_BOOST)) {
                    got_token = true;
                    printf("   token  : angle %3d power %4.0f -> speed %.1f after it (speed token 875, stop 0)\n", a,
                           s.power, r.boost_speed);
                    if (!(fabsf(r.boost_speed - 875) < 1 || r.boost_speed < 1)) fails++;
                }
                if (want_pad && !got_pad && (r.events & EV_JUMP)) {
                    got_pad = true;
                    printf("   jump   : angle %3d power %4.0f -> jumped, scale up to %.3f, %s\n", a, s.power,
                           r.jump_scale, r.jump_landed ? "landed and rolled on" : "NEVER LANDED");
                    if (!r.jump_landed || r.jump_scale < 0.08f) fails++;
                }
                if (want_cars && r.cars_max > 0 && !saw_cars) {
                    saw_cars = true;
                    printf("   spawns : up to %d cars/skulls on the course at once\n", r.cars_max);
                }
                if (want_cars && !got_car && (r.events & EV_CAR)) {
                    got_car = true;
                    printf("   car    : angle %3d power %4.0f -> hit one, %s\n", a, s.power,
                           r.lose ? "cat died after" : "cat carried on");
                }
                if (want_skull && !got_skull && (r.events & EV_SKULL)) {
                    got_skull = true;
                    printf("   skull  : angle %3d power %4.0f -> killed, %s\n", a, s.power,
                           r.lose ? "cat died after" : "cat carried on");
                }
                if (want_water && !got_water && (r.events & EV_SPLASH)) {
                    got_water = true;
                    printf("   water  : angle %3d power %4.0f -> splash, %s (death %d, water death event %s)\n", a,
                           s.power, r.lose ? "lost" : "NOT LOST", r.death,
                           (r.events & EV_WATER_DEATH) ? "yes" : "no");
                    if (!r.lose || r.death != DEATH_WATER) fails++;
                }
                if (want_sand && !got_sand && r.sand) {
                    got_sand = true;
                    printf("   sand   : angle %3d power %4.0f -> claimed, speed %.1f then %.1f (ratio %.3f, want 0.85)\n",
                           a, s.power, r.sand_speed_in, r.sand_speed_next,
                           r.sand_speed_in > 0 ? r.sand_speed_next / r.sand_speed_in : 0);
                }
                if (want_switch && !got_switch && (r.events & EV_SWITCH)) {
                    got_switch = true;
                    printf("   switch : angle %3d power %4.0f -> hit, state %d, layers on %d, their shapes on %d\n", a,
                           s.power, r.switch_state, r.groups_on, r.group_shapes_on);
                }
                if (want_ring && !got_ring && (r.events & EV_RING)) {
                    got_ring = true;
                    printf("   ring   : angle %3d power %4.0f -> started, %d coins taken, %s\n", a, s.power, r.scoins,
                           (r.events & EV_RINGWIN) ? "passed" : (r.events & EV_RINGFAIL) ? "failed (time)" : "NO RESULT");
                    if (!(r.events & (EV_RINGWIN | EV_RINGFAIL))) fails++;
                }
                if (want_ring && !got_ringwin && (r.events & EV_RINGWIN)) {
                    got_ringwin = true;
                    printf("   ring   : angle %3d power %4.0f -> PASSED with %d coins\n", a, s.power, r.scoins);
                }
                if (want_dog && r.dog_moved) dog_moves = true;
                if (want_dog && !got_dogwin && (r.events & EV_DOGWIN)) {
                    got_dogwin = true;
                    printf("   dog    : angle %3d power %4.0f -> dog in the goal, layers on %d\n", a, s.power, r.groups_on);
                }
                if (want_chase && !got_chase && r.chase_max > 0) {
                    got_chase = true;
                    printf("   chase  : chasers move, up to %.1f (limit 200)\n", r.chase_max);
                    if (r.chase_max > 200.5f) fails++;
                }
                if (want_chase && !got_chuckle && (r.events & EV_CHUCKLE)) {
                    got_chuckle = true;
                    printf("   chuckle: angle %3d power %4.0f -> killed, %s\n", a, s.power,
                           r.lose ? "cat died after" : "cat carried on");
                }
                if (want_key && !got_key && (r.events & EV_KEY) && !r.lose) {
                    got_key = true;
                    key_shot = s;
                    printf("   key    : angle %3d power %4.0f -> picked up, held %d\n", a, s.power, r.keys_held);
                }
            }
        if (want_door && got_key) {
            int before = 0;
            {
                Run r0 = play(&lv, &key_shot, 1);
                before = r0.bodies_alive;
            }
            for (int p = 0; p < NP && !got_door; p++)
                for (int a = 0; a < 360 && !got_door; a += 3) {
                    DemoShot two[2] = {key_shot, shot(a, p)};
                    Run r = play(&lv, two, 2);
                    if (r.events & EV_KEYDOOR) {
                        got_door = true;
                        printf("   door   : key shot, then angle %3d power %4.0f -> opened, held %d, bodies %d -> %d\n",
                               a, two[1].power, r.keys_held, before, r.bodies_alive);
                        if (r.bodies_alive >= before) fails++;
                    }
                }
        }
        if (want_water && !got_water) { // two shots, coarse
            for (int p1 = 0; p1 < NP && !got_water; p1 += 2)
                for (int a1 = 0; a1 < 360 && !got_water; a1 += 15)
                    for (int p2 = 0; p2 < NP && !got_water; p2 += 2)
                        for (int a2 = 0; a2 < 360 && !got_water; a2 += 15) {
                            DemoShot two[2] = {shot(a1, p1), shot(a2, p2)};
                            Run r = play(&lv, two, 2);
                            if (r.events & EV_SPLASH) {
                                got_water = true;
                                printf("   water  : two shots (%d,%.0f) (%d,%.0f) -> splash, %s (death %d)\n", a1,
                                       two[0].power, a2, two[1].power, r.lose ? "lost" : "NOT LOST", r.death);
                                if (!r.lose || r.death != DEATH_WATER) fails++;
                            }
                        }
        }
        for (int i = 0; i < lv.nzones && want_water; i++) { // dropped straight into each pool
            if (lv.zones[i].layers != ZONE_WATER) continue;
            vec2 c = v2((lv.zones[i].x0 + lv.zones[i].x1) / 2, (lv.zones[i].y0 + lv.zones[i].y1) / 2);
            DemoShot none = shot(0, 0);
            Run r = play_at(&lv, &none, 0, &c);
            printf("   water  : cat placed in pool %d -> %s, death %d\n", i, r.lose ? "lost" : "NOT LOST", r.death);
            if (!r.lose || r.death != DEATH_WATER) fails++;
        }
#define MISS(w, g, n) if (w && !g) printf("   %-6s : no shot found\n", n)
        MISS(want_db, got_db, "dblock");
        MISS(want_token, got_token, "token");
        MISS(want_pad, got_pad, "jump");
        MISS(want_cars && nobj[OBJ_CAR] + live_spawners > 0 && got_car == false && saw_cars, got_car, "car");
        if (want_cars && !saw_cars) printf("   cars   : NONE SPAWNED\n"), fails++;
        MISS(want_skull, got_skull, "skull");
        MISS(want_water, got_water, "water");
        MISS(want_sand, got_sand, "sand");
        MISS(want_key, got_key, "key");
        MISS(want_door && got_key, got_door, "door");
        MISS(want_switch, got_switch, "switch");
        MISS(want_ring, got_ring, "ring");
        MISS(want_dog, got_dogwin, "dog");
        if (want_dog) printf("   dog    : %s\n", dog_moves ? "dogs move" : "dogs stay put (speed 0)");
        if (want_chase && !got_chase) printf("   chase  : CHASERS NEVER MOVED\n"), fails++;
        MISS(want_chase, got_chuckle, "chuckle");
        level_free(&lv);
    }
    printf("\nfailures: %d\n", fails);
    return fails != 0;
}
