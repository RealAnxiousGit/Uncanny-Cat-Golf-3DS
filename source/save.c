#include "save.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SD_DIR "sdmc:/3ds/UncannyCatGolf"

typedef struct {
    char sec[24], key[48], val[96];
} Entry;

struct Cfg {
    char path[96], tmp[100];
    Entry *e;
    int n, cap;
};

static bool cia_save; // using the CIA's save data archive
static char base[40];

void save_init(void) {
    cia_save = false;
    if (!envIsHomebrew()) {
        FS_Path empty = fsMakePath(PATH_EMPTY, "");
        Result r = archiveMount(ARCHIVE_SAVEDATA, empty, "save");
        if (R_FAILED(r)) { // first start: the save data isn't formatted yet
            FSUSER_FormatSaveData(ARCHIVE_SAVEDATA, empty, 0x200, 16, 16, 17, 17, true);
            r = archiveMount(ARCHIVE_SAVEDATA, empty, "save");
        }
        cia_save = R_SUCCEEDED(r);
    }
    if (cia_save) {
        snprintf(base, sizeof(base), "save:");
    } else {
        mkdir("sdmc:/3ds", 0777);
        mkdir(SD_DIR, 0777);
        snprintf(base, sizeof(base), SD_DIR);
    }
}

void save_exit(void) {
    if (cia_save) archiveUnmount("save");
}

static Entry *find(Cfg *c, const char *sec, const char *key) {
    for (int i = 0; i < c->n; i++)
        if (!strcmp(c->e[i].sec, sec) && !strcmp(c->e[i].key, key)) return &c->e[i];
    return NULL;
}

void cfg_set_str(Cfg *c, const char *sec, const char *key, const char *v) {
    Entry *e = find(c, sec, key);
    if (!e) {
        if (c->n == c->cap) {
            c->cap = c->cap ? c->cap * 2 : 64;
            c->e = realloc(c->e, c->cap * sizeof(Entry));
        }
        e = &c->e[c->n++];
        snprintf(e->sec, sizeof(e->sec), "%s", sec);
        snprintf(e->key, sizeof(e->key), "%s", key);
    }
    snprintf(e->val, sizeof(e->val), "%s", v);
}

Cfg *cfg_open(const char *name) {
    Cfg *c = calloc(1, sizeof(Cfg));
    snprintf(c->path, sizeof(c->path), "%s/%s", base, name);
    snprintf(c->tmp, sizeof(c->tmp), "%s/%s.new", base, name);
    FILE *f = fopen(c->path, "r");
    if (!f) return c;
    char line[200], sec[24] = "";
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) *end = 0;
            snprintf(sec, sizeof(sec), "%.23s", line + 1);
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq || line[0] == ';') continue;
        *eq = 0;
        char *v = eq + 1;
        size_t vl = strlen(v); // strings are stored quoted, like Godot does
        if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') {
            v[vl - 1] = 0;
            v++;
        }
        cfg_set_str(c, sec, line, v);
    }
    fclose(f);
    return c;
}

bool cfg_write(Cfg *c) {
    FILE *f = fopen(c->tmp, "w");
    if (!f) return false;
    // sections in first-seen order
    for (int i = 0; i < c->n; i++) {
        bool seen = false;
        for (int j = 0; j < i && !seen; j++) seen = !strcmp(c->e[j].sec, c->e[i].sec);
        if (seen) continue;
        fprintf(f, "%s[%s]\n\n", i ? "\n" : "", c->e[i].sec);
        for (int j = i; j < c->n; j++)
            if (!strcmp(c->e[j].sec, c->e[i].sec)) fprintf(f, "%s=%s\n", c->e[j].key, c->e[j].val);
    }
    bool ok = fclose(f) == 0;
    if (ok) {
        remove(c->path);
        ok = rename(c->tmp, c->path) == 0;
    }
    if (cia_save) archiveCommitSaveData("save");
    return ok;
}

int cfg_get_int(Cfg *c, const char *sec, const char *key, int def) {
    Entry *e = find(c, sec, key);
    if (!e) return def;
    if (!strcmp(e->val, "true")) return 1;
    if (!strcmp(e->val, "false")) return 0;
    return atoi(e->val);
}

float cfg_get_float(Cfg *c, const char *sec, const char *key, float def) {
    Entry *e = find(c, sec, key);
    return e ? strtof(e->val, NULL) : def;
}

const char *cfg_get_str(Cfg *c, const char *sec, const char *key, const char *def) {
    Entry *e = find(c, sec, key);
    if (!e) return def;
    return e->val;
}

void cfg_set_int(Cfg *c, const char *sec, const char *key, int v) {
    char b[16];
    snprintf(b, sizeof(b), "%d", v);
    cfg_set_str(c, sec, key, b);
}

void cfg_set_float(Cfg *c, const char *sec, const char *key, float v) {
    char b[32];
    snprintf(b, sizeof(b), "%g", v);
    cfg_set_str(c, sec, key, b);
}
