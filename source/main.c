// Uncanny Cat Golf 3DS: screens, drawing and input on the console.
// Bottom screen: the course (and every menu). Top screen: the sidebar.
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "game.h"

#define COURSE_X 40.0f
#define COURSE_W 240.0f
#define BOT_W 320
#define TOP_W 400
#define SCR_H 240

// ---------------------------------------------------------------- data files
typedef struct { char name[32]; int idx; float ax, ay, ux, uy; } SpriteInfo;
static SpriteInfo sprinfo[64];
static int nspr;
static C2D_SpriteSheet sprites;

static int spr_find(const char *name) {
    for (int i = 0; i < nspr; i++)
        if (!strcmp(sprinfo[i].name, name)) return i;
    return -1;
}

static void load_sprites(void) {
    sprites = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
    FILE *f = fopen("romfs:/gfx/sprites.txt", "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line), f) && nspr < 64) {
        SpriteInfo *s = &sprinfo[nspr];
        int w, h;
        if (sscanf(line, "%31s %d %d %d %f %f %f %f", s->name, &s->idx, &w, &h, &s->ax, &s->ay, &s->ux, &s->uy) == 8)
            nspr++;
    }
    fclose(f);
}

// translations: romfs:/lang/<lang>.txt, "KEY<tab>text" per line
static char *lang_blob;
static char **lang_keys, **lang_vals;
static int lang_n;

static void load_lang(const char *code) {
    char path[64];
    snprintf(path, sizeof(path), "romfs:/lang/%s.txt", code);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    free(lang_blob);
    lang_blob = malloc(len + 1);
    fread(lang_blob, 1, len, f);
    lang_blob[len] = 0;
    fclose(f);
    int lines = 1;
    for (long i = 0; i < len; i++)
        if (lang_blob[i] == '\n') lines++;
    free(lang_keys);
    free(lang_vals);
    lang_keys = malloc(lines * sizeof(char *));
    lang_vals = malloc(lines * sizeof(char *));
    lang_n = 0;
    char *p = lang_blob;
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
            lang_keys[lang_n] = p;
            lang_vals[lang_n] = v;
            lang_n++;
        }
        if (!nl) break;
        p = nl + 1;
    }
}

