// Uncanny Cat Golf 3DS: screens, drawing and input on the console.
// Bottom screen: the course (and every menu). Top screen: the sidebar.
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prof.h"
#include "worker.h"
#include "data.h"
#include "audio.h"
#include "game.h"
#include "save.h"
#include "thoughts.h"

#define COURSE_X 40.0f
#define COURSE_W 240.0f
#define BOT_W 320
#define TOP_W 400
#define SCR_H 240
#define NWORLDS 8

// ---------------------------------------------------------------- sprites
typedef struct { char name[32]; C2D_SpriteSheet *sheet; int idx; float ax, ay, ux, uy; } SpriteInfo;
#define MAX_SPR 200
static SpriteInfo sprinfo[MAX_SPR];
static int nspr;
static C2D_SpriteSheet sprites, hudsheet, top_bg, menusheet, settings_bg, jumpscare_sheet, creditsheet;

static int spr_find(const char *name) {
    for (int i = 0; i < nspr; i++)
        if (!strcmp(sprinfo[i].name, name)) return i;
    return -1;
}

// a sprite sheet, from bytes the worker read ahead when there are some (then only the copy into a
// texture happens here), else straight from the file
static C2D_SpriteSheet sheet_load(const char *path) {
    size_t n;
    void *b = prefetch_take(path, &n);
    if (!b) return C2D_SpriteSheetLoad(path);
    C2D_SpriteSheet s = C2D_SpriteSheetLoadFromMem(b, n);
    free(b);
    return s;
}

static void load_sheet(C2D_SpriteSheet *sheet, const char *t3x, const char *txt) {
    *sheet = sheet_load(t3x);
    FILE *f = fopen(txt, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line), f) && nspr < MAX_SPR) {
        SpriteInfo *s = &sprinfo[nspr];
        int w, h;
        if (sscanf(line, "%31s %d %d %d %f %f %f %f", s->name, &s->idx, &w, &h, &s->ax, &s->ay, &s->ux, &s->uy) == 8) {
            s->sheet = sheet;
            nspr++;
        }
    }
    fclose(f);
}

// ---------------------------------------------------------------- text
// romfs:/lang/<code>.txt, "KEY<tab>text" per line, sorted for lookup.
// English stays loaded underneath: keys a language leaves empty fall back to it.
typedef struct { char *blob; char **keys, **vals; int n; } Lang;
static Lang lang_en, lang_cur;
static const char *LANGS[] = {"en", "fr", "ru", "ja", "ro", "bi"}; // Settings.LANGS
#define NLANGS 6

static int key_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void lang_free(Lang *l) {
    free(l->blob);
    free(l->keys);
    free(l->vals);
    memset(l, 0, sizeof(*l));
}

static void lang_load(Lang *l, const char *code) {
    lang_free(l);
    char path[64];
    snprintf(path, sizeof(path), "%s/lang/%s.txt", data_root(), code);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    l->blob = malloc(len + 1);
    fread(l->blob, 1, len, f);
    l->blob[len] = 0;
    fclose(f);
    int lines = 1;
    for (long i = 0; i < len; i++)
        if (l->blob[i] == '\n') lines++;
    // pairs stored as [key, val] so one sort keeps them together
    char **pairs = malloc(lines * 2 * sizeof(char *));
    int n = 0;
    char *p = l->blob;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        char *tab = strchr(p, '\t');
        if (tab) {
            *tab = 0;
            char *v = tab + 1, *w = v;
            for (char *r = v; *r; r++) { // unescape \n and backslashes
                if (*r == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
                else if (*r == '\\' && r[1] == '\\') { *w++ = '\\'; r++; }
                else *w++ = *r;
            }
            *w = 0;
            pairs[2 * n] = p;
            pairs[2 * n + 1] = v;
            n++;
        }
        if (!nl) break;
        p = nl + 1;
    }
    qsort(pairs, n, 2 * sizeof(char *), key_cmp);
    l->keys = malloc(n * sizeof(char *));
    l->vals = malloc(n * sizeof(char *));
    for (int i = 0; i < n; i++) l->keys[i] = pairs[2 * i], l->vals[i] = pairs[2 * i + 1];
    l->n = n;
    free(pairs);
}

static const char *lang_get(const Lang *l, const char *key) {
    int lo = 0, hi = l->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(key, l->keys[mid]);
        if (!c) return l->vals[mid];
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return NULL;
}

static const char *tr(const char *key) {
    const char *v = lang_get(&lang_cur, key);
    if (!v) v = lang_get(&lang_en, key);
    return v ? v : key;
}

// tr() with one "{name}" replaced
static void trf(char *out, size_t n, const char *key, const char *name, const char *val) {
    const char *t = tr(key);
    char pat[32];
    snprintf(pat, sizeof(pat), "{%s}", name);
    const char *at = strstr(t, pat);
    if (!at) {
        snprintf(out, n, "%s", t);
        return;
    }
    snprintf(out, n, "%.*s%s%s", (int)(at - t), t, val, at + strlen(pat));
}

// level list
static char wnames[NWORLDS][32][16];
static int wcount[NWORLDS];

static void load_level_index(void) {
    FILE *f = fopen(dp("levels/index.txt"), "r");
    if (!f) return;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        int w, n, off;
        if (sscanf(line, "world %d %d %n", &w, &n, &off) < 2 || w < 0 || w >= NWORLDS) continue;
        char *p = line + off;
        for (int i = 0; i < n && i < 32; i++) {
            int used;
            if (sscanf(p, "%15s%n", wnames[w][i], &used) != 1) break;
            p += used;
            wcount[w] = i + 1;
        }
    }
    fclose(f);
}

// ---------------------------------------------------------------- settings
typedef struct {
    int sens10;           // shot sensitivity x10 (2..20), the original's 0.2 steps
    bool invert, buffer;  // the original's invert_shot / buffer_shot
    bool drag_on_cat;     // a drag can only start on the cat
    bool left_handed;     // side strips swapped
    bool thoughts;        // Cat Thought Displayer on the top screen
    int jumpscares;       // 0 on, 1 quiet, 2 off
    bool show_fps;
    bool zoom;            // course fills the bottom screen's width and follows the cat
    int master, music, sound; // 0..100
    int lang;
} AppSettings;

static AppSettings set = {10, false, true, false, false, true, 0, false, true, 100, 100, 100, 0};
static Cfg *config, *savedata;

static void apply_settings(void) {
    audio_set_volumes(set.master / 100.0f, set.music / 100.0f, set.sound / 100.0f);
    thoughts_enable(set.thoughts);
    lang_load(&lang_cur, LANGS[set.lang]);
}

static void load_settings(void) {
    config = cfg_open("config.ini");
    set.sens10 = (int)lroundf(cfg_get_float(config, "GENERAL", "power_sensitivity", 1.0f) * 10);
    if (set.sens10 < 2) set.sens10 = 2;
    if (set.sens10 > 20) set.sens10 = 20;
    set.invert = cfg_get_int(config, "GENERAL", "invert_shot", 0);
    set.buffer = cfg_get_int(config, "GENERAL", "buffer_shot", 1);
    const char *lc = cfg_get_str(config, "GENERAL", "language", "en");
    for (int i = 0; i < NLANGS; i++)
        if (!strcmp(lc, LANGS[i])) set.lang = i;
    set.master = (int)lroundf(cfg_get_float(config, "AUDIO", "master_volume", 1.0f) * 100);
    set.music = (int)lroundf(cfg_get_float(config, "AUDIO", "music_volume", 1.0f) * 100);
    set.sound = (int)lroundf(cfg_get_float(config, "AUDIO", "sound_volume", 1.0f) * 100);
    set.drag_on_cat = cfg_get_int(config, "3DS", "drag_on_cat", 0);
    set.left_handed = cfg_get_int(config, "3DS", "left_handed", 0);
    set.thoughts = cfg_get_int(config, "3DS", "thoughts", 1);
    set.jumpscares = cfg_get_int(config, "3DS", "jumpscares", 0) % 3;
    set.show_fps = cfg_get_int(config, "3DS", "show_fps", 0);
    set.zoom = cfg_get_int(config, "3DS", "course_zoom", 1);
}

static void save_settings(void) {
    cfg_set_str(config, "GENERAL", "language", LANGS[set.lang]);
    cfg_set_float(config, "GENERAL", "power_sensitivity", set.sens10 / 10.0f);
    cfg_set_str(config, "GENERAL", "buffer_shot", set.buffer ? "true" : "false");
    cfg_set_str(config, "GENERAL", "invert_shot", set.invert ? "true" : "false");
    cfg_set_float(config, "AUDIO", "master_volume", set.master / 100.0f);
    cfg_set_float(config, "AUDIO", "music_volume", set.music / 100.0f);
    cfg_set_float(config, "AUDIO", "sound_volume", set.sound / 100.0f);
    cfg_set_int(config, "3DS", "drag_on_cat", set.drag_on_cat);
    cfg_set_int(config, "3DS", "left_handed", set.left_handed);
    cfg_set_int(config, "3DS", "thoughts", set.thoughts);
    cfg_set_int(config, "3DS", "jumpscares", set.jumpscares);
    cfg_set_int(config, "3DS", "show_fps", set.show_fps);
    cfg_set_int(config, "3DS", "course_zoom", set.zoom);
    cfg_write_later(config);
}

static GameSettings game_settings(void) {
    GameSettings g = {set.sens10 / 10.0f, set.invert, set.buffer};
    return g;
}

// best results per level, until playthroughs (the original's pthru files) arrive
static void record_result(const char *id, int score, float time, int rank, int strokes) {
    char k[40];
    snprintf(k, sizeof(k), "%s_score", id);
    bool better = score > cfg_get_int(savedata, "LEVELS", k, -1);
    if (better) {
        cfg_set_int(savedata, "LEVELS", k, score);
        snprintf(k, sizeof(k), "%s_rank", id);
        cfg_set_int(savedata, "LEVELS", k, rank);
        snprintf(k, sizeof(k), "%s_strokes", id);
        cfg_set_int(savedata, "LEVELS", k, strokes);
    }
    snprintf(k, sizeof(k), "%s_time", id);
    float bt = cfg_get_float(savedata, "LEVELS", k, 1e9f);
    if (time < bt) cfg_set_float(savedata, "LEVELS", k, time);
    PROF_BEGIN("save write");
    cfg_write_later(savedata);
    PROF_END();
}

// ---------------------------------------------------------------- app state
enum { ST_SELECT, ST_PLAY, ST_PAUSE, ST_SETTINGS, ST_INTRO };

static C3D_RenderTarget *top, *bot;
static C2D_TextBuf tbuf;
static int state = ST_INTRO, settings_back = ST_SELECT;
static int cur_world, cur_level, sel_world, sel_cursor, sel_scroll;
static Game game;
static LevelDef level;
static bool level_loaded;
static C2D_SpriteSheet floor_u, floor_o, dyn_sheet;
static GameSettings gset;
static int jumpscare_t, results_t, win_t;
static int results_stage; // 0 sliding in, 1 counting, 2 rank shown
static int results_rank_shown;
static vec2 last_touch;
static bool dragging;
static int pause_cursor, set_cursor, set_scroll;
enum { STOUCH_NONE, STOUCH_PENDING, STOUCH_SCROLL, STOUCH_SLIDER };
static float btn_alpha[3] = {1, 1, 1}; // how see-through each course button is (pause, restart, skip)
static int set_touch, set_touch_row; // stylus in settings: tap, list drag or slider drag
static float set_pos, set_touch_pos, set_vel, set_last_y; // the list's scroll in pixels, drag start, fling speed
static vec2 set_touch_at;
static float rainbow_t;
static int fps_shown, fps_count;
static u64 fps_since;

// Fit: the whole course in the 240 px between the side strips. Zoomed: the course is as wide as
// the screen (320 px) and the view scrolls up and down after the cat (cam_y, in world units).
static float cam_y;
static float course_scale(void) { return (set.zoom ? BOT_W : COURSE_W) / (2.0f * level.bounds); }
static float course_left(void) { return set.zoom ? 0 : COURSE_X; }
static float course_top(void) { return set.zoom ? SCR_H / 2 - (cam_y + level.bounds) * course_scale() : 0; }

// keeps the zoomed view on the cat; snap jumps straight there (level start, view switch)
static void camera_follow(vec2 target, bool snap) {
    float half = SCR_H / 2 / course_scale(), lim = level.bounds - half;
    float want = lim > 0 ? clampf(target.y, -lim, lim) : 0;
    cam_y = snap ? want : cam_y + (want - cam_y) * 0.12f;
}

static vec2 world_to_screen(vec2 w) {
    float k = course_scale();
    float x = (w.x + level.bounds) * k;
    if (level.flags & LF_MIRROR) x = 2 * level.bounds * k - x;
    return v2(course_left() + x, course_top() + (w.y + level.bounds) * k);
}

// touch (bottom screen px) -> the original's viewport px (course = 0..512)
// stylus pixels to the original's viewport pixels (256 / bounds of them per world unit); shots
// only use differences, so the origin does not matter
static vec2 touch_to_viewport(vec2 t) {
    float f = 256.0f / (level.bounds * course_scale());
    return v2(t.x * f, t.y * f);
}

// side strip buttons; the left-handed layout mirrors them
static float strip_x(float x, float w) { return set.left_handed ? BOT_W - x - w : x; }

