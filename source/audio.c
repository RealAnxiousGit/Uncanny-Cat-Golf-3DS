#include "prof.h"
#include "audio.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tremor/ivorbisfile.h>

#include "data.h"

#define SFX_CHANNELS 7      // ndsp channels 1..7; channel 0 is music
#define MAX_SFX 160
#define MUSIC_BUFS 4
#define MUSIC_BUF_SAMPLES 4096

typedef struct {
    char name[64];
    int16_t *pcm;
    uint32_t frames;
    int channels;
    int rate;
} Sfx;

static Sfx sfx[MAX_SFX];
static int nsfx;
static ndspWaveBuf sfx_wbuf[SFX_CHANNELS];
static int next_ch;
static bool ok;
static float vol_master = 1, vol_music = 1, vol_sound = 1;

// music
typedef struct { int id; char file[64]; bool loop; float loop_offset; } Track;
static Track tracks[32];
static int ntracks;
static OggVorbis_File mvf;
static bool music_open;
static int music_channels, music_rate;
static const Track *cur_track;
static ndspWaveBuf mbuf[MUSIC_BUFS];
static int16_t *mdata[MUSIC_BUFS];

// Music has a thread of its own (music_thread): higher priority than the game, woken by the DSP after
// every audio frame, so a long sound decode or file read on the worker can never starve it (that was
// the stutter on real hardware, e.g. in the intro while every sound is decoded). mlock guards the music
// decoder. The worker (audio_bg_step) decodes queued sounds; alock guards the sound table.
static LightLock alock, mlock;
static Thread mthread;
static LightEvent mev;
static volatile bool mrun;
#define PRELOAD_Q 64
static char preload_q[PRELOAD_Q][64];
static int q_head, q_tail;
static volatile bool bg_on; // a worker is running; otherwise the game thread does the work

static void load_track_index(void) {
    FILE *f = fopen(dp("music/index.txt"), "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line), f) && ntracks < 32) {
        Track t;
        int loop = 0;
        if (sscanf(line, "%d %63s %d %f", &t.id, t.file, &loop, &t.loop_offset) == 4) {
            t.loop = loop != 0;
            tracks[ntracks++] = t;
        }
    }
    fclose(f);
}

static bool music_step(void);

// called by ndsp after every audio frame (about 5 ms)
static void ndsp_frame(void *arg) {
    (void)arg;
    LightEvent_Signal(&mev);
}

static void music_thread(void *arg) {
    (void)arg;
    while (mrun) {
        while (music_step()) {}
        LightEvent_Wait(&mev);
    }
}

bool audio_init(void) {
    if (R_FAILED(ndspInit())) return false;
    ok = true;
    LightLock_Init(&alock);
    LightLock_Init(&mlock);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int i = 0; i < MUSIC_BUFS; i++) mdata[i] = linearAlloc(MUSIC_BUF_SAMPLES * 2 * sizeof(int16_t));
    load_track_index();
    LightEvent_Init(&mev, RESET_ONESHOT);
    ndspSetCallback(ndsp_frame, NULL);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    mrun = true;
    mthread = threadCreate(music_thread, NULL, 32 * 1024, prio - 1, -2, false);
    if (!mthread) mrun = false;
    return true;
}

void audio_exit(void) {
    if (!ok) return;
    if (mthread) {
        mrun = false;
        LightEvent_Signal(&mev);
        threadJoin(mthread, U64_MAX);
        threadFree(mthread);
        mthread = NULL;
    }
    ndspSetCallback(NULL, NULL);
    if (music_open) ov_clear(&mvf);
    ndspExit();
    for (int i = 0; i < nsfx; i++) linearFree(sfx[i].pcm);
    for (int i = 0; i < MUSIC_BUFS; i++) linearFree(mdata[i]);
    ok = false;
}

void audio_set_volumes(float master, float music, float sound) {
    vol_master = master;
    vol_music = music;
    vol_sound = sound;
    if (ok) {
        float mix[12] = {0};
        mix[0] = mix[1] = vol_master * vol_music;
        ndspChnSetMix(0, mix);
    }
}

static Sfx *find_locked(const char *rel) {
    for (int i = 0; i < nsfx; i++)
        if (!strcmp(sfx[i].name, rel)) return &sfx[i];
    return NULL;
}

static Sfx *find(const char *rel) {
    LightLock_Lock(&alock);
    Sfx *s = find_locked(rel);
    LightLock_Unlock(&alock);
    return s;
}