static const char *tr(const char *key) {
    for (int i = 0; i < lang_n; i++)
        if (!strcmp(lang_keys[i], key)) return lang_vals[i];
    return key;
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
static char wnames[8][32][16];
static int wcount[8];

static void load_level_index(void) {
    FILE *f = fopen("romfs:/levels/index.txt", "r");
    if (!f) return;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        int w, n, off;
        if (sscanf(line, "world %d %d %n", &w, &n, &off) < 2 || w < 0 || w > 7) continue;
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

// ---------------------------------------------------------------- app state
enum { ST_SELECT, ST_PLAY, ST_PAUSE };

static C3D_RenderTarget *top, *bot;
static C2D_TextBuf tbuf;
static int state = ST_SELECT;
static int cur_world, cur_level, sel_cursor;
static Game game;
static LevelDef level;
static bool level_loaded;
static C2D_SpriteSheet floor_u, floor_o;
static GameSettings settings = {1.0f, false, true};
static int jumpscare_t, results_t, win_t;
static int results_stage;
static vec2 last_touch;
static bool dragging;
static int pause_cursor;

static float course_scale(void) { return COURSE_W / (2.0f * level.bounds); }

static vec2 world_to_screen(vec2 w) {
    float k = course_scale();
    float x = (w.x + level.bounds) * k;
    if (level.flags & LF_MIRROR) x = COURSE_W - x;
    return v2(COURSE_X + x, (w.y + level.bounds) * k);
}

// touch (bottom screen px) -> the original's viewport px (course = 0..512)
static vec2 touch_to_viewport(vec2 t) {
    return v2((t.x - COURSE_X) * 512.0f / COURSE_W, t.y * 512.0f / COURSE_W);
}

static void unload_level(void) {
    if (floor_u) C2D_SpriteSheetFree(floor_u);
    if (floor_o) C2D_SpriteSheetFree(floor_o);
    floor_u = floor_o = NULL;
    if (level_loaded) level_free(&level);
    level_loaded = false;
}

static bool start_level(int w, int l) {
    unload_level();
    char path[128];
    snprintf(path, sizeof(path), "romfs:/levels/w%d/%s.txt", w, wnames[w][l]);
    if (!level_load(&level, path)) return false;
    level_loaded = true;
    snprintf(path, sizeof(path), "romfs:/levels/w%d/%s_u.t3x", w, wnames[w][l]);
    floor_u = C2D_SpriteSheetLoad(path);
    if (level.has_over) {
        snprintf(path, sizeof(path), "romfs:/levels/w%d/%s_o.t3x", w, wnames[w][l]);
        floor_o = C2D_SpriteSheetLoad(path);
    }
    cur_world = w;
    cur_level = l;
    game.ndemo = 0;
    game_start(&game, &level, MODE_NORMAL, &settings);
    jumpscare_t = results_t = win_t = 0;
    results_stage = 0;
    dragging = false;
    music_play(level.music);
    state = ST_PLAY;
    return true;
}

static void next_level(void) {
    if (cur_level + 1 < wcount[cur_world]) start_level(cur_world, cur_level + 1);
    else if (cur_world + 1 < 8 && wcount[cur_world + 1] > 0) start_level(cur_world + 1, 0);
    else {
        unload_level();
        state = ST_SELECT;
        music_play(1);
    }
}

// ---------------------------------------------------------------- sounds
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
    if (ev & EV_CAUGHT) {
        sfx_play("ghost/defaultJumpscare.ogg", 1, 1);
        jumpscare_t = 50;
    }
    if (ev & EV_BACKROOMS) music_play(-2);
}

// ---------------------------------------------------------------- drawing helpers
// citro2d keeps the higher depth on top, so UI layers raise ui_z before drawing.
static float ui_z = 0.5f;

static void text(float x, float y, float size, u32 color, u32 flags, const char *s) {
    C2D_Text t;
    C2D_TextParse(&t, tbuf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | flags, x, y, ui_z, size, size, color);
}

static void draw_sprite(const char *name, vec2 screen, float px_per_unit, float extra_scale, u32 tint_rgba,
                        float angle) {
    int i = spr_find(name);
    if (i < 0 || !sprites) return;
    SpriteInfo *s = &sprinfo[i];
    C2D_Image img = C2D_SpriteSheetGetImage(sprites, s->idx);
    float sx = s->ux * px_per_unit * extra_scale, sy = s->uy * px_per_unit * extra_scale;
    C2D_DrawParams p = {{screen.x, screen.y, img.subtex->width * sx, img.subtex->height * sy},
                        {s->ax * sx, s->ay * sy}, ui_z, angle};
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, tint_rgba, 1.0f); // tint mode is multiply (set at start-up)
    C2D_DrawImage(img, &p, &tint);
}

static u32 shade_color(float a) {
    return C2D_Color32f(level.shade[0], level.shade[1], level.shade[2], level.shade[3] * a);
}

static void button(float x, float y, float w, float h, const char *label, bool hl) {
    C2D_DrawRectSolid(x, y, ui_z, w, h, hl ? C2D_Color32(255, 200, 80, 255) : C2D_Color32(40, 40, 60, 230));
    C2D_DrawRectSolid(x + 2, y + 2, ui_z, w - 4, h - 4, hl ? C2D_Color32(255, 235, 150, 255) : C2D_Color32(70, 70, 100, 230));
    text(x + w / 2, y + h / 2 - 9, 0.6f, hl ? C2D_Color32(30, 20, 0, 255) : C2D_Color32(255, 255, 255, 255),
         C2D_AlignCenter, label);
}

static bool hit_box(vec2 t, float x, float y, float w, float h) {
    return t.x >= x && t.x < x + w && t.y >= y && t.y < y + h;
}

