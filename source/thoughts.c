// Thoughts clips: romfs:/thoughts/<Category>/<name>.uth, made by tools/convert/hud.py.
// One frame is read and unpacked at a time, so clips never have to fit in memory.
// The clip files belong to the worker thread (thoughts_bg_step): it opens clips and unpacks
// frames into a finished picture, and the game thread only copies that picture into a texture.
#include "prof.h"
#include "thoughts.h"
#include "data.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define FW 109
#define FH 89
#define TEX 128
#define STATIC_TIME 0.2f // tv_static_time

typedef struct {
    char cat[16], name[40];
} ClipRef;

typedef struct {
    FILE *f;
    int frames, ncol, loops;
    float fps;
    u16 pal[256];
    u32 *offs;
    int decoded; // last unpacked frame, -1 = none
    u8 idx[FW * FH];
} Clip;

static ClipRef *refs;
static int nrefs;

// game thread
static bool cur_open, enabled = true, loading, static_visible = true, info_ready;
static char current_cat[16] = "", fallback_cat[16] = "Idle";
static float current_frame, load_time, static_frame;
static int loops;
typedef struct {
    int frames, loops;
    float fps;
    bool ok;
} ClipInfo;
static ClipInfo info;                  // the current clip, once the worker has opened it
static int shown_id[3] = {-1, -1, -1}; // clip (0 static, 1 current), frame, request number

// worker side
static Clip stat, cur;
static u8 zbuf[FW * FH + 1024];
static u16 tiled_off[FW * FH];
static int opened_gen = 0, made_id[3] = {-1, -1, -1};
#define PIX (TEX * 96) // the picture's 12 rows of 8x8 tiles
static u16 work_pix[PIX];

// shared, under tlock
static LightLock tlock;
static volatile bool bg;
static char req_path[160];
static int req_gen, open_gen_pub, want_id[3] = {0, 0, 0}, ready_id[3] = {-1, -1, -1};
static ClipInfo info_pub;
static u16 ready_pix[PIX];

static C3D_Tex tex[2];
static Tex3DS_SubTexture subtex = {FW, FH, 0.0f, 1.0f, FW / (float)TEX, 1.0f - FH / (float)TEX};
static int tex_cur;

static void clip_close(Clip *c) {
    if (c->f) fclose(c->f);
    free(c->offs);
    memset(c, 0, sizeof(*c));
}

static bool clip_open(Clip *c, const char *path) {
    clip_close(c);
    c->f = fopen(path, "rb");
    if (!c->f) return false;
    PROF_BEGIN("clip open");
    u8 h[20];
    if (fread(h, 1, 20, c->f) != 20 || memcmp(h, "UTH1", 4)) {
        clip_close(c);
        return false;
    }
    c->frames = h[8] | h[9] << 8;
    c->ncol = h[10] | h[11] << 8;
    memcpy(&c->fps, h + 12, 4);
    c->loops = (s16)(h[16] | h[17] << 8);
    if (c->ncol > 256 || c->frames <= 0) {
        clip_close(c);
        return false;
    }
    fread(c->pal, 2, c->ncol, c->f);
    c->offs = malloc(4 * (c->frames + 1));
    fread(c->offs, 4, c->frames + 1, c->f);
    c->decoded = -1;
    PROF_END();
    return true;
}

static bool clip_unpack(Clip *c, int frame) {
    if (frame == c->decoded) return true;
    int from = (c->decoded >= 0 && frame > c->decoded) ? c->decoded + 1 : 0;
    for (int i = from; i <= frame; i++) {
        u32 len = c->offs[i + 1] - c->offs[i];
        if (len > sizeof(zbuf) || fseek(c->f, c->offs[i], SEEK_SET) || fread(zbuf, 1, len, c->f) != len) return false;
        static u8 out[FW * FH];
        uLongf olen = sizeof(out);
        if (uncompress(out, &olen, zbuf + 1, len - 1) != Z_OK) return false;
        if (zbuf[0] == 1)
            for (int k = 0; k < FW * FH; k++) c->idx[k] ^= out[k];
        else
            memcpy(c->idx, out, sizeof(out));
        c->decoded = i;
    }
    return true;
}

// worker: open the clip asked for, or unpack the frame asked for into ready_pix
bool thoughts_bg_step(void) {
    char path[160];
    int gen, want[3];
    LightLock_Lock(&tlock);
    gen = req_gen;
    if (gen != opened_gen) snprintf(path, sizeof(path), "%s", req_path);
    memcpy(want, want_id, sizeof(want));
    LightLock_Unlock(&tlock);
    if (gen != opened_gen) {
        bool ok = path[0] && clip_open(&cur, path);
        if (!ok) clip_close(&cur);
        opened_gen = gen;
        LightLock_Lock(&tlock);
        open_gen_pub = gen;
        info_pub = (ClipInfo){cur.frames, cur.loops, cur.fps, ok};
        LightLock_Unlock(&tlock);
        return true;
    }
    if (!memcmp(want, made_id, sizeof(want))) return false;
    Clip *c = want[0] ? &cur : &stat;
    if ((want[0] && want[2] != opened_gen) || !c->frames || want[1] >= c->frames) return false;
    if (!clip_unpack(c, want[1])) return false;
    for (int k = 0; k < FW * FH; k++) work_pix[tiled_off[k]] = c->pal[c->idx[k]];
    memcpy(made_id, want, sizeof(want));
    LightLock_Lock(&tlock);
    memcpy(ready_pix, work_pix, sizeof(ready_pix));
    memcpy(ready_id, want, sizeof(want));
    LightLock_Unlock(&tlock);
    return true;
}