static void unload_level(void) {
    if (floor_u) C2D_SpriteSheetFree(floor_u);
    if (floor_o) C2D_SpriteSheetFree(floor_o);
    if (dyn_sheet) C2D_SpriteSheetFree(dyn_sheet);
    floor_u = floor_o = dyn_sheet = NULL;
    if (level_loaded) level_free(&level);
    level_loaded = false;
}

static void to_select(void) {
    unload_level();
    state = ST_SELECT;
    music_play(1);
    thoughts_fallback("Idle");
    thoughts_play("Idle");
}

static bool start_level_(int w, int l);
static bool start_level(int w, int l) {
    PROF_BEGIN("level load");
    bool ok = start_level_(w, l);
    PROF_END();
    return ok;
}

static bool start_level_(int w, int l) {
    unload_level();
    char path[128];
    snprintf(path, sizeof(path), "%s/levels/w%d/%s.txt", data_root(), w, wnames[w][l]);
    PROF_BEGIN("level text");
    bool read = level_load(&level, path);
    PROF_END();
    if (!read) return false;
    level_loaded = true;
    PROF_BEGIN("floor pictures");
    snprintf(path, sizeof(path), "%s/levels/w%d/%s_u.t3x", data_root(), w, wnames[w][l]);
    floor_u = sheet_load(path);
    if (level.has_over) {
        snprintf(path, sizeof(path), "%s/levels/w%d/%s_o.t3x", data_root(), w, wnames[w][l]);
        floor_o = sheet_load(path);
    }
    if (level.ndspr > 0) { // pictures of the blocks, doors, keys and skulls
        snprintf(path, sizeof(path), "%s/levels/w%d/%s_d.t3x", data_root(), w, wnames[w][l]);
        dyn_sheet = sheet_load(path);
    }
    prefetch_drop(); // a picture this level turned out not to have
    PROF_END();
    cur_world = w;
    cur_level = l;
    game.ndemo = 0;
    gset = game_settings();
    game_start(&game, &level, MODE_NORMAL, &gset);
    camera_follow(game.pos, true);
    jumpscare_t = results_t = win_t = 0;
    btn_alpha[0] = btn_alpha[1] = btn_alpha[2] = 1;
    results_stage = 0;
    results_rank_shown = -1;
    dragging = false;
    music_play(level.music);
    // level.gd _ready(): Idle, falling back to Idle
    thoughts_play("Idle");
    thoughts_fallback("Idle");
    state = ST_PLAY;
    return true;
}

static bool level_after(int *w, int *l);
static void next_level(void) {
    int w = cur_world, l = cur_level;
    if (level_after(&w, &l)) start_level(w, l);
    else to_select();
}

// ---------------------------------------------------------------- transitions
// Screen changes fade through black (the original cuts straight across). The change itself happens
// on the first frame after the screen is fully black, so a level load's pause is never seen.
enum { TR_START, TR_NEXT, TR_SELECT, TR_SETTINGS, TR_CLOSE_SETTINGS, TR_INTRO_DONE };
#define TR_OUT (1.0f / 60 / 0.12f) // 0.12 s to black
#define TR_IN (1.0f / 60 / 0.16f)  // 0.16 s back
static int tr_phase = 2, tr_act, tr_a, tr_b; // phase 0 idle, 1 going dark, 2 coming back
static float tr_k = 1;                       // how dark (the game starts on black and fades in)
static void boot_finish(void);

static float tr_out = TR_OUT, tr_in = TR_IN; // the intro fades slower

// the level after (w, l), or false at the end of the game
static bool level_after(int *w, int *l) {
    if (*l + 1 < wcount[*w]) return ++*l, true;
    if (*w + 1 < NWORLDS && wcount[*w + 1] > 0) return ++*w, *l = 0, true;
    return false;
}

// start reading a level's pictures, so the load during the fade's black frame only copies them
static void prefetch_level(int w, int l) {
    static const char *const SUF[3] = {"u", "o", "d"};
    char path[160];
    for (int i = 0; i < 3; i++) {
        // a picture the level does not have just fails on the worker (opening files here would cost
        // the frame: on the SD card each open takes a while)
        snprintf(path, sizeof(path), "%s/levels/w%d/%s_%s.t3x", data_root(), w, wnames[w][l], SUF[i]);
        prefetch(path);
    }
}

static void go(int act, int a, int b) {
    if (tr_phase == 1) return; // one change at a time
    tr_phase = 1;
    tr_act = act, tr_a = a, tr_b = b;
    if (act == TR_START) prefetch_level(a, b);
    if (act == TR_NEXT) {
        int w = cur_world, l = cur_level;
        if (level_after(&w, &l)) prefetch_level(w, l);
    }
}

static void open_settings(int back);
static void close_settings_now(void);

static void transition_step(void) {
    if (tr_phase == 1 && tr_k >= 1) {
        switch (tr_act) {
        case TR_START: start_level(tr_a, tr_b); break;
        case TR_NEXT: next_level(); break;
        case TR_SELECT: to_select(); break;
        case TR_SETTINGS: open_settings(tr_a); break;
        case TR_CLOSE_SETTINGS: close_settings_now(); break;
        case TR_INTRO_DONE: boot_finish(); state = ST_SELECT; break;
        }
        tr_phase = 2;
    } else if (tr_phase == 1) {
        tr_k = fminf(1, tr_k + tr_out);
    } else if (tr_phase == 2) {
        tr_k -= tr_in;
        if (tr_k <= 0) tr_k = 0, tr_phase = 0, tr_out = TR_OUT, tr_in = TR_IN;
    }
}

static void draw_fade(float w) {
    if (tr_k <= 0) return;
    float a = tr_k * tr_k * (3 - 2 * tr_k); // smoothstep
    C2D_DrawRectSolid(0, 0, 1.0f, w, SCR_H, C2D_Color32f(0, 0, 0, a));
}

// ---------------------------------------------------------------- sounds and thoughts
static void play_events(uint32_t ev) {
    if (ev & EV_JOIN) sfx_play("game/levelStart.ogg", 1, 1);
    if (ev & EV_GRAB) sfx_play("player/grab.ogg", 1, 1);
    if (ev & EV_CANCEL) sfx_play("player/cancel.ogg", 1, 1);
    if (ev & EV_LAUNCH) sfx_play("player/hit.ogg", game.launch_pitch, 1);
    if (ev & EV_BUMP) sfx_play("player/wallBump.ogg", game.bump_pitch, 1);
    if (ev & EV_PUSH) sfx_play("player/push.ogg", 1, 1);
    if (ev & EV_PICKUP) sfx_play("player/notif.ogg", 1, 1);
    if (ev & EV_WIN) sfx_play("game/levelWin.ogg", 1, 0.58f);
    if (ev & EV_COIN) sfx_play("collectibles/coin.ogg", 1, 1);
    if (ev & EV_DEATH_WALL) sfx_play("minigame/meowl_die.ogg", 1, 1);
    if (ev & EV_OB) sfx_play("game/deathOB.ogg", 1, 1);
    if (ev & EV_CAUGHT && set.jumpscares != 2) {
        if (set.jumpscares == 0) sfx_play("ghost/defaultJumpscare.ogg", 1, 1);
        jumpscare_t = 50;
    }
    if (ev & EV_BACKROOMS) music_play(-2);
    if (ev & EV_DBLOCK) sfx_play("block/dblock.ogg", 1, 1);
    if (ev & EV_KEYDOOR) sfx_play("block/keydoor.ogg", 1, 1);
    if (ev & EV_KEY) sfx_play("collectibles/key.ogg", 1, 1);
    if (ev & EV_SKULL) sfx_play("enemy/skullDeath.ogg", game.skull_pitch, 1);
    if (ev & EV_BOOST) sfx_play("collectibles/boost.ogg", 1, 1);
    if (ev & EV_JUMP) sfx_play("gimmicks/jump_pad.ogg", 1, 1);
    if (ev & EV_POLICE) { // 2-11's "police" player: the siren and the spotlight, together
        sfx_play("gimmicks/policesiren.ogg", 1, 1);
        sfx_play("gimmicks/spotlight.ogg", 1, 1);
    }
    if (ev & EV_CAR) {
        sfx_play("enemy/carDeath.ogg", 1, 1);
        // car.tscn horn: AudioStreamRandomizer of two horns, random_pitch 1.2, -10 dB
        float pitch = powf(1.2f, (rand() / (float)RAND_MAX) * 2 - 1);
        sfx_play(rand() % 2 ? "enemy/carHorn1.ogg" : "enemy/carHorn2.ogg", pitch, 0.316f);
    }
    if (ev & EV_SPLASH) {
        static const char *SPLASH[3] = {"player/water_splash1.ogg", "player/water_splash2.ogg", "player/water_splash3.ogg"};
        sfx_play(SPLASH[rand() % 3], 1, 1); // the player's AudioStreamRandomizer
    }
    if (ev & EV_WATER_DEATH) sfx_play("player/water_death.ogg", 1, 1);
}

static void play_thought(int th) {
    switch (th) {
    case TH_IDLE: thoughts_play("Idle"); break;
    case TH_MOVING: thoughts_play("Moving"); break;
    case TH_MONEY: thoughts_play("Money"); break;
    case TH_SAND: thoughts_play("Sand"); break;
    case TH_SKULL: thoughts_play("Skull"); break;
    case TH_CAR: thoughts_play("Car"); break;
    case TH_JUMP: thoughts_play("Jump"); break;
    case TH_POLICE: thoughts_play("Police"); thoughts_fallback("Police"); break;
    case TH_WIN: thoughts_play("Win"); thoughts_fallback("Win"); break;
    case TH_DEATH: thoughts_play("Death"); thoughts_fallback("Death"); break;
    case TH_BACKROOMS: thoughts_play("Backrooms"); thoughts_fallback("Backrooms"); break;
    }
}

// ---------------------------------------------------------------- drawing helpers
// citro2d keeps the higher depth on top, so UI layers raise ui_z before drawing.
static float ui_z = 0.5f;

// Fonts: the original's own (converted with mkbcfnt). Sizes passed around are in
// "system font" units, so a string that needs the system font (Japanese, Russian,
// anything the game's font lacks) comes out the same height.
static C2D_Font font_ui, font_bubble, font_square, cur_font;
static float lf_sys = 30;
static const u32 BLACK = 0xFF000000;
static u32 ol_color = 0xFF000000; // outline colour; level select labels use blue like the original

static C2D_Font font_for(const char *s) {
    if (!cur_font) return NULL;
    u16 alt = C2D_FontGetInfo(cur_font)->alterCharIndex;
    const u8 *p = (const u8 *)s;
    while (*p) {
        u32 cp;
        ssize_t n = decode_utf8(&cp, p);
        if (n <= 0) break;
        p += n;
        if (cp >= 0x20 && C2D_FontGlyphIndexFromCodePoint(cur_font, cp) == alt) return NULL;
    }
    return cur_font;
}

// Comic Sans (every game font) has a tall line for its letters and a thin stroke, so it is drawn a
// quarter larger than the line-height match to read like the original's Arial Rounded / SuperBubble
#define FONT_GROW 1.3f
static float font_k(C2D_Font f, float size) {
    return size * lf_sys / C2D_FontGetInfo(f)->lineFeed * (f ? FONT_GROW : 1);
}

static void str_draw(float x, float y, float size, u32 color, u32 flags, float width, const char *s, bool ol) {
    C2D_Text t;
    C2D_Font f = font_for(s);
    C2D_TextFontParse(&t, f, tbuf, s);
    C2D_TextOptimize(&t);
    float k = font_k(f, size);
    if (ol) { // the original's outline_size labels: eight copies around the text
        float r = 0.45f + size * 1.0f; // thin enough that small text keeps its holes
        static const float o[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
        for (int i = 0; i < 8; i++)
            C2D_DrawText(&t, C2D_WithColor | flags, x + o[i][0] * r, y + o[i][1] * r, ui_z, k, k, ol_color, width);
    }
    C2D_DrawText(&t, C2D_WithColor | flags, x, y, ui_z + 0.001f, k, k, color, width);
    if (f) // Comic Sans Regular is the only cut the game ships: a second strike half a pixel over makes it bold
        C2D_DrawText(&t, C2D_WithColor | flags, x + 0.6f, y, ui_z + 0.002f, k, k, color, width);
}

static float str_width(float size, const char *s) {
    C2D_Text t;
    C2D_Font f = font_for(s);
    C2D_TextFontParse(&t, f, tbuf, s);
    float w, h, k = font_k(f, size);
    C2D_TextGetDimensions(&t, k, k, &w, &h);
    return w;
}

static void text(float x, float y, float size, u32 color, u32 flags, const char *s) {
    str_draw(x, y, size, color, flags, 0, s, false);
}

static void text_ol(float x, float y, float size, u32 color, u32 flags, const char *s) {
    str_draw(x, y, size, color, flags, 0, s, true);
}

static void text_ol_wrap(float x, float y, float size, u32 color, u32 flags, float width, const char *s) {
    str_draw(x, y, size, color, flags | C2D_WordWrap, width, s, true);
}

static u32 hsv(float h, float s, float v, float a) {
    h = fmodf(h, 1.0f);
    if (h < 0) h += 1;
    float r, g, b, f = h * 6;
    int i = (int)f;
    f -= i;
    float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i % 6) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    return C2D_Color32f(r, g, b, a);
}