static void format_time(char *out, size_t n, float t) {
    int ms = (int)(t * 100) % 100, s = (int)t;
    snprintf(out, n, "%d:%02d.%02d", s / 60, s % 60, ms);
}

static const char *RANK_NAMES[6] = {"UNCANNY", "ATROCIOUS", "STINKY", "OK", "SWAG", "PEAK"};

// ---------------------------------------------------------------- course
static void draw_course(void) {
    float k = course_scale();
    C2D_DrawRectSolid(0, 0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, 255));
    if (floor_u) {
        C2D_Image img = C2D_SpriteSheetGetImage(floor_u, 0);
        float s = COURSE_W / level.image_size;
        if (level.flags & LF_MIRROR) C2D_DrawImageAt(img, COURSE_X + COURSE_W, 0, 0.1f, NULL, -s, s);
        else C2D_DrawImageAt(img, COURSE_X, 0, 0.1f, NULL, s, s);
    }
    if (game.has_goal) draw_sprite("goal", world_to_screen(game.goal_pos), k, 1, shade_color(1), 0);
    for (int i = 0; i < game.ncoins; i++)
        if (!game.coins[i].taken) draw_sprite("coin", world_to_screen(game.coins[i].pos), k, 1, shade_color(1), 0);
    if (game.sprite_visible) {
        float sc = game.sprite_scale / 0.064f;
        draw_sprite("costume_canny", world_to_screen(game.pos), k, sc, shade_color(1), 0);
    }
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
        float s = COURSE_W / level.image_size;
        if (level.flags & LF_MIRROR) C2D_DrawImageAt(img, COURSE_X + COURSE_W, 0, 0.7f, NULL, -s, s);
        else C2D_DrawImageAt(img, COURSE_X, 0, 0.7f, NULL, s, s);
    }
    // coin score popups
    ui_z = 0.75f;
    for (int i = 0; i < game.ncoins; i++)
        if (game.coins[i].popup > 0) {
            char b[16];
            snprintf(b, sizeof(b), "%d", game.coins[i].score);
            vec2 p = world_to_screen(game.coins[i].pos);
            text(p.x, p.y - 14 - (1 - game.coins[i].popup) * 10, 0.45f, C2D_Color32(255, 255, 120, 255), C2D_AlignCenter, b);
        }
    // side strips
    C2D_DrawRectSolid(0, 0, 0.8f, COURSE_X, SCR_H, C2D_Color32(20, 18, 30, 255));
    C2D_DrawRectSolid(COURSE_X + COURSE_W, 0, 0.8f, COURSE_X, SCR_H, C2D_Color32(20, 18, 30, 255));
    ui_z = 0.85f;
    button(2, 4, 36, 40, "II", false);
    button(282, 4, 36, 40, "R", false);
    if (!(level.flags & LF_UNSKIPPABLE)) {
        button(282, 196, 36, 40, ">>", game.skipcounter > 0);
        if (game.skipcounter > 0) C2D_DrawRectSolid(284, 192, 0.9f, 32 * game.skipcounter / (float)SKIPCOUNT, 3, C2D_Color32(255, 220, 80, 255));
    }
}