void thoughts_bg_set(bool on) { bg = on; }

// game thread: ask for a picture, and take the newest finished one if it is that picture
// (two textures, so the GPU never reads one being written)
static void show(int clip, int frame) {
    int want[3] = {clip, frame, clip ? req_gen : 0};
    if (!memcmp(want, shown_id, sizeof(want))) return;
    LightLock_Lock(&tlock);
    memcpy(want_id, want, sizeof(want));
    LightLock_Unlock(&tlock);
    if (!bg)
        while (thoughts_bg_step()) {}
    LightLock_Lock(&tlock);
    if (!memcmp(ready_id, want, sizeof(want))) {
        tex_cur ^= 1;
        memcpy(tex[tex_cur].data, ready_pix, sizeof(ready_pix));
        C3D_TexFlush(&tex[tex_cur]);
        memcpy(shown_id, want, sizeof(want));
    }
    LightLock_Unlock(&tlock);
}

// game thread: which clip the worker should have open ("" closes it)
static void request_clip(const char *path) {
    LightLock_Lock(&tlock);
    snprintf(req_path, sizeof(req_path), "%s", path);
    req_gen++;
    LightLock_Unlock(&tlock);
    info_ready = false;
}

static u32 morton(u32 x, u32 y) {
    u32 m = 0;
    for (int b = 0; b < 3; b++) m |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1);
    return m;
}

void thoughts_init(void) {
    LightLock_Init(&tlock);
    for (int i = 0; i < 2; i++) {
        C3D_TexInit(&tex[i], TEX, TEX, GPU_RGB565);
        C3D_TexSetFilter(&tex[i], GPU_LINEAR, GPU_LINEAR);
        memset(tex[i].data, 0, tex[i].size);
        C3D_TexFlush(&tex[i]);
    }
    // the subtexture's top edge (v = 1) is the start of the data, so image row y is
    // tiled row y; the 39 spare rows sit past the picture's bottom edge
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++) {
            u32 r = y;
            tiled_off[y * FW + x] = ((r >> 3) * (TEX >> 3) + (x >> 3)) * 64 + morton(x & 7, r & 7);
        }
    FILE *f = fopen(dp("thoughts/index.txt"), "r");
    if (f) {
        char line[160];
        int cap = 0;
        while (fgets(line, sizeof(line), f)) {
            if (nrefs == cap) {
                cap = cap ? cap * 2 : 256;
                refs = realloc(refs, cap * sizeof(ClipRef));
            }
            if (sscanf(line, "%15s %39s", refs[nrefs].cat, refs[nrefs].name) == 2) nrefs++;
        }
        fclose(f);
    }
    clip_open(&stat, dp("thoughts/static.uth"));
}

void thoughts_exit(void) {
    clip_close(&stat);
    clip_close(&cur);
    for (int i = 0; i < 2; i++) C3D_TexDelete(&tex[i]);
    free(refs);
    refs = NULL;
    nrefs = 0;
}

void thoughts_enable(bool on) {
    enabled = on;
    if (!on) {
        request_clip("");
        cur_open = false;
        current_cat[0] = 0;
        static_visible = true;
    }
}

void thoughts_fallback(const char *category) { snprintf(fallback_cat, sizeof(fallback_cat), "%s", category); }

void thoughts_play(const char *category) {
    if (!enabled || !strcmp(category, current_cat)) return;
    snprintf(current_cat, sizeof(current_cat), "%s", category);
    int n = 0;
    for (int i = 0; i < nrefs; i++) n += !strcmp(refs[i].cat, category);
    if (!n) return;
    int pick = rand() % n;
    for (int i = 0; i < nrefs; i++)
        if (!strcmp(refs[i].cat, category) && pick-- == 0) {
            char path[160];
            snprintf(path, sizeof(path), "%s/thoughts/%s/%s.uth", data_root(), refs[i].cat, refs[i].name);
            request_clip(path);
            cur_open = true; // until the worker says it could not open it
            break;
        }
    loading = cur_open;
    static_visible = true;
    load_time = 0;
}

void thoughts_update(float dt) {
    if (!enabled) return;
    static_frame += dt * 30.0f;
    if (stat.frames) static_frame = fmodf(static_frame, stat.frames);
    if (cur_open && !info_ready) {
        LightLock_Lock(&tlock);
        if (open_gen_pub == req_gen) info = info_pub, info_ready = true;
        LightLock_Unlock(&tlock);
        if (info_ready && (!info.ok || info.frames <= 0)) { // a clip that would not open: static stays
            cur_open = loading = false;
            static_visible = true;
        }
    }
    if (loading) {
        load_time += dt;
        if (load_time >= STATIC_TIME && info_ready) { // _start_playback()
            static_visible = false;
            loading = false;
            current_frame = 0;
            loops = 0;
        }
    }
    if (!loading && cur_open) {
        current_frame += dt * info.fps;
        if (current_frame >= info.frames) {
            current_frame = 0;
            loops++;
            if (loops > info.loops) {
                static_visible = true;
                cur_open = false;
                request_clip("");
                current_cat[0] = 0;
                thoughts_play(fallback_cat); // autoplay_fallback is on everywhere the game uses it
                return;
            }
        }
    }
}

bool thoughts_image(C2D_Image *out) {
    if (!enabled) return false;
    if (static_visible || !cur_open) {
        if (!stat.frames) return false;
        show(0, (int)static_frame % stat.frames);
    } else {
        int fr = (int)current_frame;
        if (fr >= info.frames) fr = info.frames - 1;
        show(1, fr);
    }
    out->tex = &tex[tex_cur];
    out->subtex = &subtex;
    return true;
}
