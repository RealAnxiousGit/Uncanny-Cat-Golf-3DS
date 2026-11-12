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
    uint32_t events;
    bool lose, win, sand;
    int death, frames, keys_held, bodies_alive;
    vec2 end;
    float sand_speed_in, sand_speed_next; // speed on the sand tick and the tick after
} Run;

static int alive_bodies(const Game *g) {
    int n = 0;
    for (int i = 0; i < g->nbodies; i++) n += g->bodies[i].alive;
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
        if (g.strokes >= n && r.frames - last_shot_frame > 30 && g.vel.x == 0 && g.vel.y == 0) break;
    }
    r.lose = g.lose;
    r.win = g.victory;
    r.death = g.death_type;
    r.keys_held = g.nheld;
    r.bodies_alive = alive_bodies(&g);
    r.end = g.pos;
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
        printf("%-5s dblock %d keydoor %d key %d skull %d sand %d water %d\n", lv.id, nobj[OBJ_DBLOCK],
               nobj[OBJ_KEYDOOR], nobj[OBJ_KEY], nobj[OBJ_SKULL], nsand, nwater);
        bool want_db = nobj[OBJ_DBLOCK], want_door = nobj[OBJ_KEYDOOR] && nobj[OBJ_KEY], want_key = nobj[OBJ_KEY],
             want_skull = nobj[OBJ_SKULL], want_sand = nsand, want_water = nwater;
        bool got_db = false, got_key = false, got_door = false, got_skull = false, got_sand = false, got_water = false;
        DemoShot key_shot = {0};
        for (int p = 0; p < NP; p++)
            for (int a = 0; a < 360; a += 3) {
                DemoShot s = shot(a, p);
                Run r = play(&lv, &s, 1);
                if (want_db && !got_db && (r.events & EV_DBLOCK)) {
                    got_db = true;
                    printf("   dblock : angle %3d power %4.0f -> hit, blocks left %d\n", a, s.power, r.bodies_alive);
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
        MISS(want_skull, got_skull, "skull");
        MISS(want_water, got_water, "water");
        MISS(want_sand, got_sand, "sand");
        MISS(want_key, got_key, "key");
        MISS(want_door && got_key, got_door, "door");
        level_free(&lv);
    }
    printf("\nfailures: %d\n", fails);
    return fails != 0;
}
