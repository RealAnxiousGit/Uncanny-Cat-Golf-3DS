#include "data.h"

#include <stdio.h>
#include <string.h>

static const char *root = "romfs:";

static bool has_game(const char *r) {
    char p[96];
    snprintf(p, sizeof(p), "%s/levels/index.txt", r);
    FILE *f = fopen(p, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

// ucg-data.txt (written by tools/convert): the game version it was made from and the data format
static bool fits(const char *r) {
    char p[96], line[96], want_v[64], want_f[32];
    snprintf(p, sizeof(p), "%s/ucg-data.txt", r);
    snprintf(want_v, sizeof(want_v), "version %s", UCG_GAME_VERSION);
    snprintf(want_f, sizeof(want_f), "format %d", DATA_FORMAT);
    FILE *f = fopen(p, "r");
    if (!f) return false; // made by a converter from before the stamp
    bool v = false, fm = false;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        v = v || !strcmp(line, want_v);
        fm = fm || !strcmp(line, want_f);
    }
    fclose(f);
    return v && fm;
}

int data_find(void) {
    // the SD card first, so a folder the player made wins over a test build's own copy
    static const char *const ROOTS[] = {DATA_SD, "romfs:"};
    int found = DATA_MISSING;
    for (int i = 0; i < 2; i++)
        if (has_game(ROOTS[i])) {
            if (fits(ROOTS[i])) {
                root = ROOTS[i];
                return DATA_OK;
            }
            found = DATA_WRONG_VERSION;
        }
    return found;
}

const char *data_root(void) { return root; }

const char *dp(const char *rel) {
    static char buf[8][160];
    static int next;
    char *b = buf[next];
    next = (next + 1) % 8;
    snprintf(b, sizeof(buf[0]), "%s/%s", root, rel);
    return b;
}