// [rainbow freq=f sat=f val=1.0]: Godot colours each glyph from hsv(freq * (t + x / 50))
static void text_rainbow(float x, float y, float size, float freq, const char *s) {
    float cx = x;
    char ch[8];
    for (const char *p = s; *p;) {
        int n = 1;
        unsigned char c = *p;
        if (c >= 0xF0) n = 4;
        else if (c >= 0xE0) n = 3;
        else if (c >= 0xC0) n = 2;
        memcpy(ch, p, n);
        ch[n] = 0;
        p += n;
        float w = str_width(size, ch);
        float orig_x = (cx - x) * 25.0f / (30.0f * size); // in the original's 25 px font
        text_ol(cx, y, size, hsv(freq * (rainbow_t + orig_x / 50.0f), freq, 1, 1), 0, ch);
        cx += w;
    }
}

static void draw_spr(int i, vec2 screen, float sx, float sy, u32 tint_rgba, float angle) {
    if (i < 0 || !*sprinfo[i].sheet) return;
    SpriteInfo *s = &sprinfo[i];
    C2D_Image img = C2D_SpriteSheetGetImage(*s->sheet, s->idx);
    C2D_DrawParams p = {{screen.x, screen.y, img.subtex->width * sx, img.subtex->height * sy},
                        {s->ax * sx, s->ay * sy}, ui_z, angle};
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, tint_rgba, 1.0f); // tint mode is multiply (set at start-up)
    C2D_DrawImage(img, &p, &tint);
}

static void draw_sprite(const char *name, vec2 screen, float px_per_unit, float extra_scale, u32 tint_rgba,
                        float angle) {
    int i = spr_find(name);
    if (i < 0) return;
    float k = px_per_unit * extra_scale;
    draw_spr(i, screen, sprinfo[i].ux * k, sprinfo[i].uy * k, tint_rgba, angle);
}

// the uncanny face, 480x480, across both screens as one stacked 400x480 picture (bottom screen is 40px in)
static void draw_jumpscare(bool top) {
    if (!jumpscare_sheet) {
        draw_sprite("costume_uncanny", v2(top ? 200 : 160, 120), top ? 6.0f : 4.0f, 1, 0xFFFFFFFF, 0);
        return;
    }
    C2D_DrawImageAt(C2D_SpriteSheetGetImage(jumpscare_sheet, 0), top ? -40 : -80, top ? 0 : -240, 0.99f, NULL,
                    480.0f / 512, 480.0f / 512);
}

static void draw_hud(const char *name, float x, float y, float scale, u32 tint) {
    draw_spr(spr_find(name), v2(x, y), scale, scale, tint, 0);
}

static const u32 WHITE = 0xFFFFFFFF;

static u32 shade_color(float a) {
    return C2D_Color32f(level.shade[0], level.shade[1], level.shade[2], level.shade[3] * a);
}

static u32 grey(float v) { return C2D_Color32f(v, v, v, 1); }

// part (sx, sy, sw, sh in image px) of a picture, stretched over a screen rect
static void draw_img_part(C2D_Image img, float sx, float sy, float sw, float sh, float dx, float dy, float dw,
                          float dh, u32 tint_rgba) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    const Tex3DS_SubTexture *s = img.subtex;
    float du = (s->right - s->left) / s->width, dv = (s->bottom - s->top) / s->height;
    Tex3DS_SubTexture sub = {(u16)sw, (u16)sh, s->left + du * sx, s->top + dv * sy, s->left + du * (sx + sw),
                             s->top + dv * (sy + sh)};
    C2D_Image part = {img.tex, &sub};
    C2D_DrawParams p = {{dx, dy, dw, dh}, {0, 0}, ui_z, 0};
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, tint_rgba, 1.0f);
    C2D_DrawImage(part, &p, &tint);
}

// a StyleBoxTexture: corners kept, edges and middle stretched (sm: margin in the
// picture, dm: margin on screen)
static void nine(const char *name, float x, float y, float w, float h, float sm, float dm, u32 tint) {
    int i = spr_find(name);
    if (i < 0 || !*sprinfo[i].sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(*sprinfo[i].sheet, sprinfo[i].idx);
    float iw = img.subtex->width, ih = img.subtex->height;
    dm = fminf(dm, fminf(w, h) / 2);
    float sx[3] = {0, sm, iw - sm}, sw[3] = {sm, iw - 2 * sm, sm};
    float sy[3] = {0, sm, ih - sm}, sh[3] = {sm, ih - 2 * sm, sm};
    float dx[3] = {x, x + dm, x + w - dm}, dw[3] = {dm, w - 2 * dm, dm};
    float dy[3] = {y, y + dm, y + h - dm}, dh[3] = {dm, h - 2 * dm, dm};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) draw_img_part(img, sx[c], sy[r], sw[c], sh[r], dx[c], dy[r], dw[c], dh[r], tint);
}

// the menu background: menu_bg.png tiled, scrolling like its autoscroll (-10, -5)
static void draw_menu_bg(float z) {
    int i = spr_find("menu_tile");
    if (i < 0) {
        C2D_DrawRectSolid(0, 0, z, BOT_W, SCR_H, grey(0.75f));
        return;
    }
    const float T = 100; // the 200 px tile at half size
    float ox = -fmodf(rainbow_t * 5.0f, T), oy = -fmodf(rainbow_t * 2.5f, T);
    float keep = ui_z;
    ui_z = z;
    for (float y = oy; y < SCR_H; y += T)
        for (float x = ox; x < BOT_W; x += T) draw_spr(i, v2(x, y), 0.5f, 0.5f, WHITE, 0);
    ui_z = keep;
}

// the button the stylus just pressed shows the theme's pressed modulate for a moment
static float press_rect[4];
static int press_t;

static bool hit_box(vec2 t, float x, float y, float w, float h) {
    return t.x >= x && t.x < x + w && t.y >= y && t.y < y + h;
}

static void press_mark(float x, float y, float w, float h) {
    press_rect[0] = x, press_rect[1] = y, press_rect[2] = w, press_rect[3] = h;
    press_t = 8;
}

static bool tap_box(vec2 t, float x, float y, float w, float h) {
    if (!hit_box(t, x, y, w, h)) return false;
    press_mark(x, y, w, h);
    return true;
}

static bool pressed_now(float x, float y, float w, float h) {
    return press_t > 0 && press_rect[0] == x && press_rect[1] == y && press_rect[2] == w && press_rect[3] == h;
}

// default.tres: button.png nine-patch (margin 16), button_focus.png with the hover
// modulate when selected, 0.745 while pressed; Arial Rounded, black outline,
// font colour 0.875 (0.95 when focused)
static float btn_alpha_now = 1; // the course buttons' see-through amount, for button_box

static void button_box(float x, float y, float w, float h, bool hl) {
    bool pr = pressed_now(x, y, w, h);
    float g = pr ? 0.745f : 1;
    nine(hl ? "btn_hi" : "btn", x, y, w, h, 16, fminf(12, fminf(w, h) / 3), C2D_Color32f(g, g, g, btn_alpha_now));
}

static void button(float x, float y, float w, float h, const char *label, bool hl) {
    float z = ui_z;
    button_box(x, y, w, h, hl);
    ui_z = z + 0.002f;
    float sz = h >= 30 ? 0.5f : 0.44f;
    C2D_Font keep = cur_font;
    cur_font = font_ui;
    text_ol(x + w / 2, y + h / 2 - sz * lf_sys * 0.5f, sz, grey(hl ? 0.95f : 0.875f), C2D_AlignCenter, label);
    cur_font = keep;
    ui_z = z;
}

// a triangle with a black edge, for the arrow buttons (the original uses ⬆ ⬇ glyphs)
static void arrow(float cx, float cy, float s, int dir) {
    float d = dir < 0 ? -1 : 1;
    for (int pass = 0; pass < 2; pass++) {
        float k = pass ? s : s + 2.5f;
        u32 c = pass ? grey(0.875f) : BLACK;
        C2D_DrawTriangle(cx, cy - d * k, c, cx - k, cy + d * k * 0.75f, c, cx + k, cy + d * k * 0.75f, c, ui_z + pass * 0.001f);
    }
}

// the same, pointing left (-1) or right (1)
static void arrow_lr(float cx, float cy, float s, int dir) {
    float d = dir < 0 ? -1 : 1;
    for (int pass = 0; pass < 2; pass++) {
        float k = pass ? s : s + 2.5f;
        u32 c = pass ? grey(0.875f) : BLACK;
        C2D_DrawTriangle(cx + d * k, cy, c, cx - d * k * 0.75f, cy - k, c, cx - d * k * 0.75f, cy + k, c, ui_z + pass * 0.001f);
    }
}

// Utils.format_seconds + format_seconds_mil
static void format_time(char *mmss, size_t n, char *mil, size_t m, float t) {
    snprintf(mmss, n, "%02d:%02d", (int)(t / 60), (int)fmodf(t, 60));
    snprintf(mil, m, ".%02d", (int)(fmodf(t, 1) * 100));
}

static const char *RANK_NAMES[6] = {"UNCANNY", "ATROCIOUS", "STINKY", "OK", "SWAG", "PEAK"};

// ---------------------------------------------------------------- course
static void draw_dyn(int spr, vec2 world, float k, u32 tint) {
    if (spr < 0 || spr >= level.ndspr || !dyn_sheet) return;
    const DynSprite *d = &level.dspr[spr];
    C2D_Image img = C2D_SpriteSheetGetImage(dyn_sheet, spr);
    vec2 s = world_to_screen(world);
    float sx = (level.flags & LF_MIRROR) ? -k : k;
    C2D_DrawParams p = {{s.x, s.y, d->w * sx, d->h * k}, {d->ax * sx, d->ay * k}, ui_z, 0};
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 1.0f);
    C2D_DrawImage(img, &p, &t);
}

static void popup_text(vec2 world, int score, float popup) {
    char b[16];
    snprintf(b, sizeof(b), "%d", score);
    vec2 p = world_to_screen(world);
    text_ol(p.x, p.y - 14 - (1 - popup) * 10, 0.45f, C2D_Color32(255, 255, 120, 255), C2D_AlignCenter, b);
}

// is a circle (world centre, world radius) really over a button? The rect is shrunk to the button's
// drawn face (its corners are round) and the circle has to reach a few pixels in, so something only
// passing near the button leaves it solid
static bool circle_under(vec2 world, float r, float x, float y, float w, float h) {
    vec2 c = world_to_screen(world);
    float rr = r * course_scale() - 3;
    if (rr <= 0) rr = 1;
    x += 4, y += 4, w -= 8, h -= 8;
    float nx = fmaxf(x, fminf(c.x, x + w)), ny = fmaxf(y, fminf(c.y, y + h));
    return (c.x - nx) * (c.x - nx) + (c.y - ny) * (c.y - ny) < rr * rr;
}

// anything that moves or can be picked up, under a button: the cat, ghosts, keys, skulls, coins, the goal
static bool course_under(float x, float y, float w, float h) {
    if (game.sprite_visible && circle_under(game.pos, PLAYER_RADIUS * game.sprite_scale / 0.064f, x, y, w, h))
        return true;
    for (int i = 0; i < game.nghosts; i++)
        if (game.ghosts[i].alive && circle_under(game.ghosts[i].pos, PLAYER_RADIUS, x, y, w, h)) return true;
    for (int i = 0; i < game.nkeys; i++)
        if (game.keys[i].alive && circle_under(game.keys[i].pos, 14, x, y, w, h)) return true;
    for (int i = 0; i < game.nbodies; i++)
        if (game.bodies[i].alive && (game.bodies[i].type == OBJ_SKULL || game.bodies[i].type == OBJ_CAR) &&
            circle_under(game.bodies[i].pos, game.bodies[i].radius, x, y, w, h))
            return true;
    for (int i = 0; i < game.ncoins; i++)
        if (!game.coins[i].taken && circle_under(game.coins[i].pos, game.coins[i].r, x, y, w, h)) return true;
    if (game.has_goal && circle_under(game.goal_pos, 14, x, y, w, h)) return true;
    return false;
}

