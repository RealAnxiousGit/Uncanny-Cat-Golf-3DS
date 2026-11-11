// PC physics check: replays the game's attract demos with the port's game code
// and reports how each one ends. Usage: ucgsim <romfs dir> [world filter]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/game.h"

static char level_names[8][32][16];
static int level_count[8];

static void load_index(const char *romfs) {
    char path[512], line[2048];
    snprintf(path, sizeof(path), "%s/levels/index.txt", romfs);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "no %s\n", path); exit(1); }
    while (fgets(line, sizeof(line), f)) {
        int w, n, off;
        if (sscanf(line, "world %d %d %n", &w, &n, &off) < 2 || w < 0 || w > 7) continue;
        char *p = line + off;
        for (int i = 0; i < n && i < 32; i++) {
            int used;
            sscanf(p, "%15s%n", level_names[w][i], &used);
            p += used;
        }
        level_count[w] = n;
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: ucgsim <romfs> [world]\n"); return 2; }
    const char *romfs = argv[1];
    int only = argc > 3 ? atoi(argv[3]) : -1;
    load_index(romfs);
    char path[512], line[4096];
    snprintf(path, sizeof(path), "%s/demos/index.txt", romfs);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "no demos\n"); return 1; }
    static const char *DEATHS[] = {"caught by uncanny", "out of bounds", "backrooms", "stuck in wall", "horse", "water"};
    int checked = 0, wins = 0;
    while (fgets(line, sizeof(line), f)) {
        char name[64];
        int w, l, dt, n, off;
        if (sscanf(line, "%63s %d %d %d %d %n", name, &w, &l, &dt, &n, &off) < 5) continue;
        if (only >= 0 && w != only) continue;
        if (w > 7 || l >= level_count[w]) {
            printf("%-18s level not converted, skipped\n", name);
            continue;
        }
        static Game g;
        memset(&g, 0, sizeof(g));
        char *p = line + off;
        g.ndemo = n < MAX_DEMO_SHOTS ? n : MAX_DEMO_SHOTS;
        for (int i = 0; i < g.ndemo; i++) {
            int used;
            sscanf(p, "%d %f %f %f%n", &g.demo[i].wait_time, &g.demo[i].power, &g.demo[i].dir.x, &g.demo[i].dir.y, &used);
            p += used;
        }
        LevelDef lv;
        snprintf(path, sizeof(path), "%s/levels/w%d/%s.txt", romfs, w, level_names[w][l]);
        if (!level_load(&lv, path)) { printf("%-18s cannot load %s\n", name, path); continue; }
        GameSettings set = {1.0f, false, true};
        game_start(&g, &lv, MODE_DEMO, &set);
        GameInput in;
        memset(&in, 0, sizeof(in));
        int frames = 0, bumps = 0;
        int limit = dt > 0 ? dt * 3 : 3000;
        while (frames < limit && !g.victory && !g.lose) {
            game_tick(&g, &in);
            if (g.events & EV_BUMP) bumps++;
            frames++;
        }
        checked++;
        const char *how = g.victory ? "WIN" : g.lose ? DEATHS[g.death_type] : "still rolling";
        if (g.victory) wins++;
        printf("%-18s %s %-3s  %-16s shots %d/%d  frames %4d (demo_time %d)  bumps %d  end (%.1f, %.1f)\n",
               name, lv.id, "", how, g.strokes, g.ndemo, frames, dt, bumps, g.pos.x, g.pos.y);
        level_free(&lv);
    }
    fclose(f);
    printf("\n%d demos replayed, %d reached the hole\n", checked, wins);
    return 0;
}
