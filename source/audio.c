#include "audio.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tremor/ivorbisfile.h>

#define SFX_CHANNELS 7      // ndsp channels 1..7; channel 0 is music
#define MAX_SFX 160
#define MUSIC_BUFS 3
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

static void load_track_index(void) {
    FILE *f = fopen("romfs:/music/index.txt", "r");
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

bool audio_init(void) {
    if (R_FAILED(ndspInit())) return false;
    ok = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int i = 0; i < MUSIC_BUFS; i++) mdata[i] = linearAlloc(MUSIC_BUF_SAMPLES * 2 * sizeof(int16_t));
    load_track_index();
    return true;
}

void audio_exit(void) {
    if (!ok) return;
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

static Sfx *find_or_load(const char *rel) {
    for (int i = 0; i < nsfx; i++)
        if (!strcmp(sfx[i].name, rel)) return &sfx[i];
    if (nsfx >= MAX_SFX) return NULL;
    char path[160];
    snprintf(path, sizeof(path), "romfs:/sfx/%s", rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    OggVorbis_File vf;
    if (ov_open(f, &vf, NULL, 0) < 0) {
        fclose(f);
        return NULL;
    }
    vorbis_info *vi = ov_info(&vf, -1);
    ogg_int64_t total = ov_pcm_total(&vf, -1);
    int ch = vi->channels > 1 ? 2 : 1;
    int16_t *pcm = linearAlloc((size_t)total * ch * sizeof(int16_t) + 8192);
    if (!pcm) {
        ov_clear(&vf);
        return NULL;
    }
    size_t got = 0, cap = (size_t)total * ch * sizeof(int16_t);
    int bs;
    while (got < cap) {
        long n = ov_read(&vf, (char *)pcm + got, (int)(cap - got > 4096 ? 4096 : cap - got), &bs);
        if (n <= 0) break;
        got += n;
    }
    Sfx *s = &sfx[nsfx++];
    snprintf(s->name, sizeof(s->name), "%s", rel);
    s->pcm = pcm;
    s->channels = ch;
    s->rate = vi->rate;
    s->frames = got / (ch * sizeof(int16_t));
    ov_clear(&vf); // closes f
    DSP_FlushDataCache(pcm, got);
    return s;
}

void sfx_preload(const char *rel) {
    if (ok) find_or_load(rel);
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
    if (!got) return;
    memset(wb, 0, sizeof(*wb));
    wb->data_vaddr = data;
    wb->nsamples = got / (music_channels * sizeof(int16_t));
    DSP_FlushDataCache(data, got);
    ndspChnWaveBufAdd(0, wb);
}

void music_play(int id) {
    if (!ok) return;
    const Track *t = NULL;
    for (int i = 0; i < ntracks; i++)
        if (tracks[i].id == id) t = &tracks[i];
    if (t && t == cur_track && music_open) return;
    ndspChnWaveBufClear(0);
    if (music_open) {
        ov_clear(&mvf);
        music_open = false;
    }
    cur_track = NULL;
    if (!t) return;
    char path[160];
    snprintf(path, sizeof(path), "romfs:/music/%s", t->file);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    if (ov_open(f, &mvf, NULL, 0) < 0) {
        fclose(f);
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
    for (int i = 0; i < MUSIC_BUFS; i++) music_fill(&mbuf[i], mdata[i]);
}

void music_update(void) {
    if (!ok || !music_open) return;
    for (int i = 0; i < MUSIC_BUFS; i++)
        if (mbuf[i].status == NDSP_WBUF_DONE) music_fill(&mbuf[i], mdata[i]);
}