static void draw_course(void) {
    float k = course_scale();
    C2D_DrawRectSolid(0, 0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, 255));
    if (floor_u) {
        C2D_Image img = C2D_SpriteSheetGetImage(floor_u, 0);
        float cw = 2 * level.bounds * k, s = cw / level.image_size;
        if (level.flags & LF_MIRROR) C2D_DrawImageAt(img, course_left() + cw, course_top(), 0.1f, NULL, -s, s);
        else C2D_DrawImageAt(img, course_left(), course_top(), 0.1f, NULL, s, s);
    }
    // fixed depths, bottom to top (goal.tscn: hole and flag under player.tscn's cat); citro2d keeps the
    // higher depth in front whatever the drawing order
    ui_z = 0.2f;
    if (game.has_goal) draw_sprite("goal", world_to_screen(game.goal_pos), k, 1, shade_color(1), 0);
    ui_z = 0.22f; // jump pads, spawner doors
    for (int i = 0; i < game.npads; i++) draw_dyn(game.pads[i].spr, game.pads[i].pos, k, WHITE);
    for (int i = 0; i < game.nspawners; i++) draw_dyn(game.spawners[i].spr, game.spawners[i].pos, k, WHITE);
    ui_z = 0.25f;
    for (int i = 0; i < game.ncoins; i++) {
        if (game.coins[i].taken) continue;
        if (game.coins[i].spr >= 0) draw_dyn(game.coins[i].spr, game.coins[i].pos, k, WHITE);
        else draw_sprite("coin", world_to_screen(game.coins[i].pos), k, 1, shade_color(1), 0);
    }
    // blocks, doors, keys and skulls: pictures baked by the converter with their
    // rotation, scale and shading, one world unit per pixel
    if (dyn_sheet) {
        ui_z = 0.3f;
        for (int i = 0; i < game.nbodies; i++) {
            const Body *b = &game.bodies[i];
            if (!b->alive) continue;
            draw_dyn(b->spr, b->pos, k, WHITE);
            if (b->type == OBJ_KEYDOOR && b->lock_a > 0.01f)
                draw_dyn(b->spr2, b->pos, k, C2D_Color32f(1, 1, 1, b->lock_a));
        }
        ui_z = 0.33f;
        for (int i = 0; i < game.nkeys; i++) {
            const Key *kk = &game.keys[i];
            if (kk->alive) draw_dyn(kk->spr, kk->pos, k, C2D_Color32f(1, 1, 1, kk->alpha));
        }
    }
    ui_z = game.pstate == PS_JUMP ? 0.72f : 0.4f; // PJump: z_index 1, over everything in the level
    if (game.sprite_visible) {
        float sc = game.sprite_scale / 0.064f;
        draw_sprite("costume_canny", world_to_screen(game.pos), k, sc, shade_color(1), 0);
    }
    ui_z = 0.45f;
    for (int i = 0; i < game.nghosts; i++)
        if (game.ghosts[i].alive) draw_sprite("costume_uncanny", world_to_screen(game.ghosts[i].pos), k, 1, shade_color(1), 0);
    if (game.hit_visible && game.pstate == PS_IDLE) {
        float c[4];
        game_aim_color(&game, c);
        vec2 a = world_to_screen(game.pos);
        vec2 hit = game.hit;
        if (level.flags & LF_MIRROR) hit.x = -hit.x;
        vec2 b = v2add(a, v2mul(hit, k));
        u32 col = C2D_Color32f(c[0], c[1], c[2], c[3]);
        C2D_DrawLine(a.x, a.y, col, b.x, b.y, col, fmaxf(2.0f, 4 * k), 0.6f);
    }
    if (floor_o) {
        C2D_Image img = C2D_SpriteSheetGetImage(floor_o, 0);
        float cw = 2 * level.bounds * k, s = cw / level.image_size;
        if (level.flags & LF_MIRROR) C2D_DrawImageAt(img, course_left() + cw, course_top(), 0.7f, NULL, -s, s);
        else C2D_DrawImageAt(img, course_left(), course_top(), 0.7f, NULL, s, s);
    }
    // score popups (coins, blocks, doors, keys, skulls)
    ui_z = 0.75f;
    for (int i = 0; i < game.ncoins; i++)
        if (game.coins[i].popup > 0) popup_text(game.coins[i].pos, game.coins[i].score, game.coins[i].popup);
    for (int i = 0; i < game.nbodies; i++)
        if (game.bodies[i].popup > 0) popup_text(game.bodies[i].pos, game.bodies[i].popup_score, game.bodies[i].popup);
    for (int i = 0; i < game.nkeys; i++)
        if (game.keys[i].popup > 0) popup_text(game.keys[i].pos, game.keys[i].popup_score, game.keys[i].popup);
    // side strips: a slice of the sidebar background (hud_bg.png), so both screens match
    ui_z = 0.8f;
    if (set.zoom) {
        // no strips: the buttons sit over the course
    } else if (top_bg) {
        C2D_Image bg = C2D_SpriteSheetGetImage(top_bg, 0);
        draw_img_part(bg, 240, 0, COURSE_X, SCR_H, 0, 0, COURSE_X, SCR_H, WHITE);
        draw_img_part(bg, 240, 0, COURSE_X, SCR_H, COURSE_X + COURSE_W, 0, COURSE_X, SCR_H, WHITE);
    } else {
        C2D_DrawRectSolid(0, 0, 0.8f, COURSE_X, SCR_H, BLACK);
        C2D_DrawRectSolid(COURSE_X + COURSE_W, 0, 0.8f, COURSE_X, SCR_H, BLACK);
    }
    // the buttons: in the zoomed view they sit over the course, and go see-through while
    // the cat or anything else is behind one
    ui_z = 0.85f;
    float px = strip_x(2, 36), rx = strip_x(282, 36);
    float bx[3] = {px, rx, rx}, by[3] = {4, 4, 196};
    for (int b = 0; b < 3; b++) {
        float want = set.zoom && course_under(bx[b], by[b], 36, 40) ? 0.35f : 1;
        btn_alpha[b] += (want - btn_alpha[b]) * 0.2f;
        if (fabsf(want - btn_alpha[b]) < 0.01f) btn_alpha[b] = want;
    }
    btn_alpha_now = btn_alpha[0];
    button_box(px, 4, 36, 40, false);
    btn_alpha_now = btn_alpha[1];
    button_box(rx, 4, 36, 40, false);
    ui_z = 0.86f;
    draw_hud("pause_icon", px + 18, 24, 0.3f, C2D_Color32f(1, 1, 1, btn_alpha[0]));
    text(rx + 18, 24 - 0.85f * lf_sys * 0.5f, 0.85f, C2D_Color32f(0.2f, 0.2f, 0.2f, btn_alpha[1]), C2D_AlignCenter,
         ""); // the Y button
    if (!(level.flags & LF_UNSKIPPABLE)) {
        ui_z = 0.85f;
        btn_alpha_now = btn_alpha[2];
        button_box(rx, 196, 36, 40, game.skipcounter > 0);
        ui_z = 0.86f;
        char sk[8];
        snprintf(sk, sizeof(sk), "skip%d", game.skipcounter > 0 ? 1 + (int)(rainbow_t * 6) % 2 : 0);
        draw_hud(sk, rx + 18, 216, 0.62f, C2D_Color32f(1, 1, 1, btn_alpha[2]));
        if (game.skipcounter > 0)
            C2D_DrawRectSolid(rx + 2, 191, 0.9f, 32 * game.skipcounter / (float)SKIPCOUNT, 3,
                              C2D_Color32f(1, 0.98f, 0.4f, btn_alpha[2]));
    }
    btn_alpha_now = 1;
}

// ---------------------------------------------------------------- top screen
// The Cat Thought Displayer 9000, scaled 0.17 where the original used 0.148607
#define DCX 289.0f
#define DCY 121.0f
#define DK 1.1440f

static void draw_top_bg(void) {
    if (top_bg) C2D_DrawImageAt(C2D_SpriteSheetGetImage(top_bg, 0), 0, 0, 0.05f, NULL, 1, 1);
    else C2D_DrawRectSolid(0, 0, 0.05f, TOP_W, SCR_H, C2D_Color32(150, 120, 190, 255));
}

static void draw_displayer(void) {
    C2D_DrawRectSolid(DCX - 62.9f, DCY - 83.5f, 0.2f, 124.7f, 101.8f, C2D_Color32(23, 23, 23, 255));
    C2D_Image img;
    float tw = 109 * 1.01f * DK, th = 89 * 1.01f * DK;
    PROF_BEGIN("thought frame");
    bool have_img = thoughts_image(&img);
    PROF_END();
    if (have_img) C2D_DrawImageAt(img, DCX - 1.14f - tw / 2, DCY - 33.2f - th / 2, 0.25f, NULL, tw / 109, th / 89);
    else draw_sprite("costume_canny", v2(DCX - 1, DCY - 33), 1.6f, 1, WHITE, 0);
    ui_z = 0.3f;
    draw_hud("displayer", DCX, DCY, 1, WHITE);
    // the Thinker: Canny next to the computer
    ui_z = 0.32f;
    draw_sprite("costume_canny", v2(170, 204), 1.7f, 1, WHITE, 0);
}

static void draw_top_hud(void) {
    char b[192], v[32], ms[8];
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    if (!(level.flags & LF_HIDE_SCORING)) {
        char key[48];
        snprintf(key, sizeof(key), "GAME_Level%s", level.id);
        snprintf(b, sizeof(b), "%s", tr(key));
        { // one line, shrunk to fit, so a long name never runs into "Level by"
            float sz = 0.5f, w = str_width(sz, b);
            if (w > 168) sz = fmaxf(0.3f, sz * 168 / w);
            if (str_width(sz, b) > 168) text_ol_wrap(8, 72, sz, WHITE, 0, 168, b);
            else text_ol(8, 80, sz, WHITE, 0, b);
        }
        snprintf(b, sizeof(b), "Level by: %s", level.author);
        text_ol(90, 104, 0.38f, WHITE, C2D_AlignCenter, b);
        // strokes, with the rainbow fading as strokes go over par (level.gd)
        float mod = 0.5f;
        if (game.strokes > game.par) mod = fmaxf(0, 0.5f - 0.05f * (game.strokes - game.par));
        snprintf(v, sizeof(v), "%d", game.strokes);
        trf(b, sizeof(b), "GAME_Strokes", "stroke", v);
        text_rainbow(8, 120, 0.62f, mod, b);
        snprintf(v, sizeof(v), "%d", game.level_bonus);
        trf(b, sizeof(b), "GAME_LevelBonus", "bonus", v);
        text_ol(8, 142, 0.42f, C2D_Color32f(1, 0.98f, 0.4f, 1), 0, b);
        // par: the spinning ball with the number on it
        ui_z = 0.45f;
        char ball[16];
        snprintf(ball, sizeof(ball), "par_ball%d", (int)(rainbow_t * 8) % 4);
        draw_hud(ball, 150, 140, 1, WHITE);
        ui_z = 0.5f;
        text_ol(150, 106, 0.42f, WHITE, C2D_AlignCenter, tr("GAME_Par"));
        snprintf(v, sizeof(v), "%d", game.par);
        text_ol(150, 128, 0.62f, WHITE, C2D_AlignCenter, v);
        // timer
        ui_z = 0.45f;
        draw_hud("timer", 66, 188, 1, WHITE);
        ui_z = 0.5f;
        format_time(v, sizeof(v), ms, sizeof(ms), game.time);
        u32 tc = game.time > level.on_time + 25 ? C2D_Color32(255, 0, 0, 255)
                 : game.time >= level.on_time    ? C2D_Color32(255, 255, 0, 255)
                                                 : C2D_Color32(0, 255, 0, 255);
        text(58, 176, 0.75f, tc, C2D_AlignCenter, v);
        text(104, 184, 0.45f, tc, C2D_AlignCenter, ms);
    }
}