static Sfx *add_sound(const char *rel, int16_t *pcm, size_t bytes, int ch, int rate) {
    DSP_FlushDataCache(pcm, bytes);
    LightLock_Lock(&alock);
    Sfx *s = find_locked(rel); // the other thread may have finished it meanwhile
    if (s || nsfx >= MAX_SFX) {
        linearFree(pcm);
    } else {
        s = &sfx[nsfx];
        snprintf(s->name, sizeof(s->name), "%s", rel);
        s->pcm = pcm;
        s->channels = ch;
        s->rate = rate;
        s->frames = bytes / (ch * sizeof(int16_t));
        nsfx++;
    }
    LightLock_Unlock(&alock);
    return s;
}

// a plain 16-bit PCM .wav (the port's own sounds, like the Oops screen's)
static Sfx *load_wav(const char *rel, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char h[12], c[8];
    int ch = 1, rate = 22050, bits = 16;
    Sfx *s = NULL;
    if (fread(h, 1, 12, f) == 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) {
        while (fread(c, 1, 8, f) == 8) {
            uint32_t len = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
            if (!memcmp(c, "fmt ", 4)) {
                unsigned char fm[16];
                if (len < 16 || fread(fm, 1, 16, f) != 16) break;
                ch = fm[2] | fm[3] << 8;
                rate = fm[4] | fm[5] << 8 | fm[6] << 16 | fm[7] << 24;
                bits = fm[14] | fm[15] << 8;
                fseek(f, len - 16 + (len & 1), SEEK_CUR);
            } else if (!memcmp(c, "data", 4)) {
                if (bits != 16 || ch < 1 || ch > 2) break;
                int16_t *pcm = linearAlloc(len + 8);
                if (!pcm) break;
                size_t got = fread(pcm, 1, len, f);
                s = add_sound(rel, pcm, got, ch, rate);
                break;
            } else {
                fseek(f, len + (len & 1), SEEK_CUR);
            }
        }
    }
    fclose(f);
    return s;
}

// decodes outside the lock (the slow part), then adds the sound to the table
static Sfx *find_or_load(const char *rel) {
    Sfx *have = find(rel);
    if (have) return have;
    char path[160];
    // a full path ("romfs:/port/...") is the port's own sound; anything else is one of the game's
    if (strchr(rel, ':')) snprintf(path, sizeof(path), "%s", rel);
    else snprintf(path, sizeof(path), "%s/sfx/%s", data_root(), rel);
    size_t rl = strlen(rel);
    if (rl > 4 && !strcmp(rel + rl - 4, ".wav")) return load_wav(rel, path);
    PROF_BEGIN("sound decode");
    FILE *f = fopen(path, "rb");
    if (!f) {
        PROF_END();
        return NULL;
    }
    OggVorbis_File vf;
    if (ov_open(f, &vf, NULL, 0) < 0) {
        fclose(f);
        PROF_END();
        return NULL;
    }
    vorbis_info *vi = ov_info(&vf, -1);
    ogg_int64_t total = ov_pcm_total(&vf, -1);
    int ch = vi->channels > 1 ? 2 : 1;
    int16_t *pcm = linearAlloc((size_t)total * ch * sizeof(int16_t) + 8192);
    if (!pcm) {
        ov_clear(&vf);
        PROF_END();
        return NULL;
    }
    size_t got = 0, cap = (size_t)total * ch * sizeof(int16_t);
    int bs;
    while (got < cap) {
        long n = ov_read(&vf, (char *)pcm + got, (int)(cap - got > 4096 ? 4096 : cap - got), &bs);
        if (n <= 0) break;
        got += n;
    }
    int rate = vi->rate;
    ov_clear(&vf); // closes f
    Sfx *s = add_sound(rel, pcm, got, ch, rate);
    PROF_END();
    return s;
}

// queues a sound for the worker to decode (or decodes it now when there is no worker)
void sfx_preload(const char *rel) {
    if (!ok) return;
    if (!bg_on) {
        find_or_load(rel);
        return;
    }
    LightLock_Lock(&alock);
    if ((q_tail + 1) % PRELOAD_Q != q_head) {
        snprintf(preload_q[q_tail], sizeof(preload_q[0]), "%s", rel);
        q_tail = (q_tail + 1) % PRELOAD_Q;
    }
    LightLock_Unlock(&alock);
}