static void draw_top_hud(void) {
    char b[160], v[32];
    C2D_DrawRectSolid(0, 0, 0, TOP_W, SCR_H, C2D_Color32(28, 22, 44, 255));
    // level name
    char key[48];
    snprintf(key, sizeof(key), "GAME_Level%s", level.id);
    snprintf(b, sizeof(b), "%s: %s", level.id, tr(key));
    text(10, 6, 0.7f, C2D_Color32(255, 255, 255, 255), 0, b);
    snprintf(b, sizeof(b), "Level by: %s", level.author);
    text(10, 30, 0.5f, C2D_Color32(190, 180, 220, 255), 0, b);
    // thinker: the cat's picture, until the thoughts animations are converted
    draw_sprite("costume_canny", v2(320, 140), 2.6f, 1, C2D_Color32(255, 255, 255, 255), 0);
    // strokes / par
    snprintf(v, sizeof(v), "%d", game.strokes);
    trf(b, sizeof(b), "GAME_Strokes", "stroke", v);
    u32 sc = game.strokes > game.par ? C2D_Color32(255, 120, 200, 255) : C2D_Color32(255, 255, 255, 255);
    text(14, 66, 0.9f, sc, 0, b);
    snprintf(b, sizeof(b), "Par: %d", game.par);
    text(14, 98, 0.7f, C2D_Color32(220, 220, 255, 255), 0, b);
    // time
    format_time(v, sizeof(v), game.time);
    u32 tc = game.time > level.on_time + 25 ? C2D_Color32(255, 0, 0, 255)
             : game.time >= level.on_time    ? C2D_Color32(255, 255, 0, 255)
                                             : C2D_Color32(0, 255, 0, 255);
    text(14, 130, 0.9f, tc, 0, v);
    snprintf(v, sizeof(v), "%d", game.level_bonus);
    trf(b, sizeof(b), "GAME_LevelBonus", "bonus", v);
    text(14, 166, 0.55f, C2D_Color32(255, 230, 140, 255), 0, b);
}

static void draw_results_top(void) {
    char b[128], v[32];
    C2D_DrawRectSolid(0, 0, 0, TOP_W, SCR_H, C2D_Color32(28, 22, 44, 255));
    text(200, 8, 0.8f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, level.id);
    snprintf(v, sizeof(v), "%d", game.strokes);
    trf(b, sizeof(b), "GAME_Strokes", "stroke", v);
    text(20, 44, 0.6f, C2D_Color32(255, 255, 255, 255), 0, b);
    snprintf(b, sizeof(b), "+%d", game.stroke_bonus);
    text(380, 44, 0.6f, C2D_Color32(0, 255, 0, 255), C2D_AlignRight, b);
    format_time(v, sizeof(v), game.time);
    trf(b, sizeof(b), "GAME_Time", "time", v);
    text(20, 74, 0.6f, C2D_Color32(255, 255, 255, 255), 0, b);
    snprintf(b, sizeof(b), "+%d", game.time_bonus);
    text(380, 74, 0.6f, game.time < level.on_time ? C2D_Color32(0, 255, 0, 255) : C2D_Color32(255, 200, 0, 255),
         C2D_AlignRight, b);
    snprintf(v, sizeof(v), "%d", game.level_bonus);
    trf(b, sizeof(b), "GAME_LevelBonus", "bonus", v);
    text(20, 104, 0.6f, C2D_Color32(255, 230, 140, 255), 0, b);
    int shown = results_stage >= 1 ? game.final_score : (int)(game.final_score * fminf(1, results_t / 60.0f));
    snprintf(b, sizeof(b), "%d", shown);
    text(200, 140, 1.1f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, b);
    // rank bar
    float frac = fminf(1, shown / (float)level.ranks[5]);
    C2D_DrawRectSolid(40, 190, 0, 320, 14, C2D_Color32(60, 50, 80, 255));
    C2D_DrawRectSolid(40, 190, 0, 320 * frac, 14, C2D_Color32(255, 200, 60, 255));
    for (int i = 1; i < 6; i++)
        C2D_DrawRectSolid(40 + 320 * level.ranks[i] / (float)level.ranks[5] - 1, 186, 0, 2, 22, C2D_Color32(255, 255, 255, 200));
}

static void draw_select(void) {
    C2D_DrawRectSolid(0, 0, 0, BOT_W, SCR_H, C2D_Color32(28, 22, 44, 255));
    text(160, 6, 0.6f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, "World 0");
    for (int i = 0; i < wcount[0]; i++) {
        char key[48], b[96], id[16];
        snprintf(id, sizeof(id), "0-%d", i + 1);
        snprintf(key, sizeof(key), "GAME_Level%s", id);
        snprintf(b, sizeof(b), "%s  %s", id, tr(key));
        button(20, 32 + i * 40, 280, 34, b, i == sel_cursor);
    }
}