static void draw_results_top(void) {
    char b[192], v[32], ms[8];
    draw_top_bg();
    // ResultsSlideDown: the panel drops in from the top
    float slide = fminf(1, results_t / 30.0f);
    float oy = -240 * (1 - slide) * (1 - slide);
    ui_z = 0.4f;
    draw_hud("results_bg", 200, 120 + oy, 1, WHITE);
    ui_z = 0.45f;
    draw_hud("results_clear", 200, 24 + oy, 1, C2D_Color32(255, 255, 0, 255));
    ui_z = 0.5f;
    // tStrokes / tStrokes2
    if (game.strokes > 1) {
        snprintf(v, sizeof(v), "%d", game.strokes);
        trf(b, sizeof(b), "GAME_Strokes", "stroke", v);
    } else snprintf(b, sizeof(b), "%s", tr(game.strokes == 1 ? "GAME_ResultStrokes1" : "GAME_ResultStrokes0"));
    text_ol(26, 44 + oy, 0.55f, WHITE, 0, b);
    char l1[96], l2[64];
    u32 c2;
    int d = game.strokes - game.par;
    snprintf(v, sizeof(v), "%d", d < 0 ? -d : d);
    if (d < 0) trf(l1, sizeof(l1), "GAME_ResultStrokesBelow4+", "stroke", v), c2 = C2D_Color32(0, 255, 0, 255);
    else if (d == 0) snprintf(l1, sizeof(l1), "%s", tr("GAME_ResultStrokesEven")), c2 = C2D_Color32(0, 255, 0, 255);
    else trf(l1, sizeof(l1), "GAME_ResultStrokesAbove4+", "stroke", v), c2 = C2D_Color32f(1, 0.2f, 0, 1);
    snprintf(v, sizeof(v), "%d", game.stroke_bonus);
    trf(l2, sizeof(l2), "GAME_ResultBonus", "bonus", v);
    snprintf(b, sizeof(b), "%s\n%s", l1, l2);
    text_ol(30, 64 + oy, 0.42f, c2, 0, b);
    // tTime / tTime2
    format_time(v, sizeof(v), ms, sizeof(ms), game.time);
    strcat(v, ms);
    trf(b, sizeof(b), "GAME_Time", "time", v);
    text_ol(26, 98 + oy, 0.55f, WHITE, 0, b);
    if (game.time >= level.on_time) {
        int tardy = (int)(game.time - level.on_time);
        if (tardy < 1) snprintf(l1, sizeof(l1), "%s", tr("GAME_ResultBarelyLate")), c2 = C2D_Color32(255, 255, 0, 255);
        else {
            snprintf(v, sizeof(v), "%d", tardy);
            trf(l1, sizeof(l1), "GAME_ResultLate", "sec", v);
            c2 = C2D_Color32f(1, 0.2f, 0, 1);
        }
    } else snprintf(l1, sizeof(l1), "%s", tr("GAME_ResultOnTime")), c2 = C2D_Color32(0, 255, 0, 255);
    snprintf(v, sizeof(v), "%d", game.time_bonus);
    trf(l2, sizeof(l2), "GAME_ResultBonus", "bonus", v);
    snprintf(b, sizeof(b), "%s\n%s", l1, l2);
    text_ol(30, 118 + oy, 0.42f, c2, 0, b);
    snprintf(v, sizeof(v), "%d", game.level_bonus);
    trf(b, sizeof(b), "GAME_LevelBonus", "bonus", v);
    text_ol(26, 152 + oy, 0.55f, WHITE, 0, b);
    // score count-up (fill_bar's easing) and the rank bar with its prisms
    float t = results_stage >= 2 ? 1 : results_stage == 1 ? fminf(1, (results_t - 30) / (2.4f * 60)) : 0;
    t = fminf(1, (1 - powf(2, -8 * t)) / (1 - powf(2, -8)));
    int shown = (int)(game.final_score * t);
    int bar_rank = 0;
    for (int i = 1; i < 6; i++)
        if (shown >= level.ranks[i]) bar_rank = i;
    ui_z = 0.45f;
    draw_hud("results_rankbarback", 56, 190 + oy, 1, WHITE);
    int fi = spr_find("results_rankbarfill");
    if (fi >= 0 && hudsheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(hudsheet, sprinfo[fi].idx);
        float frac = fminf(1, shown / (float)level.ranks[5]);
        Tex3DS_SubTexture st = *img.subtex;
        st.width = (u16)(st.width * frac);
        st.right = st.left + (img.subtex->right - img.subtex->left) * frac;
        img.subtex = &st;
        ui_z = 0.46f;
        if (st.width > 0) C2D_DrawImageAt(img, 56, 190 + oy, ui_z, NULL, 1, 1);
    }
    ui_z = 0.48f;
    for (int i = 1; i < 6; i++) {
        float x = 56 + 288 * level.ranks[i] / (float)level.ranks[5];
        bool got = bar_rank >= i;
        C2D_DrawTriangle(x, 189 + oy, got ? C2D_Color32(140, 255, 255, 255) : C2D_Color32(90, 90, 110, 255),
                         x - 5, 196 + oy, got ? C2D_Color32(80, 160, 255, 255) : C2D_Color32(60, 60, 80, 255),
                         x + 5, 196 + oy, got ? C2D_Color32(80, 160, 255, 255) : C2D_Color32(60, 60, 80, 255), ui_z);
        C2D_DrawTriangle(x - 5, 196 + oy, got ? C2D_Color32(80, 160, 255, 255) : C2D_Color32(60, 60, 80, 255),
                         x + 5, 196 + oy, got ? C2D_Color32(80, 160, 255, 255) : C2D_Color32(60, 60, 80, 255),
                         x, 203 + oy, got ? C2D_Color32(40, 80, 200, 255) : C2D_Color32(40, 40, 60, 255), ui_z);
    }
    ui_z = 0.5f;
    snprintf(v, sizeof(v), "%d", shown);
    trf(b, sizeof(b), "GAME_ResultFinal", "score", v);
    text_ol(200, 205 + oy, 0.5f, WHITE, C2D_AlignCenter, b);
    if (results_stage >= 2) {
        ui_z = 0.55f;
        char rn[16];
        snprintf(rn, sizeof(rn), "rank_%d", game.rank);
        // centred in the board's free right part: between the score lines (they end near x 140) and the
        // chalkboard's right edge (382), and between the title (42) and the rank bar (188)
        draw_hud(rn, (140 + 382) / 2.0f, (42 + 188) / 2.0f + oy, 1, WHITE);
        ui_z = 0.6f;
        if (game.rank < 5) {
            snprintf(v, sizeof(v), "%d", level.ranks[game.rank + 1] - game.final_score);
            trf(b, sizeof(b), "GAME_ToNextRank", "number", v);
            text_ol(200, 222 + oy, 0.38f, C2D_Color32f(0.77f, 0.77f, 0.77f, 1), C2D_AlignCenter, b);
        }
    }
}

// ---------------------------------------------------------------- level select
// Laid out like level_select.tscn: a grid of square level buttons (button_1.tres,
// "0-1" in Square 721), the world name with a blue outline, ⬆ ⬇ world buttons
// and the red GO! button; the selected level's stats go on the top screen.
#define GRID_COLS 4
#define GRID_ROWS 4
#define CELL 50.0f
#define GRID_X 6.0f
#define GRID_Y 38.0f
#define SBAR_X 210.0f
#define WBTN_X 244.0f
#define GO_X 274.0f
#define GO_Y 186.0f

static const u32 BLUE = 0xFFFF0000;               // Color(0, 0, 1): the level select's outline colour
static const u32 PEAKED = 0xFF5AF1FF;             // peaked_modulate (1, 0.946, 0.35)

static int sel_rows(void) { return (wcount[sel_world] + GRID_COLS - 1) / GRID_COLS; }

static int level_rank(int w, int i) {
    char key[32];
    snprintf(key, sizeof(key), "%d-%d_rank", w, i + 1);
    return cfg_get_int(savedata, "LEVELS", key, -1);
}

static void draw_select(void) {
    char b[48], key[32];
    draw_menu_bg(0);
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    snprintf(key, sizeof(key), "GAME_World%d", sel_world);
    text_ol(GRID_X + GRID_COLS * CELL / 2, 6, 0.62f, WHITE, C2D_AlignCenter, tr(key));
    ol_color = BLACK;
    button(232, 4, 84, 28, tr("MENU_Settings"), false);
    // the grid
    int n = wcount[sel_world];
    for (int r = 0; r < GRID_ROWS; r++)
        for (int c = 0; c < GRID_COLS; c++) {
            int i = (sel_scroll + r) * GRID_COLS + c;
            if (i >= n) break;
            float x = GRID_X + c * CELL, y = GRID_Y + r * CELL;
            ui_z = 0.5f;
            button_box(x, y, CELL - 4, CELL - 4, i == sel_cursor);
            int rk = level_rank(sel_world, i);
            ui_z = 0.52f;
            cur_font = font_square;
            snprintf(b, sizeof(b), "%d-%d", sel_world, i + 1);
            text_ol(x + (CELL - 4) / 2, y + 7, 0.42f, rk == 5 ? PEAKED : grey(i == sel_cursor ? 0.95f : 0.875f),
                    C2D_AlignCenter, b);
            cur_font = font_ui;
            if (rk >= 0) {
                char rn[16];
                snprintf(rn, sizeof(rn), "rs_%d", rk);
                draw_hud(rn, x + (CELL - 4) / 2, y + 32, 0.4f, WHITE);
            }
        }
    // scrollbar (scrollbar.png track, scrollbar_handle.png), only when the world needs it
    int rows = sel_rows();
    if (rows > GRID_ROWS) {
        ui_z = 0.5f;
        nine("toggle", SBAR_X, GRID_Y, 10, GRID_ROWS * CELL - 4, 4, 4, grey(0.5f));
        float hh = (GRID_ROWS * CELL - 4) * GRID_ROWS / rows;
        float hy = GRID_Y + (GRID_ROWS * CELL - 4 - hh) * sel_scroll / (rows - GRID_ROWS);
        nine("toggle", SBAR_X, hy, 10, hh, 4, 4, WHITE);
    }
    // world buttons
    ui_z = 0.5f;
    button_box(WBTN_X, 44, 60, 40, false);
    button_box(WBTN_X, 90, 60, 40, false);
    ui_z = 0.53f;
    arrow(WBTN_X + 30, 64, 9, 1);
    arrow(WBTN_X + 30, 110, 9, -1);
    // GO!
    ui_z = 0.5f;
    draw_hud(pressed_now(GO_X - 32, GO_Y - 32, 64, 64) ? "go_pressed" : "go", GO_X, GO_Y, 1, WHITE);
    ui_z = 0.52f;
    text_ol(GO_X, GO_Y - 0.6f * lf_sys * 0.5f - 2, 0.6f, WHITE, C2D_AlignCenter, "GO!");
}

static void draw_select_top(void) {
    char b[160], id[16], key[48], v[32], ms[8];
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    text_ol(8, 76, 0.5f, WHITE, 0, tr("MENU_SelectedLevel"));
    snprintf(id, sizeof(id), "%d-%d", sel_world, sel_cursor + 1);
    snprintf(key, sizeof(key), "GAME_Level%s", id);
    snprintf(b, sizeof(b), "%s\n%s", id, tr(key));
    text_ol_wrap(8, 96, 0.5f, WHITE, 0, 200, b);
    snprintf(key, sizeof(key), "%s_score", id);
    int sc = cfg_get_int(savedata, "LEVELS", key, -1);
    snprintf(b, sizeof(b), "Best Score: %d", sc < 0 ? 0 : sc);
    text_ol(8, 146, 0.45f, WHITE, 0, b);
    if (sc >= 0) {
        snprintf(key, sizeof(key), "%s_time", id);
        format_time(v, sizeof(v), ms, sizeof(ms), cfg_get_float(savedata, "LEVELS", key, 0));
        snprintf(b, sizeof(b), "Best Time: %s%s", v, ms);
    } else {
        snprintf(b, sizeof(b), "Best Time: --:--.--");
    }
    text_ol(8, 164, 0.45f, WHITE, 0, b);
    ol_color = BLACK;
    int rk = level_rank(sel_world, sel_cursor);
    char rn[16];
    snprintf(rn, sizeof(rn), "rs_%d", rk < 0 ? 0 : rk);
    ui_z = 0.4f;
    draw_hud(rn, 44, 210, 0.85f, WHITE);
}

// ---------------------------------------------------------------- pause
// pause_menu.tscn: dark and red-tinted overlays, paused_new.png, default buttons
#define PAUSE_X 50.0f
#define PAUSE_Y 92.0f
#define PAUSE_W 220.0f
#define PAUSE_H 32.0f
#define PAUSE_STEP 36.0f

static void draw_pause(void) {
    C2D_DrawRectSolid(0, 0, 0.9f, BOT_W, SCR_H, C2D_Color32f(0, 0, 0, 0.39f));
    C2D_DrawRectSolid(0, 0, 0.9f, BOT_W, SCR_H, C2D_Color32f(0.23f, 0, 0, 0.39f));
    ui_z = 0.92f;
    draw_hud("paused", 160, 46, 1, WHITE);
    const char *items[4] = {tr("MENU_PauseResume"), "Restart Level", tr("MENU_Settings"), tr("MENU_PauseQLevel")};
    for (int i = 0; i < 4; i++) button(PAUSE_X, PAUSE_Y + i * PAUSE_STEP, PAUSE_W, PAUSE_H, items[i], i == pause_cursor);
}
// ---------------------------------------------------------------- settings menu
enum { SET_SENS, SET_INVERT, SET_BUFFER, SET_DRAG, SET_ZOOM, SET_LEFT, SET_THOUGHTS, SET_SCARES, SET_FPS,
       SET_MASTER, SET_MUSIC, SET_SOUND, SET_LANG, SET_BACK, SET_COUNT };
#define SET_ROWS 7

static const char *set_label(int i) {
    switch (i) {
    case SET_SENS: return tr("MENU_SettingsSensitivity");
    case SET_INVERT: return tr("MENU_SettingsInvertShot");
    case SET_BUFFER: return tr("MENU_SettingsBufferShot");
    case SET_DRAG: return "Drag starts:";
    case SET_ZOOM: return "Course view:";
    case SET_LEFT: return "Left-handed:";
    case SET_THOUGHTS: return "Thoughts:";
    case SET_SCARES: return "Jumpscares:";
    case SET_FPS: return "Show FPS:";
    case SET_MASTER: return tr("MENU_SettingsMasterVol");
    case SET_MUSIC: return tr("MENU_SettingsMusicVol");
    case SET_SOUND: return tr("MENU_SettingsSoundVol");
    case SET_LANG: return tr("MENU_SettingsLang");
    default: return tr("MENU_SettingsSave");
    }
}

static const char *set_desc(int i) {
    switch (i) {
    case SET_SENS: return tr("MENU_SettingsSensitivity_D");
    case SET_INVERT: return tr("MENU_SettingsInvertShot_D");
    case SET_BUFFER: return tr("MENU_SettingsBufferShot_D");
    case SET_DRAG: return "Anywhere: start a shot by touching anywhere on the course. On the cat: only a touch on Canny starts one.";
    case SET_ZOOM: return "Zoomed: the course fills the bottom screen and follows the cat. Fit: the whole course at once. L switches during play.";
    case SET_LEFT: return "Swaps the Pause, Restart and Skip buttons to the other side.";
    case SET_THOUGHTS: return "The Cat Thought Displayer 9000 on the top screen. Off saves a little battery.";
    case SET_SCARES: return "On: the full scare. Quiet: the picture without the scream. Off: neither.";
    case SET_FPS: return "Shows the frame rate in the corner.";
    case SET_MASTER: return tr("MENU_SettingsMasterVol_D");
    case SET_MUSIC: return tr("MENU_SettingsMusicVol_D");
    case SET_SOUND: return tr("MENU_SettingsSoundVol_D");
    case SET_LANG: return tr("MENU_SettingsLang_D");
    default: return tr("MENU_Settings_D");
    }
}

