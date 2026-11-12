#include "prof.h"
#if UCG_PROFILE
ProfPart prof_parts[PROF_MAX];
int prof_n;
u64 prof_last, prof_frame_start, prof_call_start[4];
const char *prof_call[4];
int prof_depth;

static FILE *out;
static float worst[PROF_MAX];
static const char *worst_name[PROF_MAX];
static int nworst;

void prof_frame_end(int frame, const char *what) {
    // the game's own work: waits for the GPU (parts named "~...") do not count, since the test
    // emulator draws in software and its GPU time says nothing about a real 3DS
    float total = 0;
    for (int i = 0; i < prof_n; i++)
        if (prof_parts[i].ms > 0 && prof_parts[i].name[0] != '~') total += prof_parts[i].ms;
    if (!out) out = fopen("sdmc:/ucg_prof.txt", "w");
    if (!out) return;
    // the worst time seen for each part, written every 10 seconds
    for (int i = 0; i < prof_n; i++) {
        int k = 0;
        while (k < nworst && worst_name[k] != prof_parts[i].name) k++;
        if (k == nworst && nworst < PROF_MAX) worst_name[nworst++] = prof_parts[i].name, worst[k] = 0;
        float ms = prof_parts[i].ms < 0 ? -prof_parts[i].ms : prof_parts[i].ms;
        if (k < nworst && ms > worst[k]) worst[k] = ms;
    }
    if (total > PROF_BUDGET_MS) {
        fprintf(out, "frame %d %.1f ms (%s):", frame, total, what);
        for (int i = 0; i < prof_n; i++)
            fprintf(out, " %s%s %.1f", prof_parts[i].ms < 0 ? "> " : "", prof_parts[i].name,
                    prof_parts[i].ms < 0 ? -prof_parts[i].ms : prof_parts[i].ms);
        fputc('\n', out);
        fflush(out);
    }
    if (frame % 600 == 0) {
        fprintf(out, "worst by frame %d:", frame);
        for (int k = 0; k < nworst; k++) fprintf(out, " %s %.1f", worst_name[k], worst[k]);
        fputc('\n', out);
        fflush(out);
    }
}
#endif
