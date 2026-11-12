// Frame profiler for finding stutters. Built in only with `make PROFILE=1`; otherwise every
// macro is empty. Each frame is split into named parts (PROF_MARK) and named calls inside them
// (PROF_BEGIN/PROF_END). A frame whose work goes over PROF_BUDGET_MS is written, part by part,
// to sdmc:/ucg_prof.txt, along with what the game was doing.
#pragma once
#if UCG_PROFILE
#include <3ds.h>
#include <stdio.h>

#define PROF_BUDGET_MS 12.0f
#define PROF_MAX 24

typedef struct {
    const char *name;
    float ms;
} ProfPart;

extern ProfPart prof_parts[PROF_MAX];
extern int prof_n;
extern u64 prof_last, prof_frame_start, prof_call_start[4];
extern const char *prof_call[4];
extern int prof_depth;

static inline float prof_ms(u64 a, u64 b) { return (float)(b - a) / CPU_TICKS_PER_MSEC; }

static inline void prof_add(const char *name, float ms) {
    if (prof_n < PROF_MAX) prof_parts[prof_n++] = (ProfPart){name, ms};
}

#define PROF_FRAME_BEGIN() (prof_n = 0, prof_frame_start = prof_last = svcGetSystemTick())
#define PROF_MARK(name)                                                                                   \
    do {                                                                                                  \
        u64 _t = svcGetSystemTick();                                                                      \
        prof_add(name, prof_ms(prof_last, _t));                                                           \
        prof_last = _t;                                                                                   \
    } while (0)
// nested calls are listed with a leading "> " and are also counted inside the part around them
// only the game thread is measured (libctru's threadGetCurrent() is NULL there); calls made on the
// worker thread are left out
#define PROF_BEGIN(name)                                                                                  \
    do {                                                                                                  \
        if (threadGetCurrent()) break;                                                                    \
        if (prof_depth < 4) prof_call[prof_depth] = (name), prof_call_start[prof_depth] = svcGetSystemTick(); \
        prof_depth++;                                                                                     \
    } while (0)
#define PROF_END()                                                                                        \
    do {                                                                                                  \
        if (threadGetCurrent()) break;                                                                    \
        if (--prof_depth < 4 && prof_depth >= 0)                                                          \
            prof_add(prof_call[prof_depth], -prof_ms(prof_call_start[prof_depth], svcGetSystemTick()));  \
        if (prof_depth < 0) prof_depth = 0;                                                               \
    } while (0)
void prof_frame_end(int frame, const char *what);
#else
#define PROF_FRAME_BEGIN() ((void)0)
#define PROF_MARK(name) ((void)0)
#define PROF_BEGIN(name) ((void)0)
#define PROF_END() ((void)0)
#define prof_frame_end(frame, what) ((void)0)
#endif