static void set_value(int i, char *b, size_t n) {
    const char *onoff[2] = {"Off", "On"};
    switch (i) {
    case SET_SENS: snprintf(b, n, "%d%%", set.sens10 * 10); break;
    case SET_INVERT: snprintf(b, n, "%s", onoff[set.invert]); break;
    case SET_BUFFER: snprintf(b, n, "%s", onoff[set.buffer]); break;
    case SET_DRAG: snprintf(b, n, "%s", set.drag_on_cat ? "On the cat" : "Anywhere"); break;
    case SET_ZOOM: snprintf(b, n, "%s", set.zoom ? "Zoomed" : "Fit"); break;
    case SET_LEFT: snprintf(b, n, "%s", onoff[set.left_handed]); break;
    case SET_THOUGHTS: snprintf(b, n, "%s", onoff[set.thoughts]); break;
    case SET_SCARES: snprintf(b, n, "%s", (const char *[]){"On", "Quiet", "Off"}[set.jumpscares]); break;
    case SET_FPS: snprintf(b, n, "%s", onoff[set.show_fps]); break;
    case SET_MASTER: snprintf(b, n, "%d%%", set.master); break;
    case SET_MUSIC: snprintf(b, n, "%d%%", set.music); break;
    case SET_SOUND: snprintf(b, n, "%d%%", set.sound); break;
    case SET_LANG: {
        char key[24];
        snprintf(key, sizeof(key), "MENU_Lang_%s", LANGS[set.lang]);
        snprintf(b, n, "%s", tr(key));
        break;
    }
    default: b[0] = 0;
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void set_change(int i, int d) {
    switch (i) {
    case SET_SENS: set.sens10 = clampi(set.sens10 + 2 * d, 2, 20); break;
    case SET_INVERT: set.invert = !set.invert; break;
    case SET_BUFFER: set.buffer = !set.buffer; break;
    case SET_DRAG: set.drag_on_cat = !set.drag_on_cat; break;
    case SET_ZOOM: set.zoom = !set.zoom; break;
    case SET_LEFT: set.left_handed = !set.left_handed; break;
    case SET_THOUGHTS: set.thoughts = !set.thoughts; thoughts_enable(set.thoughts); thoughts_play("Idle"); break;
    case SET_SCARES: set.jumpscares = (set.jumpscares + 3 + d) % 3; break;
    case SET_FPS: set.show_fps = !set.show_fps; break;
    case SET_MASTER: set.master = clampi(set.master + 5 * d, 0, 100); break;
    case SET_MUSIC: set.music = clampi(set.music + 5 * d, 0, 100); break;
    case SET_SOUND: set.sound = clampi(set.sound + 5 * d, 0, 100); break;
    case SET_LANG: set.lang = (set.lang + NLANGS + d) % NLANGS; lang_load(&lang_cur, LANGS[set.lang]); break;
    default: return;
    }
    audio_set_volumes(set.master / 100.0f, set.music / 100.0f, set.sound / 100.0f);
    sfx_play("player/grab.ogg", 1, 1);
}

static void open_settings(int back) {
    settings_back = back;
    set_cursor = 0;
    set_scroll = 0;
    set_pos = 0;
    set_touch = STOUCH_NONE;
    state = ST_SETTINGS;
}

static void close_settings_now(void) {
    save_settings();
    gset = game_settings();
    game.set = gset;
    state = settings_back;
}

static void close_settings(void) { go(TR_CLOSE_SETTINGS, 0, 0); }

// settings.tres: SuperBubble labels, toggle_normal.png boxes (modulate 1.39 when
// focused, so the others sit darker here), on.png/off.png checks, slider.png
// tracks with the norank grabber (peak while focused)
static bool set_is_toggle(int i) {
    return i == SET_INVERT || i == SET_BUFFER || i == SET_LEFT || i == SET_THOUGHTS || i == SET_FPS;
}

static bool set_slider(int i, float *frac) {
    switch (i) {
    case SET_SENS: *frac = (set.sens10 - 2) / 18.0f; return true;
    case SET_MASTER: *frac = set.master / 100.0f; return true;
    case SET_MUSIC: *frac = set.music / 100.0f; return true;
    case SET_SOUND: *frac = set.sound / 100.0f; return true;
    default: return false;
    }
}

#define SET_Y 30.0f
#define SET_STEP 29.0f
#define SET_H 26.0f
#define SET_CTRL_X 186.0f

#define SET_MAXPOS ((SET_COUNT - SET_ROWS) * SET_STEP)
#define SET_LIST_END (SET_Y + SET_ROWS * SET_STEP - 2) // rows are cut off past here

// the settings background between y0 and y1 (or black without the picture)
static void settings_bg_band(float y0, float y1) {
    if (settings_bg)
        draw_img_part(C2D_SpriteSheetGetImage(settings_bg, 0), 0, y0, BOT_W, y1 - y0, 0, y0, BOT_W, y1 - y0, WHITE);
    else
        C2D_DrawRectSolid(0, y0, ui_z, BOT_W, y1 - y0, BLACK);
}

static void draw_settings(void) {
    char v[48];
    ui_z = 0.9f;
    settings_bg_band(0, SCR_H);
    // rows scroll by the pixel; the title band and the bottom edge are drawn again over them
    int first = (int)floorf(set_pos / SET_STEP);
    for (int i = first < 0 ? 0 : first; i < SET_COUNT && i <= first + SET_ROWS; i++) {
        float y = SET_Y + i * SET_STEP - set_pos;
        if (y + SET_H < SET_Y - 4 || y > SET_LIST_END) continue;
        bool hl = i == set_cursor;
        ui_z = 0.92f;
        if (i == SET_BACK) {
            button(50, y, 220, SET_H, set_label(i), hl);
            continue;
        }
        // toggle_normal.png is 8x8 with 4 px margins (no middle), so stretch from 3
        nine("toggle", 4, y, 312, SET_H, 3, 4, grey(hl ? 0.9f : 0.42f));
        ui_z = 0.93f;
        cur_font = font_bubble;
        text_ol(10, y + SET_H / 2 - 0.42f * lf_sys * 0.5f, 0.42f, grey(hl ? 1.0f : 0.875f), 0, set_label(i));
        cur_font = font_ui;
        float frac;
        set_value(i, v, sizeof(v));
        if (set_is_toggle(i)) {
            bool on = !strcmp(v, "On");
            draw_hud(on ? "on" : "off", 298, y + SET_H / 2, 1.5f, WHITE);
            text_ol(284, y + SET_H / 2 - 0.4f * lf_sys * 0.5f, 0.4f, WHITE, C2D_AlignRight, v);
        } else if (set_slider(i, &frac)) {
            float tw = 88;
            nine("slider", SET_CTRL_X, y + 5, tw, 16, 8, 8, hl ? WHITE : grey(0.672f));
            ui_z = 0.94f;
            draw_hud(hl ? "peak" : "norank", SET_CTRL_X + 8 + (tw - 16) * frac, y + SET_H / 2, 1.25f, WHITE);
            text_ol(312, y + SET_H / 2 - 0.36f * lf_sys * 0.5f, 0.36f, WHITE, C2D_AlignRight, v);
        } else {
            float cx = (SET_CTRL_X + 312) / 2;
            text_ol(cx, y + SET_H / 2 - 0.4f * lf_sys * 0.5f, 0.4f, WHITE, C2D_AlignCenter, v);
            arrow_lr(SET_CTRL_X + 6, y + SET_H / 2, 5, -1);
            arrow_lr(306, y + SET_H / 2, 5, 1);
        }
    }
    ui_z = 0.955f;
    settings_bg_band(0, SET_Y - 2);
    settings_bg_band(SET_LIST_END, SCR_H);
    ui_z = 0.96f;
    cur_font = font_bubble;
    text_ol(160, 2, 0.62f, WHITE, C2D_AlignCenter, tr("MENU_Settings"));
    cur_font = font_ui;
    // more rows above/below
    ui_z = 0.97f;
    if (set_pos > 1) arrow(304, 14, 6, 1);
    if (set_pos < SET_MAXPOS - 1) arrow(304, 236, 5, -1);
}

static void draw_settings_top(void) {
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    cur_font = font_bubble;
    text_ol_wrap(8, 76, 0.5f, WHITE, 0, 210, set_label(set_cursor));
    cur_font = font_ui;
    text_ol_wrap(8, 104, 0.42f, grey(0.95f), 0, 205, set_desc(set_cursor));
}

// move a slider's grabber to the stylus x
static void slider_to(int i, float x) {
    float frac;
    if (!set_slider(i, &frac)) return;
    float f = (x - SET_CTRL_X - 8) / (88 - 16);
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    int steps = i == SET_SENS ? 9 : 20, now = (int)roundf(frac * steps), want = (int)roundf(f * steps);
    if (want != now) set_change(i, want - now);
}

// the row under a point, or -1
static int set_row_at(vec2 t) {
    if (t.y < SET_Y - 2 || t.y >= SET_LIST_END) return -1;
    for (int i = 0; i < SET_COUNT; i++)
        if (hit_box(t, 4, SET_Y + i * SET_STEP - set_pos, 312, SET_H)) return i;
    return -1;
}

// keep the cursor on a row that is mostly on screen
static void set_cursor_in_view(void) {
    int top = (int)ceilf(set_pos / SET_STEP - 0.4f), bottom = top + SET_ROWS - 1;
    if (set_cursor < top) set_cursor = top;
    if (set_cursor > bottom) set_cursor = bottom;
    if (set_cursor >= SET_COUNT) set_cursor = SET_COUNT - 1;
}

// a stylus tap that did not turn into a drag, acted on when the stylus lifts
static bool settings_tap(vec2 t) {
    if (t.y < SET_Y) {
        if (set_scroll > 0) set_scroll--;
        if (set_cursor >= set_scroll + SET_ROWS) set_cursor = set_scroll + SET_ROWS - 1;
        return false;
    }
    if (t.y >= SET_LIST_END) {
        if (set_scroll + SET_ROWS < SET_COUNT) set_scroll++;
        if (set_cursor < set_scroll) set_cursor = set_scroll;
        return false;
    }
    int i = set_row_at(t);
    if (i < 0) return false;
    set_cursor = i;
    if (i == SET_BACK) {
        tap_box(t, 50, SET_Y + i * SET_STEP - set_pos, 220, SET_H);
        close_settings();
        return true;
    }
    float frac;
    if (set_is_toggle(i)) set_change(i, 1);
    else if (set_slider(i, &frac)) { if (t.x >= SET_CTRL_X) slider_to(i, t.x); }
    else if (t.x >= SET_CTRL_X) set_change(i, t.x < (SET_CTRL_X + 312) / 2 ? -1 : 1);
    return false;
}

static void settings_input(u32 kd, bool tdown, bool theld, bool tup, vec2 touch) {
    bool moved = false;
    if (kd & KEY_DOWN) { set_cursor = (set_cursor + 1) % SET_COUNT; moved = true; }
    if (kd & KEY_UP) { set_cursor = (set_cursor + SET_COUNT - 1) % SET_COUNT; moved = true; }
    if (kd & KEY_LEFT) set_change(set_cursor, -1);
    if (kd & KEY_RIGHT) set_change(set_cursor, 1);
    if (kd & KEY_A) {
        if (set_cursor == SET_BACK) { close_settings(); return; }
        set_change(set_cursor, 1);
    }
    if (kd & (KEY_B | KEY_START)) { close_settings(); return; }
    if (moved) { // the list follows the D-pad cursor
        if (set_cursor < set_scroll) set_scroll = set_cursor;
        if (set_cursor >= set_scroll + SET_ROWS) set_scroll = set_cursor - SET_ROWS + 1;
    }

    if (tdown) {
        set_touch_at = touch;
        set_touch_pos = set_pos;
        set_last_y = touch.y;
        set_vel = 0;
        int i = set_row_at(touch);
        float frac;
        if (i >= 0 && touch.x >= SET_CTRL_X && set_slider(i, &frac)) {
            // a slider follows the stylus while it is held
            set_touch = STOUCH_SLIDER;
            set_touch_row = i;
            set_cursor = i;
            slider_to(i, touch.x);
        } else {
            set_touch = STOUCH_PENDING;
        }
    } else if (theld && set_touch == STOUCH_SLIDER) {
        slider_to(set_touch_row, touch.x);
    } else if (theld && set_touch != STOUCH_NONE) {
        float dy = touch.y - set_touch_at.y;
        if (set_touch == STOUCH_PENDING && fabsf(dy) > 6) set_touch = STOUCH_SCROLL;
        if (set_touch == STOUCH_SCROLL) { // the list follows the stylus, stretching a little past its ends
            float p = set_touch_pos - dy;
            if (p < 0) p /= 3;
            if (p > SET_MAXPOS) p = SET_MAXPOS + (p - SET_MAXPOS) / 3;
            set_pos = p;
            set_vel = set_vel * 0.5f + (set_last_y - touch.y) * 0.5f;
            set_last_y = touch.y;
            set_cursor_in_view();
        }
    }
    if (tup) {
        int was = set_touch;
        set_touch = STOUCH_NONE;
        if (was == STOUCH_SCROLL) { // let go: carry on a little with the stylus's speed, then settle on a row
            int sc = (int)roundf((set_pos + set_vel * 6) / SET_STEP);
            int maxs = SET_COUNT - SET_ROWS;
            set_scroll = sc < 0 ? 0 : sc > maxs ? maxs : sc;
        }
        if (was == STOUCH_PENDING && settings_tap(set_touch_at)) return;
    }
    if (set_touch != STOUCH_SCROLL) { // glide to the row the list rests on
        float target = set_scroll * SET_STEP;
        set_pos += (target - set_pos) * 0.25f;
        if (fabsf(target - set_pos) < 0.3f) set_pos = target;
        if (set_touch == STOUCH_NONE && fabsf(target - set_pos) > 0.3f) set_cursor_in_view();
    }
}

// ---------------------------------------------------------------- intro credits
// A golf joke: the original team on a pencilled scorecard (top), and Canny whacked into the cup of a
// putting green whose flag is NickBin's logo (bottom). Art from tools/convert/credits.py.
static int intro_t; // frames since boot
#define INTRO_HIT 48   // Canny gets whacked
#define INTRO_LAND 108 // and drops in the cup
#define INTRO_END (60 * 7)
#define CUP_X 242.0f
#define CUP_Y 172.0f

static float ease_out(float p) {
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    return 1 - (1 - p) * (1 - p) * (1 - p);
}

static void draw_intro_top(void) {
    draw_top_bg();
    // the card drops in, lands with a small wobble and stays a little crooked
    float t = intro_t / 60.0f, p = ease_out(t / 0.6f);
    float y = 120 - (1 - p) * 260;
    y += t > 0.6f ? sinf((t - 0.6f) * 14) * 5 * expf(-(t - 0.6f) * 4) : 0;
    ui_z = 0.5f;
    draw_spr(spr_find("cr_card"), v2(200, y), 1, 1, WHITE, 0);
    // pencil on the card, in the system font: the card's art holds nothing from the game, so it is
    // part of the 3dsx/cia even when the game's files live on the SD card (credits.py lines it up)
    static const char *const HOLES[5][4] = {
        // from the game's credits room (MENU_Credits* in UCG_T.csv)
        {"1", "SH2K (Robert)", "made the game", "1"},
        {"2", "fogwaves", "programming + juice", "2"},
        {"3", "BlakeTheGuy", "made the friction good", "3"},
        {"4", "mbx8_rt", "level design", "4"},
        {"5", "bibizu", "*cough* pro gamming", "5"},
    };
    float ox = 8, oy = y - 113;
    u32 pencil = C2D_Color32(58, 58, 74, 255), red = C2D_Color32(214, 92, 92, 255);
    C2D_Font keep = cur_font;
    cur_font = NULL; // the system font
    ui_z = 0.52f;
    text(ox + 42, oy + 6, 0.62f, pencil, 0, "UNCANNY CAT GOLF - the scorecard");
    static const float CX[4] = {6, 42, 168, 346};
    static const char *const HEAD[4] = {"HOLE", "WHO", "DID WHAT", "PAR"};
    for (int c = 0; c < 4; c++) text(ox + CX[c], oy + 33, 0.4f, red, 0, HEAD[c]);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++)
            text(ox + CX[c] + (c == 0 || c == 3 ? 7 : 0), oy + 54 + r * 22, 0.5f, pencil, 0, HOLES[r][c]);
    // the last three each get their own band between the card's ruled lines (162, 184, 206)
    float mid = ox + 192 + 17; // the middle of the card right of its red margin
    text(mid, oy + 166, 0.4f, red, C2D_AlignCenter, "playtested by");
    text(mid, oy + 188, 0.42f, pencil, C2D_AlignCenter, "Penny, Amber, Bobby, hyphen-lost, Mira, Felix, Rheanna");
    text(mid, oy + 209, 0.38f, C2D_Color32(110, 110, 130, 255), C2D_AlignCenter,
         "the original game: slappyhappy2000.itch.io");
    cur_font = keep;
}

static void draw_intro(void) {
    float t = intro_t / 60.0f;
    ui_z = 0.1f;
    draw_hud("cr_green", 0, 0, 1, WHITE);
    // the flag: a pole in the cup and the logo as its banner, rippling away from the pole
    ui_z = 0.3f;
    C2D_DrawRectSolid(CUP_X - 1, 60, ui_z, 3, CUP_Y - 60, C2D_Color32(236, 236, 228, 255));
    int li = spr_find("cr_logo");
    if (li >= 0 && *sprinfo[li].sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(*sprinfo[li].sheet, sprinfo[li].idx);
        const int N = 16;
        for (int i = 0; i < N; i++) {
            float k = (float)i / (N - 1), dy = sinf(t * 5 - i * 0.45f) * 3.5f * k;
            draw_img_part(img, i * 128.0f / N, 0, 128.0f / N, 128, CUP_X + 2 + i * 4.0f, 62 + dy, 4, 64, WHITE);
        }
    }
    // Canny: waits on the tee, flies when whacked, then sits in the cup
    ui_z = 0.4f;
    float k = 1.1f;
    if (intro_t < INTRO_HIT) {
        draw_sprite("costume_canny", v2(42, 196), k, 1, WHITE, 0);
    } else if (intro_t < INTRO_LAND) {
        float p = (intro_t - INTRO_HIT) / (float)(INTRO_LAND - INTRO_HIT);
        float x = 42 + (CUP_X - 42) * p, y = 196 + (CUP_Y - 6 - 196) * p - sinf(p * 3.14159f) * 130;
        draw_sprite("costume_canny", v2(x, y), k, 1, WHITE, p * 4 * 3.14159f);
    } else {
        // only the top half shows above the cup's rim
        draw_sprite("costume_canny", v2(CUP_X, CUP_Y - 10), k * 0.8f, 1, WHITE, 0);
        ui_z = 0.42f; // the green in front of the cup hides Canny's bottom half
        int gi = spr_find("cr_green");
        if (gi >= 0 && *sprinfo[gi].sheet)
            draw_img_part(C2D_SpriteSheetGetImage(*sprinfo[gi].sheet, sprinfo[gi].idx), 0, CUP_Y, 320, 240 - CUP_Y, 0,
                          CUP_Y, 320, 240 - CUP_Y, WHITE);
    }
    ui_z = 0.6f;
    cur_font = font_ui;
    text_ol(18, 74, 0.55f, grey(0.95f), 0, "Ported to 3DS by");
    text_ol(18, 96, 1.05f, WHITE, 0, "NickBin");
    if (intro_t >= INTRO_LAND) {
        float s = 0.55f + 0.25f * ease_out((intro_t - INTRO_LAND) / 18.0f);
        text_rainbow(160 - str_width(s, "HOLE IN ONE!") / 2, 12, s, 1, "HOLE IN ONE!");
    }
    if (intro_t > 150 && (intro_t / 30) % 2 == 0 && tr_phase == 0)
        text_ol(160, 214, 0.45f, grey(0.95f), C2D_AlignCenter, "Tap to tee off");
}

// ---------------------------------------------------------------- main
// every gameplay sound (about 6 MB decoded). The worker thread decodes them all from boot, so none stalls
// a level the first time it plays and the boot screen does not wait for them.
static const char *const PRELOAD[] = {
    "player/grab.ogg", "player/hit.ogg", "player/wallBump.ogg", "player/push.ogg", "player/cancel.ogg",
    "player/notif.ogg", "player/water_death.ogg", "player/water_splash1.ogg", "player/water_splash2.ogg",
    "player/water_splash3.ogg", "collectibles/coin.ogg", "collectibles/key.ogg", "block/dblock.ogg",
    "block/keydoor.ogg", "enemy/skullDeath.ogg", "collectibles/boost.ogg", "gimmicks/jump_pad.ogg",
    "enemy/carDeath.ogg", "enemy/carHorn1.ogg", "enemy/carHorn2.ogg", "gimmicks/policesiren.ogg",
    "gimmicks/spotlight.ogg", "ghost/defaultJumpscare.ogg", "game/deathOB.ogg",
    "game/levelStart.ogg", "game/levelWin.ogg", "game/resultsDrumroll.ogg", "game/rankAtrocious.ogg",
    "game/rankErm.ogg", "game/rankOK.ogg", "game/rankPeak.ogg", "game/rankSwag.ogg",
    "gimmicks/screamsOfDeadPeople.ogg", "minigame/meowl_die.ogg"};

// ---------------------------------------------------------------- loading behind the intro
// The intro needs only its own art, the sprites (Canny), the top background and the font. Everything
// else is read by the worker while the credits play, and turned into textures here a piece per frame.
static const char *const BEHIND[] = {"gfx/hud.t3x", "gfx/menu.t3x", "gfx/settings_bg.t3x", "gfx/jumpscare.t3x"};
#define NBEHIND (int)(sizeof(BEHIND) / sizeof(BEHIND[0]))
static int behind_done;

static void boot_piece(int i) {
    switch (i) {
    case 0: load_sheet(&hudsheet, dp("gfx/hud.t3x"), dp("gfx/hud.txt")); break;
    case 1: load_sheet(&menusheet, dp("gfx/menu.t3x"), dp("gfx/menu.txt")); break;
    case 2: settings_bg = sheet_load(dp("gfx/settings_bg.t3x")); break;
    case 3: jumpscare_sheet = sheet_load(dp("gfx/jumpscare.t3x")); break;
    }
}

static void boot_step(void) { // one finished piece per frame
    if (behind_done < NBEHIND && prefetch_ready(dp(BEHIND[behind_done]))) boot_piece(behind_done++);
}

static void boot_finish(void) { // whatever is left, before the game shows (waits for the worker)
    while (behind_done < NBEHIND) boot_piece(behind_done++);
}

// ---------------------------------------------------------------- the Oops screen
// When the game's files are in neither sdmc:/3ds/Uncanny Cat Golf/ nor the 3dsx: an upset cat, the
// message and a sad trombone, all from romfs:/port/ (tools/convert/credits.py). START quits.
static void oops_screen(bool wrong_version) {
    C2D_SpriteSheet sheet = NULL;
    load_sheet(&sheet, "romfs:/port/oops.t3x", "romfs:/port/oops.txt");
    audio_init();
    sfx_play("romfs:/port/oops.wav", 1, 1);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(top);
        ui_z = 0.5f;
        draw_hud("oops_top", 0, 0, 1, WHITE);
        C2D_SceneBegin(bot);
        draw_hud(wrong_version ? "oops_version" : "oops_bottom", 0, 0, 1, WHITE);
        C3D_FrameEnd(0);
    }
    audio_exit();
    if (sheet) C2D_SpriteSheetFree(sheet);
}