static void draw_select_top(void) {
    C2D_DrawRectSolid(0, 0, 0, TOP_W, SCR_H, C2D_Color32(28, 22, 44, 255));
    text(200, 30, 1.0f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, "Uncanny Cat Golf");
    text(200, 66, 0.55f, C2D_Color32(200, 190, 230, 255), C2D_AlignCenter, "3DS test build: World 0");
    draw_sprite("costume_canny", v2(200, 160), 2.0f, 1, C2D_Color32(255, 255, 255, 255), 0);
    text(200, 214, 0.45f, C2D_Color32(170, 160, 200, 255), C2D_AlignCenter, "Tap a level, or D-Pad + A");
}

static void draw_pause(void) {
    C2D_DrawRectSolid(0, 0, 0.9f, BOT_W, SCR_H, C2D_Color32(0, 0, 0, 170));
    text(160, 20, 0.8f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, "Paused");
    const char *items[3] = {"Resume", "Restart", "Level select"};
    for (int i = 0; i < 3; i++) button(60, 70 + i * 50, 200, 40, items[i], i == pause_cursor);
}

// ---------------------------------------------------------------- main
int main(void) {
    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
    C2D_Prepare();
    C2D_SetTintMode(C2D_TintMult);
    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    tbuf = C2D_TextBufNew(8192);
    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);

    load_sprites();
    load_lang("en");
    load_level_index();
    audio_init();
    sfx_preload("player/grab.ogg");
    sfx_preload("player/hit.ogg");
    sfx_preload("player/wallBump.ogg");
    music_play(1);

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown(), kh = hidKeysHeld(), ku = hidKeysUp();
        touchPosition tp;
        hidTouchRead(&tp);
        vec2 touch = v2(tp.px, tp.py);
        bool tdown = kd & KEY_TOUCH, theld = kh & KEY_TOUCH, tup = ku & KEY_TOUCH;
        if (theld) last_touch = touch;

        if (state == ST_SELECT) {
            if (kd & KEY_DOWN) sel_cursor = (sel_cursor + 1) % wcount[0];
            if (kd & KEY_UP) sel_cursor = (sel_cursor + wcount[0] - 1) % wcount[0];
            if (kd & KEY_A) start_level(0, sel_cursor);
            if (tdown)
                for (int i = 0; i < wcount[0]; i++)
                    if (hit_box(touch, 20, 32 + i * 40, 280, 34)) {
                        sel_cursor = i;
                        start_level(0, i);
                    }
            if (kd & KEY_START) break;
        } else if (state == ST_PAUSE) {
            int pick = -1;
            if (kd & KEY_DOWN) pause_cursor = (pause_cursor + 1) % 3;
            if (kd & KEY_UP) pause_cursor = (pause_cursor + 2) % 3;
            if (kd & KEY_A) pick = pause_cursor;
            if (kd & (KEY_START | KEY_B)) pick = 0;
            if (tdown)
                for (int i = 0; i < 3; i++)
                    if (hit_box(touch, 60, 70 + i * 50, 200, 40)) pick = i;
            if (pick == 0) state = ST_PLAY;
            else if (pick == 1) start_level(cur_world, cur_level);
            else if (pick == 2) {
                unload_level();
                state = ST_SELECT;
                music_play(1);
            }
        } else if (state == ST_PLAY) {
            GameInput in;
            memset(&in, 0, sizeof(in));
            bool on_course = touch.x >= COURSE_X && touch.x < COURSE_X + COURSE_W;
            bool pause_btn = tdown && hit_box(touch, 0, 0, 40, 48);
            bool restart_btn = tdown && hit_box(touch, 280, 0, 40, 48);
            bool skip_btn = theld && hit_box(touch, 280, 192, 40, 48);
            if (tdown && on_course) dragging = true;
            if (dragging) {
                in.mouse = touch_to_viewport(last_touch);
                in.click_pressed = tdown;
                in.click_down = theld;
                in.click_released = tup;
                if (tup) dragging = false;
            } else {
                in.mouse = touch_to_viewport(last_touch);
            }
            if (kd & KEY_A) in.click_pressed = true, in.click_down = true;
            if (kh & KEY_A) in.click_down = true;
            if (ku & KEY_A) in.click_released = true;
            in.restart_pressed = (kd & KEY_Y) || restart_btn;
            in.skip_down = (kh & KEY_X) || skip_btn;
            in.pause_pressed = (kd & KEY_START) || pause_btn;

            if (game.victory) {
                // results: tap to speed up, then tap for the next level
                results_t++;
                if (in.click_pressed || (kd & KEY_A)) {
                    if (results_stage == 0 && win_t > 40) results_stage = 1, results_t = 60;
                    else if (results_stage == 1) next_level();
                }
                if (in.restart_pressed) start_level(cur_world, cur_level);
                if (state == ST_PLAY && game.victory) {
                    game_tick(&game, &(GameInput){0});
                    win_t++;
                    if (results_t >= 60 && results_stage == 0) {
                        results_stage = 1;
                        static const char *RS[6] = {"gimmicks/screamsOfDeadPeople.ogg", "game/rankAtrocious.ogg",
                                                    "game/rankErm.ogg", "game/rankOK.ogg", "game/rankSwag.ogg",
                                                    "game/rankPeak.ogg"};
                        sfx_play(RS[game.rank], 1, 1);
                    }
                }
            } else {
                game_tick(&game, &in);
                play_events(game.events);
                if (game.events & EV_PAUSE) {
                    state = ST_PAUSE;
                    pause_cursor = 0;
                }
                if (game.progress == PROG_RESET || game.progress == PROG_WIN_RESET) start_level(cur_world, cur_level);
                else if (game.progress == PROG_SKIP) next_level();
            }
            if (jumpscare_t > 0) jumpscare_t--;
        }

        music_update();

        // ---- draw
        C2D_TextBufClear(tbuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 255));

        C2D_SceneBegin(top);
        ui_z = 0.5f;
        if (state == ST_SELECT) draw_select_top();
        else if (game.victory && results_t > 20) draw_results_top();
        else draw_top_hud();
        ui_z = 0.99f;
        if (jumpscare_t > 0) draw_sprite("costume_uncanny", v2(200, 120), 6.0f, 1, C2D_Color32(255, 255, 255, 255), 0);

        C2D_SceneBegin(bot);
        ui_z = 0.5f;
        if (state == ST_SELECT) draw_select();
        else {
            draw_course();
            ui_z = 0.97f;
            if (state == ST_PAUSE) draw_pause();
            if (game.lose && jumpscare_t == 0) {
                static const char *WHY[6] = {"Caught by the uncanny!", "Out of bounds!", "...where are you?",
                                             "Stuck in the wall!", "Horse!", "Splash!"};
                C2D_DrawRectSolid(COURSE_X, 90, 0.95f, COURSE_W, 60, C2D_Color32(0, 0, 0, 190));
                text(160, 96, 0.6f, C2D_Color32(255, 120, 120, 255), C2D_AlignCenter, WHY[game.death_type]);
                text(160, 122, 0.5f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, "Tap to try again");
            }
            if (game.victory && results_stage >= 1) {
                C2D_DrawRectSolid(COURSE_X, 70, 0.95f, COURSE_W, 100, C2D_Color32(0, 0, 0, 200));
                text(160, 80, 1.1f, C2D_Color32(255, 220, 80, 255), C2D_AlignCenter, RANK_NAMES[game.rank]);
                text(160, 136, 0.5f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, "Tap for the next level");
            }
            ui_z = 0.99f;
            if (jumpscare_t > 0) draw_sprite("costume_uncanny", v2(160, 120), 4.0f, 1, C2D_Color32(255, 255, 255, 255), 0);
        }
        C3D_FrameEnd(0);
    }

    unload_level();
    audio_exit();
    C2D_TextBufDelete(tbuf);
    if (sprites) C2D_SpriteSheetFree(sprites);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    return 0;
}
