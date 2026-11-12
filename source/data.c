#include "data.h"

#include <stdio.h>

static const char *root = "romfs:";

static bool has_game(const char *r) {
    char p[96];
    snprintf(p, sizeof(p), "%s/levels/index.txt", r);
    FILE *f = fopen(p, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

bool data_find(void) {
    // the SD card first, so a folder made from a newer game version wins over a test build's copy
    static const char *const ROOTS[] = {DATA_SD, "romfs:"};
    for (int i = 0; i < 2; i++)
        if (has_game(ROOTS[i])) {
            root = ROOTS[i];
            return true;
        }
    return false;
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
