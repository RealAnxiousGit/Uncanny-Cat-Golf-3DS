#include "worker.h"

#include <3ds.h>

#include "audio.h"
#include "save.h"
#include "thoughts.h"

static Thread th;
static volatile bool running;

static void loop(void *arg) {
    (void)arg;
    while (running) {
        bool busy = audio_bg_step();
        busy = thoughts_bg_step() || busy;
        busy = save_bg_step() || busy;
        if (!busy) svcSleepThread(2 * 1000 * 1000); // 2 ms
    }
}

void worker_start(void) {
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
}
