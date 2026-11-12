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

// the whole file as text (cheap: it is all in memory)
static char *cfg_text(Cfg *c, size_t *len) {
    size_t cap = 256 + (size_t)c->n * 180, n = 0;
    char *t = malloc(cap);
    if (!t) return NULL;
    for (int i = 0; i < c->n; i++) {
        bool seen = false;
        for (int j = 0; j < i && !seen; j++) seen = !strcmp(c->e[j].sec, c->e[i].sec);
        if (seen) continue;
        if (n < cap) n += snprintf(t + n, cap - n, "%s[%s]\n\n", i ? "\n" : "", c->e[i].sec);
        for (int j = i; j < c->n; j++)
            if (!strcmp(c->e[j].sec, c->e[i].sec) && n < cap) n += snprintf(t + n, cap - n, "%s=%s\n", c->e[j].key, c->e[j].val);
    }
    *len = n < cap ? n : cap - 1;
    return t;
}

static bool write_file(const char *path, const char *tmp, const char *t, size_t len) {
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    bool ok = fwrite(t, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (cia_save) archiveCommitSaveData("save");
    return ok;
}

bool cfg_write(Cfg *c) {
    size_t len;
    char *t = cfg_text(c, &len);
    if (!t) return false;
    bool ok = write_file(c->path, c->tmp, t, len);
    free(t);
    return ok;
}

// Writes from the worker thread: the text is made now, the SD card write happens there.
// A newer write of the same file replaces one still waiting.
typedef struct {
    Cfg *c;
    char *text;
    size_t len;
} Pending;
#define MAX_PENDING 4
static Pending pending[MAX_PENDING];
static LightLock plock;
static bool plock_ready, async_on;

void save_async(bool on) {
    if (!plock_ready) LightLock_Init(&plock), plock_ready = true;
    async_on = on;
}

void cfg_write_later(Cfg *c) {
    if (!async_on) {
        cfg_write(c);
        return;
    }
    size_t len;
    char *t = cfg_text(c, &len);
    if (!t) return;
    LightLock_Lock(&plock);
    int slot = -1;
    for (int i = 0; i < MAX_PENDING; i++)
        if (pending[i].c == c || (slot < 0 && !pending[i].c)) slot = i;
    if (slot < 0) { // full: should not happen with two files
        LightLock_Unlock(&plock);
        write_file(c->path, c->tmp, t, len);
        free(t);
        return;
    }
    free(pending[slot].c == c ? pending[slot].text : NULL);
    pending[slot] = (Pending){c, t, len};
    LightLock_Unlock(&plock);
}

bool save_bg_step(void) {
    if (!plock_ready) return false;
    Pending p = {0};
    LightLock_Lock(&plock);
    for (int i = 0; i < MAX_PENDING && !p.c; i++)
        if (pending[i].c) p = pending[i], pending[i] = (Pending){0};
    LightLock_Unlock(&plock);
    if (!p.c) return false;
    write_file(p.c->path, p.c->tmp, p.text, p.len);
    free(p.text);
    return true;
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