void sfx_play(const char *rel, float pitch, float volume) {
    if (!ok || vol_sound <= 0 || vol_master <= 0) return;
    Sfx *s = find_or_load(rel);
    if (!s || !s->frames) return;
    int ch = 1 + next_ch;
    next_ch = (next_ch + 1) % SFX_CHANNELS;
    ndspChnWaveBufClear(ch);
    ndspChnReset(ch);
    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetRate(ch, s->rate * pitch);
    ndspChnSetFormat(ch, s->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    float mix[12] = {0};
    mix[0] = mix[1] = vol_master * vol_sound * volume;
    ndspChnSetMix(ch, mix);
    ndspWaveBuf *wb = &sfx_wbuf[ch - 1];
    memset(wb, 0, sizeof(*wb));
    wb->data_vaddr = s->pcm;
    wb->nsamples = s->frames;
    ndspChnWaveBufAdd(ch, wb);
}

static void music_fill(ndspWaveBuf *wb, int16_t *data) {
    size_t want = MUSIC_BUF_SAMPLES * music_channels * sizeof(int16_t), got = 0;
    int bs;
    while (got < want) {
        long n = ov_read(&mvf, (char *)data + got, (int)(want - got), &bs);
        if (n <= 0) {
            if (cur_track && cur_track->loop) {
                ov_pcm_seek(&mvf, (ogg_int64_t)(cur_track->loop_offset * music_rate));
                continue;
            }
            break;
        }
        got += n;
    }
    if (!got) { // the track ended: leave the buffer alone so it is not asked for again
        wb->status = NDSP_WBUF_FREE;
        return;
    }
    memset(wb, 0, sizeof(*wb));
    wb->data_vaddr = data;
    wb->nsamples = got / (music_channels * sizeof(int16_t));
    DSP_FlushDataCache(data, got);
    ndspChnWaveBufAdd(0, wb);
}

void music_play(int id) {
    if (!ok) return;
    LightLock_Lock(&mlock);
    const Track *t = NULL;
    for (int i = 0; i < ntracks; i++)
        if (tracks[i].id == id) t = &tracks[i];
    if (t && t == cur_track && music_open) {
        LightLock_Unlock(&mlock);
        return;
    }
    ndspChnWaveBufClear(0);
    if (music_open) {
        ov_clear(&mvf);
        music_open = false;
    }
    cur_track = NULL;
    if (!t) {
        LightLock_Unlock(&mlock);
        return;
    }
    char path[160];
    snprintf(path, sizeof(path), "%s/music/%s", data_root(), t->file);
    FILE *f = fopen(path, "rb");
    if (!f) {
        LightLock_Unlock(&mlock);
        return;
    }
    if (ov_open(f, &mvf, NULL, 0) < 0) {
        fclose(f);
        LightLock_Unlock(&mlock);
        return;
    }
    vorbis_info *vi = ov_info(&mvf, -1);
    music_channels = vi->channels > 1 ? 2 : 1;
    music_rate = vi->rate;
    music_open = true;
    cur_track = t;
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, music_rate);
    ndspChnSetFormat(0, music_channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    audio_set_volumes(vol_master, vol_music, vol_sound);
    // the music thread fills the buffers; without one, the first fill happens here
    for (int i = 0; i < MUSIC_BUFS; i++) {
        memset(&mbuf[i], 0, sizeof(mbuf[i]));
        mbuf[i].status = NDSP_WBUF_DONE;
    }
    if (!mthread)
        for (int i = 0; i < MUSIC_BUFS; i++) music_fill(&mbuf[i], mdata[i]);
    LightLock_Unlock(&mlock);
    if (mthread) LightEvent_Signal(&mev);
}

static bool music_step(void) {
    bool did = false;
    LightLock_Lock(&mlock);
    if (music_open)
        for (int i = 0; i < MUSIC_BUFS; i++)
            if (mbuf[i].status == NDSP_WBUF_DONE) {
                music_fill(&mbuf[i], mdata[i]);
                did = true;
                break; // one buffer per step keeps the lock short
            }
    LightLock_Unlock(&mlock);
    return did;
}

void music_update(void) {
    if (!ok || mthread) return;
    while (music_step()) {}
}

void audio_bg_set(bool on) { bg_on = on; }

bool audio_bg_step(void) {
    if (!ok) return false;
    bool did = false;
    char rel[64];
    rel[0] = 0;
    LightLock_Lock(&alock);
    if (q_head != q_tail) {
        snprintf(rel, sizeof(rel), "%s", preload_q[q_head]);
        q_head = (q_head + 1) % PRELOAD_Q;
    }
    LightLock_Unlock(&alock);
    if (rel[0]) {
        find_or_load(rel);
        did = true;
    }
    return did;
}