int main(void) {
    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
    C2D_Prepare();
    C2D_SetTintMode(C2D_TintMult);
    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    tbuf = C2D_TextBufNew(16384);
    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);
    srand((unsigned)osGetTime());

    save_init();
    fontEnsureMapped();
    lf_sys = C2D_FontGetInfo(NULL)->lineFeed;
    int found = data_find();
    if (found != DATA_OK) {
        oops_screen(found == DATA_WRONG_VERSION);
        save_exit();
        C2D_TextBufDelete(tbuf);
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        romfsExit();
        return 0;
    }
    // what the intro needs now; the rest is read behind it (boot_step)
    load_sheet(&creditsheet, "romfs:/port/credits.t3x", "romfs:/port/credits.txt");
    load_sheet(&sprites, dp("gfx/sprites.t3x"), dp("gfx/sprites.txt"));
    top_bg = C2D_SpriteSheetLoad(dp("gfx/top_bg.t3x"));
    // the three fonts are one file when the game uses one font for all (it does: Comic Sans)
    font_ui = C2D_FontLoad(dp("gfx/ui.bcfnt"));
    font_bubble = C2D_FontLoad(dp("gfx/bubble.bcfnt"));
    font_square = C2D_FontLoad(dp("gfx/square.bcfnt"));
    if (!font_bubble) font_bubble = font_ui;
    if (!font_square) font_square = font_ui;
    cur_font = font_ui;
    lang_load(&lang_en, "en");
    load_level_index();
    audio_init();
    thoughts_init();
    load_settings();
    savedata = cfg_open("save_data.ini");
    apply_settings();
    worker_start();
    for (int i = 0; i < NBEHIND; i++) prefetch(dp(BEHIND[i]));
    // decoded by the worker, the menu's own sounds first, so no sound is decoded mid-game
    sfx_preload("player/hit.ogg"); // the intro's two sounds first
    sfx_preload("game/rankPeak.ogg");
    for (int i = 0; i < (int)(sizeof(PRELOAD) / sizeof(PRELOAD[0])); i++) sfx_preload(PRELOAD[i]);
    music_play(1);
    thoughts_fallback("Idle");
    thoughts_play("Idle");
    fps_since = osGetTime();
    tr_in = 1.0f / 60 / 0.5f; // the credits fade in over half a second

#if UCG_PROFILE
    int frame_no = 0;
