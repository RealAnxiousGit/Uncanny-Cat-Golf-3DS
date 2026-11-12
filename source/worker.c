#include "worker.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "save.h"
#include "thoughts.h"

static Thread th;
static volatile bool running;

// ---- reading ahead
enum { F_FREE, F_QUEUED, F_READING, F_DONE, F_FAILED };
typedef struct {
    char path[128];
    int st;
    FILE *f;
    char *buf;
    size_t n, got;
} Fetch;
#define MAX_FETCH 12
#define CHUNK (64 * 1024)
static Fetch fetch[MAX_FETCH];
static LightLock flock;
static bool flock_ready;

static void flock_init(void) {
    if (!flock_ready) LightLock_Init(&flock), flock_ready = true;
}

void prefetch(const char *path) {
    flock_init();
    LightLock_Lock(&flock);
    int slot = -1;
    for (int i = 0; i < MAX_FETCH; i++) {
        if (fetch[i].st != F_FREE && !strcmp(fetch[i].path, path)) slot = -2; // asked for already
        else if (slot == -1 && fetch[i].st == F_FREE) slot = i;
    }
    if (slot >= 0) {
        memset(&fetch[slot], 0, sizeof(Fetch));
        snprintf(fetch[slot].path, sizeof(fetch[slot].path), "%s", path);
        fetch[slot].st = F_QUEUED;
    }
    LightLock_Unlock(&flock);
}

// one piece of reading; false when there is nothing to read
static bool fetch_step(void) {
    if (!flock_ready) return false;
    Fetch *w = NULL;
    LightLock_Lock(&flock);
    for (int i = 0; i < MAX_FETCH && !w; i++)
        if (fetch[i].st == F_READING) w = &fetch[i];
    for (int i = 0; i < MAX_FETCH && !w; i++)
        if (fetch[i].st == F_QUEUED) w = &fetch[i], w->st = F_READING;
    LightLock_Unlock(&flock);
    if (!w) return false;
    // only this thread touches a READING entry's file and buffer
    if (!w->f) {
        w->f = fopen(w->path, "rb");
        if (w->f && !fseek(w->f, 0, SEEK_END)) {
            long len = ftell(w->f);
            fseek(w->f, 0, SEEK_SET);
            w->n = len > 0 ? (size_t)len : 0;
            w->buf = w->n ? malloc(w->n) : NULL;
        }
        if (!w->f || !w->buf) {
            if (w->f) fclose(w->f);
            w->f = NULL;
            LightLock_Lock(&flock);
            w->st = F_FAILED;
            LightLock_Unlock(&flock);
            return true;
        }
    }
    size_t want = w->n - w->got < CHUNK ? w->n - w->got : CHUNK;
    size_t r = fread(w->buf + w->got, 1, want, w->f);
    w->got += r;
    if (r < want || w->got >= w->n) {
        fclose(w->f);
        w->f = NULL;
        LightLock_Lock(&flock);
        w->st = w->got >= w->n ? F_DONE : F_FAILED;
        LightLock_Unlock(&flock);
    }
    return true;
}

static Fetch *find_fetch(const char *path) {
    for (int i = 0; i < MAX_FETCH; i++)
        if (fetch[i].st != F_FREE && !strcmp(fetch[i].path, path)) return &fetch[i];
    return NULL;
}

bool prefetch_ready(const char *path) {
    flock_init();
    LightLock_Lock(&flock);
    Fetch *f = find_fetch(path);
    bool r = !f || f->st == F_DONE || f->st == F_FAILED;
    LightLock_Unlock(&flock);
    return r;
}

void *prefetch_take(const char *path, size_t *size) {
    flock_init();
    for (;;) {
        LightLock_Lock(&flock);
        Fetch *f = find_fetch(path);
        if (!f) {
            LightLock_Unlock(&flock);
            return NULL;
        }
        if (f->st == F_DONE || f->st == F_FAILED) {
            void *b = f->st == F_DONE ? f->buf : NULL;
            if (!b) free(f->buf);
            *size = f->n;
            f->buf = NULL;
            f->st = F_FREE;
            LightLock_Unlock(&flock);
            return b;
        }
        LightLock_Unlock(&flock);
        if (running) svcSleepThread(1000 * 1000);
        else fetch_step(); // no worker: read it here
    }
}

void prefetch_drop(void) {
    flock_init();
    for (int i = 0; i < MAX_FETCH; i++) {
        LightLock_Lock(&flock);
        bool idle = fetch[i].st == F_DONE || fetch[i].st == F_FAILED || fetch[i].st == F_QUEUED;
        if (idle) {
            free(fetch[i].buf);
            fetch[i].buf = NULL;
            fetch[i].st = F_FREE;
        }
        LightLock_Unlock(&flock);
    }
}

// ---- the thread
static void loop(void *arg) {
    (void)arg;
    while (running) {
        bool busy = audio_bg_step();
        busy = thoughts_bg_step() || busy;
        busy = save_bg_step() || busy;
        busy = fetch_step() || busy;
        if (!busy) svcSleepThread(2 * 1000 * 1000); // 2 ms
    }
}

void worker_start(void) {
    flock_init();
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    running = true;
    audio_bg_set(true);
    thoughts_bg_set(true);
    save_async(true);
    // core 2 is the New 3DS's spare core; elsewhere it shares the game's core, and its lower
    // priority lets it run while the game waits for the screen
    th = threadCreate(loop, NULL, 64 * 1024, prio + 1, 2, false);
    if (!th) th = threadCreate(loop, NULL, 64 * 1024, prio + 1, -2, false);
    if (!th) { // no thread: everything goes back to the game thread
        running = false;
        audio_bg_set(false);
        thoughts_bg_set(false);
        save_async(false);
    }
}

void worker_stop(void) {
    if (!th) return;
    running = false;
    threadJoin(th, U64_MAX);
    threadFree(th);
    th = NULL;
    audio_bg_set(false);
    thoughts_bg_set(false);
    save_async(false);
    while (save_bg_step()) {}
    prefetch_drop();
    for (int i = 0; i < MAX_FETCH; i++) // a file the thread was still reading
        if (fetch[i].f) fclose(fetch[i].f), fetch[i].f = NULL, free(fetch[i].buf), fetch[i].buf = NULL,
                        fetch[i].st = F_FREE;
}