#endif
    while (aptMainLoop()) {
        PROF_FRAME_BEGIN();
        hidScanInput();
        u32 kd = hidKeysDown(), kh = hidKeysHeld(), ku = hidKeysUp();
        touchPosition tp;
        hidTouchRead(&tp);
        vec2 touch = v2(tp.px, tp.py);
        transition_step();
        // no input while the screen goes dark; while it comes back the new screen already listens
        if (tr_phase == 1) kd = kh = ku = 0;
        bool tdown = kd & KEY_TOUCH, theld = kh & KEY_TOUCH, tup = ku & KEY_TOUCH;
        if (theld) last_touch = touch;

        if (state == ST_INTRO) {
            intro_t++;
            if (intro_t == INTRO_HIT) sfx_play("player/hit.ogg", 1, 1);
            if (intro_t == INTRO_LAND) sfx_play("game/rankPeak.ogg", 1, 1);
            if (intro_t > INTRO_LAND + 30) boot_step(); // once Canny is in the cup and the screen is calm
            if ((intro_t > 20 && (kd & (KEY_A | KEY_B | KEY_START | KEY_TOUCH))) || intro_t > INTRO_END) {
                go(TR_INTRO_DONE, 0, 0);
                tr_out = tr_in = 1.0f / 60 / 0.5f; // half a second out of the credits and into the game
            }
        } else if (state == ST_SELECT) {
            int n = wcount[sel_world];
            int dw = (kd & KEY_R) ? 1 : (kd & KEY_L) ? -1 : 0;
            if (kd & KEY_RIGHT) sel_cursor = (sel_cursor + 1) % n;
            if (kd & KEY_LEFT) sel_cursor = (sel_cursor + n - 1) % n;
            if (kd & KEY_DOWN) sel_cursor = sel_cursor + GRID_COLS < n ? sel_cursor + GRID_COLS : sel_cursor % GRID_COLS;
            if (kd & KEY_UP) {
                if (sel_cursor >= GRID_COLS) sel_cursor -= GRID_COLS;
                else {
                    int last = sel_cursor + (sel_rows() - 1) * GRID_COLS;
                    sel_cursor = last < n ? last : last - GRID_COLS;
                }
            }
            bool play = kd & KEY_A;
            if (kd & KEY_SELECT) go(TR_SETTINGS, ST_SELECT, 0);
            else if (tdown) {
                if (tap_box(touch, 232, 4, 84, 28)) go(TR_SETTINGS, ST_SELECT, 0);
                else if (tap_box(touch, WBTN_X, 44, 60, 40)) dw = -1;
                else if (tap_box(touch, WBTN_X, 90, 60, 40)) dw = 1;
                else if (v2len(v2sub(touch, v2(GO_X, GO_Y))) < 32) {
                    tap_box(touch, GO_X - 32, GO_Y - 32, 64, 64);
                    play = true;
                } else if (sel_rows() > GRID_ROWS && hit_box(touch, SBAR_X - 4, GRID_Y, 18, GRID_ROWS * CELL)) {
                    // the scrollbar: a row up or down, the cursor follows
                    float hh = (GRID_ROWS * CELL - 4) * GRID_ROWS / sel_rows();
                    float hy = GRID_Y + (GRID_ROWS * CELL - 4 - hh) * sel_scroll / (sel_rows() - GRID_ROWS);
                    if (touch.y < hy && sel_scroll > 0) sel_scroll--;
                    else if (touch.y > hy + hh && sel_scroll + GRID_ROWS < sel_rows()) sel_scroll++;
                    int row = sel_cursor / GRID_COLS;
                    if (row < sel_scroll) sel_cursor += (sel_scroll - row) * GRID_COLS;
                    if (row >= sel_scroll + GRID_ROWS) sel_cursor -= (row - sel_scroll - GRID_ROWS + 1) * GRID_COLS;
                } else
                    for (int r = 0; r < GRID_ROWS; r++)
                        for (int c = 0; c < GRID_COLS; c++) {
                            int i = (sel_scroll + r) * GRID_COLS + c;
                            float x = GRID_X + c * CELL, y = GRID_Y + r * CELL;
                            if (i < n && tap_box(touch, x, y, CELL - 4, CELL - 4)) {
                                // the first tap selects (its stats show on top), a second one plays
                                if (i == sel_cursor) play = true;
                                sel_cursor = i;
                                sfx_play("player/grab.ogg", 1, 1);
                            }
                        }
            }
            if (dw) {
                int w = sel_world;
                do w = (w + NWORLDS + dw) % NWORLDS;
                while (!wcount[w]);
                sel_world = w;
                sel_cursor = sel_scroll = 0;
                play = false;
            }
            if (play && state == ST_SELECT) go(TR_START, sel_world, sel_cursor);
            if (state == ST_SELECT) {
                int row = sel_cursor / GRID_COLS;
                if (row < sel_scroll) sel_scroll = row;
                if (row >= sel_scroll + GRID_ROWS) sel_scroll = row - GRID_ROWS + 1;
            }
            if (kd & KEY_START) break;
        } else if (state == ST_SETTINGS) {
            settings_input(kd, tdown, theld, tup, touch);
        } else if (state == ST_PAUSE) {
            int pick = -1;
            if (kd & KEY_DOWN) pause_cursor = (pause_cursor + 1) % 4;
            if (kd & KEY_UP) pause_cursor = (pause_cursor + 3) % 4;
            if (kd & KEY_A) pick = pause_cursor;
            if (kd & (KEY_START | KEY_B)) pick = 0;
            if (tdown)
                for (int i = 0; i < 4; i++)
                    if (tap_box(touch, PAUSE_X, PAUSE_Y + i * PAUSE_STEP, PAUSE_W, PAUSE_H)) pick = i;
            if (pick == 0) state = ST_PLAY;
            else if (pick == 1) go(TR_START, cur_world, cur_level);
            else if (pick == 2) go(TR_SETTINGS, ST_PAUSE, 0);
            else if (pick == 3) go(TR_SELECT, 0, 0);
        } else if (state == ST_PLAY) {
            GameInput in;
            memset(&in, 0, sizeof(in));
            bool on_course = set.zoom || (touch.x >= COURSE_X && touch.x < COURSE_X + COURSE_W);
            if (on_course && set.drag_on_cat && tdown) {
                vec2 c = world_to_screen(game.pos);
                on_course = v2len(v2sub(c, touch)) < fmaxf(22.0f, 24 * course_scale());
            }
            bool pause_btn = tdown && hit_box(touch, strip_x(0, 40), 0, 40, 48);
            if (kd & KEY_L) { // switch between the zoomed and the whole-course view
                set.zoom = !set.zoom;
                save_settings();
                camera_follow(game.pos, true);
            }
            bool restart_btn = tdown && hit_box(touch, strip_x(280, 40), 0, 40, 48);
            bool skip_btn = theld && hit_box(touch, strip_x(280, 40), 192, 40, 48);
            if (pause_btn) press_mark(strip_x(2, 36), 4, 36, 40);
            if (restart_btn) press_mark(strip_x(282, 36), 4, 36, 40);
            // in the zoomed view the buttons sit over the course, so a touch on one is not a shot
            bool on_button = pause_btn || restart_btn ||
                             (!(level.flags & LF_UNSKIPPABLE) && hit_box(touch, strip_x(280, 40), 192, 40, 48));
            if (tdown && on_course && !(set.zoom && on_button)) dragging = true;
            in.mouse = touch_to_viewport(last_touch);
            if (dragging) {
                in.click_pressed = tdown;
                in.click_down = theld;
                in.click_released = tup;
                if (tup) dragging = false;
            }
            if (kd & KEY_A) in.click_pressed = true, in.click_down = true;
            if (kh & KEY_A) in.click_down = true;
            if (ku & KEY_A) in.click_released = true;
            in.restart_pressed = (kd & KEY_Y) || restart_btn;
            in.skip_down = (kh & KEY_X) || skip_btn;
            in.pause_pressed = (kd & KEY_START) || pause_btn;

            if (game.victory) {
                // results: tap to skip the count-up, then tap for the next level
                results_t++;
                if (results_stage == 0 && results_t >= 30) {
                    results_stage = 1;
                    sfx_play("game/resultsDrumroll.ogg", 1, 1);
                }
                if (results_stage == 1 && results_t >= 30 + 144) results_stage = 2;
                if ((in.click_pressed || (kd & KEY_A)) && win_t > 40) {
                    if (results_stage < 2) results_stage = 2, results_t = 30 + 144;
                    else go(TR_NEXT, 0, 0);
                }
                if (in.restart_pressed) go(TR_START, cur_world, cur_level);
                if (state == ST_PLAY && game.victory) {
                    game_tick(&game, &(GameInput){0});
                    camera_follow(game.pos, false);
                    win_t++;
                    if (results_stage == 2 && results_rank_shown != game.rank) {
                        results_rank_shown = game.rank;
                        static const char *RS[6] = {"gimmicks/screamsOfDeadPeople.ogg", "game/rankAtrocious.ogg",
                                                    "game/rankErm.ogg", "game/rankOK.ogg", "game/rankSwag.ogg",
                                                    "game/rankPeak.ogg"};
                        sfx_play(RS[game.rank], 1, 1);
                    }
                }
            } else {
                game_tick(&game, &in);
                camera_follow(game.pos, false);
                play_events(game.events);
                play_thought(game.thought);
                if (in.skip_down && !(level.flags & LF_UNSKIPPABLE)) thoughts_play("Idle");
                if (game.events & EV_WIN) {
                    char id[16];
                    snprintf(id, sizeof(id), "%s", level.id);
                    record_result(id, game.final_score, game.time, game.rank, game.strokes);
                }
                if (game.progress == PROG_RESET || game.progress == PROG_WIN_RESET) go(TR_START, cur_world, cur_level);
                else if (game.progress == PROG_SKIP) go(TR_NEXT, 0, 0);
            }
            // pause works the whole time a course is up: before the cat drops in, after a death and on
            // the results too (the original's level.gd only allows it while the level is active)
            if (in.pause_pressed && state == ST_PLAY && tr_phase != 1) {
                state = ST_PAUSE;
                pause_cursor = 0;
                sfx_play("player/grab.ogg", 1, 1);
            }
            if (jumpscare_t > 0) jumpscare_t--;
        }

        PROF_MARK("logic");
        music_update();
        PROF_MARK("music");
        thoughts_update(1.0f / 60);
        PROF_MARK("thoughts");
        rainbow_t += 1.0f / 60;
        if (press_t > 0) press_t--;

        // ---- draw
        C2D_TextBufClear(tbuf);
        PROF_MARK("misc");
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        PROF_MARK("~wait gpu");
        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));

        C2D_SceneBegin(top);
        ui_z = 0.5f;
        if (state == ST_INTRO) draw_intro_top();
        else if (state == ST_SELECT) draw_select_top();
        else if (state == ST_SETTINGS) draw_settings_top();
        else if (game.victory && win_t > 20) draw_results_top();
        else draw_top_hud();
        ui_z = 0.99f;
        if (jumpscare_t > 0) draw_jumpscare(true);
        draw_fade(TOP_W);

        C2D_SceneBegin(bot);
        ui_z = 0.5f;
        if (state == ST_INTRO) draw_intro();
        else if (state == ST_SELECT) draw_select();
        else if (state == ST_SETTINGS && settings_back == ST_SELECT) draw_settings();
        else {
            draw_course();
            ui_z = 0.97f;
            if (state == ST_PAUSE) draw_pause();
            if (state == ST_SETTINGS) draw_settings();
            if (game.lose && jumpscare_t == 0 && state == ST_PLAY) {
                static const char *WHY[6] = {"Caught by the uncanny!", "Out of bounds!", "...where are you?",
                                             "Stuck in the wall!", "Horse!", "Splash!"};
                // the pause menu's dark red wash behind outlined text
                C2D_DrawRectSolid(course_left(), 88, 0.95f, set.zoom ? BOT_W : COURSE_W, 64, C2D_Color32f(0.23f, 0, 0, 0.6f));
                ui_z = 0.96f;
                text_ol(160, 94, 0.62f, C2D_Color32(255, 120, 120, 255), C2D_AlignCenter, WHY[game.death_type]);
                text_ol(160, 124, 0.45f, grey(0.95f), C2D_AlignCenter, "Tap to try again");
            }
            if (game.victory && results_stage >= 2 && state == ST_PLAY) {
                ui_z = 0.96f;
                char rn[16];
                snprintf(rn, sizeof(rn), "rsb_%d", game.rank); // 128x100 badge, about 186x145 here
                if (spr_find(rn) >= 0) draw_hud(rn, 160, 102, 1.45f, WHITE);
                else text_ol(160, 84, 1.0f, C2D_Color32f(1, 0.98f, 0.4f, 1), C2D_AlignCenter, RANK_NAMES[game.rank]);
                text_ol(160, 196, 0.45f, grey(0.95f), C2D_AlignCenter, "Tap for the next level");
            }
            ui_z = 0.99f;
            if (jumpscare_t > 0) draw_jumpscare(false);
        }
        if (set.show_fps) {
            fps_count++;
            u64 now = osGetTime();
            if (now - fps_since >= 1000) {
                fps_shown = fps_count;
                fps_count = 0;
                fps_since = now;
            }
            char f[16];
            snprintf(f, sizeof(f), "%d", fps_shown);
            ui_z = 1.0f;
            text_ol(strip_x(4, 30), 222, 0.45f, C2D_Color32(120, 255, 120, 255), 0, f);
        }
        draw_fade(BOT_W);
        PROF_MARK("draw");
        C3D_FrameEnd(0);
        PROF_MARK("~frame end");
#if UCG_PROFILE
        {
            static const char *SN[] = {"select", "play", "pause", "settings", "intro"};
            char what[48];
            snprintf(what, sizeof(what), "%s %s%s", SN[state], state == ST_SELECT ? "" : level.id,
                     game.victory ? " won" : game.lose ? " lost" : "");
            prof_frame_end(++frame_no, what);
        }
#endif
    }

    unload_level();
    worker_stop();
    thoughts_exit();
    audio_exit();
    save_exit();
    C2D_TextBufDelete(tbuf);
    if (sprites) C2D_SpriteSheetFree(sprites);
    if (hudsheet) C2D_SpriteSheetFree(hudsheet);
    if (top_bg) C2D_SpriteSheetFree(top_bg);
    lang_free(&lang_en);
    lang_free(&lang_cur);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    return 0;
}
