#include "pthru.h"
#include "game.h"
#ifdef __3DS__
#include "save.h"
#endif

#include <stdio.h>
#include <string.h>

GlobalSave g_save;

static const char *UL_KEYS[UL_COUNT] = {
    "game_peak_hunter", "mode_endless",
    "mod_easy_mode", "mod_hard_mode", "mod_burst", "mod_telekinesis", "mod_buddy",
    "mod_parry", "mod_jumpy", "mod_dizzy", "mod_missiles", "mod_sticky", "mod_god",
    "minigame_bartbash", "minigame_meowl", "minigame_uncannydash",
    "extra_costumes", "extra_mystery_phone", "extra_sound_test",
    "costume_ms_canny", "costume_retro", "costume_dog", "costume_veteran",
    "costume_cannydigo", "costume_rainbow", "costume_king", "costume_catfish",
    "costume_nothing", "costume_copycat",
    "costume_wires", "costume_bingus", "costume_minnie", "costume_robert",
    "costume_rudy", "costume_ploobie", "costume_pig", "costume_chen",
    "costume_swag", "costume_arleling", "costume_true",
    "songs_fields", "songs_cosmic",
};

static const char *UL_LABELS[UL_COUNT] = {
    "Peak Hunter", "Endless Mode",
    "Easy Mode", "Hard Mode", "Burst Mod", "Telekinesis Mod", "Little Buddy Mod",
    "Parry Mod", "Jumpy Mod", "Dizzy Mod", "Missiles Mod", "Sticky Mod", "God Mode",
    "Bort Bash", "Meowls.", "Uncanny Dash",
    "Costume House", "Mystery Phone", "Sound Test",
    "Ms. Canny", "Retro Canny", "Dog Costume", "Veteran Canny",
    "Cannydigo", "Rainbow Canny", "King Canny", "Catfish",
    "Nothing", "Copycat",
    "Wires", "Bingus", "Minnie", "Robert",
    "Rudy", "Ploobie", "Pig", "Chen",
    "Swag Canny", "Arleling", "True Canny",
    "Fields Tracks", "Cosmic Tracks",
};

#ifdef __3DS__
static Cfg *gs_cfg;
static Cfg *pt_cfg[MAX_PTHRUS];
#endif

int pthru_unlock_by_key(const char *key) {
    if (!key || !key[0] || !strcmp(key, "-")) return -1;
    for (int i = 0; i < UL_COUNT; i++)
        if (!strcmp(UL_KEYS[i], key)) return i;
    return -1;
}

const char *pthru_unlock_key(int ul) { return (ul >= 0 && ul < UL_COUNT) ? UL_KEYS[ul] : ""; }
const char *pthru_unlock_label(int ul) { return (ul >= 0 && ul < UL_COUNT) ? UL_LABELS[ul] : ""; }

void pthru_award(int ul) {
    if (ul < 0 || ul >= UL_COUNT || g_save.unlocks[ul]) return;
    g_save.unlocks[ul] = true;
    if (g_save.toast_n < 8) g_save.toast_q[g_save.toast_n++] = ul;
}

int pthru_pop_toast(void) {
    if (g_save.toast_n <= 0) return -1;
    int r = g_save.toast_q[0];
    memmove(&g_save.toast_q[0], &g_save.toast_q[1], (g_save.toast_n - 1) * sizeof(int));
    g_save.toast_n--;
    return r;
}

static void pthru_reset_slot(PthruSave *p, const char *name, uint32_t mods, bool peak_hunter) {
    memset(p, 0, sizeof(*p));
    p->used = true;
    snprintf(p->name, sizeof(p->name), "%s", name && name[0] ? name : "Playthrough");
    p->mods = mods;
    p->peak_hunter = peak_hunter;
    for (int w = 0; w < 7; w++)
        for (int l = 0; l < 32; l++) {
            p->rec[w][l].rank = -1;
            p->rec[w][l].best_time = 1e9f;
        }
}

void pthru_init(void) {
    memset(&g_save, 0, sizeof(g_save));
    snprintf(g_save.current_costume, sizeof(g_save.current_costume), "canny");
    g_save.worlds_progress = -1;
#ifdef __3DS__
    gs_cfg = cfg_open("save_data.ini");
    snprintf(g_save.current_costume, sizeof(g_save.current_costume), "%s",
             cfg_get_str(gs_cfg, "UCG", "current_costume", "canny"));
    g_save.worlds_progress = cfg_get_int(gs_cfg, "UCG", "WORLDS_PROGRESS", -1);
    g_save.quest_progress = cfg_get_int(gs_cfg, "UCG", "QUEST_PROGRESS", 0);
    g_save.bort_best = cfg_get_int(gs_cfg, "BORT", "best", 0);
    g_save.active_pthru = cfg_get_int(gs_cfg, "UCG", "active_pthru", 0);
    if (g_save.active_pthru < 0 || g_save.active_pthru >= MAX_PTHRUS) g_save.active_pthru = 0;
    for (int i = 0; i < UL_COUNT; i++)
        g_save.unlocks[i] = cfg_get_int(gs_cfg, "UNLOCKS", UL_KEYS[i], 0) != 0;
    g_save.n_endless = cfg_get_int(gs_cfg, "ENDLESS", "count", 0);
    if (g_save.n_endless > MAX_ENDLESS_SCORES) g_save.n_endless = MAX_ENDLESS_SCORES;
    for (int i = 0; i < g_save.n_endless; i++) {
        char k[24];
        snprintf(k, sizeof(k), "%dname", i);
        snprintf(g_save.endless[i].name, sizeof(g_save.endless[i].name), "%s",
                 cfg_get_str(gs_cfg, "ENDLESS", k, "PLAYER"));
        snprintf(k, sizeof(k), "%dscore", i);
        g_save.endless[i].score = cfg_get_int(gs_cfg, "ENDLESS", k, 0);
        snprintf(k, sizeof(k), "%dcleared", i);
        g_save.endless[i].cleared = cfg_get_int(gs_cfg, "ENDLESS", k, 0);
    }
    for (int s = 0; s < MAX_PTHRUS; s++) {
        char fn[24];
        snprintf(fn, sizeof(fn), "pthru_%d.ini", s);
        pt_cfg[s] = cfg_open(fn);
        PthruSave *p = &g_save.pthrus[s];
        if (!cfg_get_int(pt_cfg[s], "META", "used", 0)) continue;
        pthru_reset_slot(p, cfg_get_str(pt_cfg[s], "META", "name", "Run"),
                         (uint32_t)cfg_get_int(pt_cfg[s], "META", "mods", 0),
                         cfg_get_int(pt_cfg[s], "META", "peak_hunter", 0) != 0);
        p->total_prisms = cfg_get_int(pt_cfg[s], "META", "total_prisms", 0);
        p->total_score = cfg_get_int(pt_cfg[s], "META", "total_score", 0);
        p->total_time = cfg_get_float(pt_cfg[s], "META", "total_time", 0.0f);
        p->total_resets = cfg_get_int(pt_cfg[s], "META", "total_resets", 0);
        p->furthest_world = cfg_get_int(pt_cfg[s], "META", "furthest_world", 0);
        p->furthest_level = cfg_get_int(pt_cfg[s], "META", "furthest_level", 0);
        p->can_swim = cfg_get_int(pt_cfg[s], "META", "can_swim", 0) != 0;
        for (int w = 0; w < 7; w++)
            for (int l = 0; l < 32; l++) {
                char k[24];
                snprintf(k, sizeof(k), "w%d_%d_r", w, l);
                int r = cfg_get_int(pt_cfg[s], "LEVELS", k, -1);
                if (r < 0) continue;
                p->rec[w][l].rank = r;
                snprintf(k, sizeof(k), "w%d_%d_s", w, l);
                p->rec[w][l].best_score = cfg_get_int(pt_cfg[s], "LEVELS", k, 0);
                snprintf(k, sizeof(k), "w%d_%d_k", w, l);
                p->rec[w][l].best_strokes = cfg_get_int(pt_cfg[s], "LEVELS", k, 0);
                snprintf(k, sizeof(k), "w%d_%d_t", w, l);
                p->rec[w][l].best_time = cfg_get_float(pt_cfg[s], "LEVELS", k, 1e9f);
            }
    }
#endif
    if (!g_save.pthrus[0].used) pthru_reset_slot(&g_save.pthrus[0], "Main", 0, false);
}

void pthru_flush(void) {
#ifdef __3DS__
    if (!gs_cfg) return;
    cfg_set_str(gs_cfg, "UCG", "current_costume", g_save.current_costume);
    cfg_set_int(gs_cfg, "UCG", "WORLDS_PROGRESS", g_save.worlds_progress);
    cfg_set_int(gs_cfg, "UCG", "QUEST_PROGRESS", g_save.quest_progress);
    cfg_set_int(gs_cfg, "BORT", "best", g_save.bort_best);
    cfg_set_int(gs_cfg, "UCG", "active_pthru", g_save.active_pthru);
    for (int i = 0; i < UL_COUNT; i++)
        cfg_set_int(gs_cfg, "UNLOCKS", UL_KEYS[i], g_save.unlocks[i] ? 1 : 0);
    cfg_set_int(gs_cfg, "ENDLESS", "count", g_save.n_endless);
    for (int i = 0; i < g_save.n_endless; i++) {
        char k[24];
        snprintf(k, sizeof(k), "%dname", i);
        cfg_set_str(gs_cfg, "ENDLESS", k, g_save.endless[i].name);
        snprintf(k, sizeof(k), "%dscore", i);
        cfg_set_int(gs_cfg, "ENDLESS", k, g_save.endless[i].score);
        snprintf(k, sizeof(k), "%dcleared", i);
        cfg_set_int(gs_cfg, "ENDLESS", k, g_save.endless[i].cleared);
    }
    cfg_write_later(gs_cfg);

    for (int s = 0; s < MAX_PTHRUS; s++) {
        if (!pt_cfg[s]) continue;
        PthruSave *p = &g_save.pthrus[s];
        cfg_set_int(pt_cfg[s], "META", "used", p->used ? 1 : 0);
        if (p->used) {
            cfg_set_str(pt_cfg[s], "META", "name", p->name);
            cfg_set_int(pt_cfg[s], "META", "mods", (int)p->mods);
            cfg_set_int(pt_cfg[s], "META", "peak_hunter", p->peak_hunter ? 1 : 0);
            cfg_set_int(pt_cfg[s], "META", "total_prisms", p->total_prisms);
            cfg_set_int(pt_cfg[s], "META", "total_score", p->total_score);
            cfg_set_float(pt_cfg[s], "META", "total_time", p->total_time);
            cfg_set_int(pt_cfg[s], "META", "total_resets", p->total_resets);
            cfg_set_int(pt_cfg[s], "META", "furthest_world", p->furthest_world);
            cfg_set_int(pt_cfg[s], "META", "furthest_level", p->furthest_level);
            cfg_set_int(pt_cfg[s], "META", "can_swim", p->can_swim ? 1 : 0);
            for (int w = 0; w < 7; w++)
                for (int l = 0; l < 32; l++) {
                    if (p->rec[w][l].rank < 0) continue;
                    char k[24];
                    snprintf(k, sizeof(k), "w%d_%d_r", w, l);
                    cfg_set_int(pt_cfg[s], "LEVELS", k, p->rec[w][l].rank);
                    snprintf(k, sizeof(k), "w%d_%d_s", w, l);
                    cfg_set_int(pt_cfg[s], "LEVELS", k, p->rec[w][l].best_score);
                    snprintf(k, sizeof(k), "w%d_%d_k", w, l);
                    cfg_set_int(pt_cfg[s], "LEVELS", k, p->rec[w][l].best_strokes);
                    snprintf(k, sizeof(k), "w%d_%d_t", w, l);
                    cfg_set_float(pt_cfg[s], "LEVELS", k, p->rec[w][l].best_time);
                }
        }
        cfg_write_later(pt_cfg[s]);
    }
#endif
}

void pthru_create(int slot, const char *name, uint32_t mods, bool peak_hunter) {
    if (slot < 0 || slot >= MAX_PTHRUS) return;
    pthru_reset_slot(&g_save.pthrus[slot], name, mods, peak_hunter);
    g_save.active_pthru = slot;
    pthru_flush();
}

void pthru_delete(int slot) {
    if (slot <= 0 || slot >= MAX_PTHRUS) return; // keep slot 0 alive
    memset(&g_save.pthrus[slot], 0, sizeof(PthruSave));
    if (g_save.active_pthru == slot) g_save.active_pthru = 0;
    pthru_flush();
}

PthruSave *pthru_cur(void) {
    if (!g_save.pthrus[g_save.active_pthru].used) g_save.active_pthru = 0;
    return &g_save.pthrus[g_save.active_pthru];
}

void pthru_record_level(int w, int l, int score, float time, int rank, int strokes, bool hide_scoring,
                        const int wcount[7]) {
    if (w < 0 || w >= 7 || l < 0 || l >= 32) return;
    PthruSave *p = pthru_cur();
    LevelRecord *r = &p->rec[w][l];
    if (hide_scoring) {
        if (r->rank < 5) {
            p->total_prisms += 5 - (r->rank < 0 ? 0 : r->rank);
            r->rank = 5;
        }
    } else {
        if (score > r->best_score) {
            p->total_score += score - r->best_score;
            r->best_score = score;
            r->best_strokes = strokes;
        }
        if (time < r->best_time) r->best_time = time;
        if (rank > r->rank) {
            p->total_prisms += rank - (r->rank < 0 ? 0 : r->rank);
            r->rank = rank;
        }
    }
    if (w > p->furthest_world || (w == p->furthest_world && l + 1 > p->furthest_level)) {
        p->furthest_world = w;
        p->furthest_level = l + 1;
        if (wcount && p->furthest_level >= wcount[w] && w < 6) {
            p->furthest_world = w + 1;
            p->furthest_level = 0;
        }
    }
    // Level-specific peak unlocks (save_data.gd)
    if (r->rank >= 5) {
        if (w == 2 && l == 9) pthru_award(UL_MOD_JUMPY);
        if (w == 2 && l == 15) pthru_award(UL_MOD_PARRY);
        if (w == 3 && l == 7) pthru_award(UL_MOD_BURST);
        if (w == 4 && l == 5) pthru_award(UL_MOD_DIZZY);
    }
    // Prism thresholds (master.tscn)
    static const struct { int prisms, ul; } PT[] = {
        {100, UL_COSTUME_WIRES}, {150, UL_COSTUME_BINGUS}, {200, UL_COSTUME_MINNIE},
        {250, UL_COSTUME_ROBERT}, {300, UL_COSTUME_RUDY}, {325, UL_COSTUME_PLOOBIE},
        {375, UL_COSTUME_PIG}, {400, UL_COSTUME_CHEN}, {450, UL_COSTUME_SWAG},
        {500, UL_COSTUME_ARLELING}, {525, UL_COSTUME_TRUE}, {565, UL_COSTUME_KING},
    };
    for (int i = 0; i < (int)(sizeof(PT) / sizeof(PT[0])); i++)
        if (p->total_prisms >= PT[i].prisms) pthru_award(PT[i].ul);
    // World peak unlocks
    if (wcount && wcount[w] > 0) {
        bool peaked = true;
        for (int i = 0; i < wcount[w]; i++)
            if (p->rec[w][i].rank < 5) { peaked = false; break; }
        if (peaked) {
            static const int WPEAK[7] = {
                -1, UL_COSTUME_MS_CANNY, UL_COSTUME_RETRO, UL_COSTUME_DOG,
                UL_COSTUME_VETERAN, UL_COSTUME_CANNYDIGO, UL_COSTUME_RAINBOW
            };
            if (WPEAK[w] >= 0) pthru_award(WPEAK[w]);
        }
    }
    pthru_flush();
}

void pthru_on_world_clear(int w) {
    if (w > g_save.worlds_progress) g_save.worlds_progress = w;
    if (w == 0) { pthru_award(UL_MOD_EASY); pthru_award(UL_EX_COSTUMES); }
    if (w == 1) { pthru_award(UL_MG_MEOWL); pthru_award(UL_EX_PHONE); }
    if (w == 2) { pthru_award(UL_MG_DASH); }
    if (w == 3) { pthru_award(UL_MG_BORT); }
    if (w == 4) { pthru_award(UL_MODE_ENDLESS); pthru_award(UL_EX_SOUNDTEST); pthru_award(UL_PEAK_HUNTER); }
    if (w == 5) { pthru_award(UL_MOD_HARD); pthru_award(UL_SONGS_FIELDS); }
    if (w == 6) { pthru_award(UL_MOD_BUDDY); pthru_award(UL_SONGS_COSMIC); }
    pthru_flush();
}

void pthru_on_endless_progress(int cleared, int score, const char *name) {
    if (cleared >= 50) pthru_award(UL_MOD_STICKY);
    if (cleared >= 100) pthru_award(UL_MOD_MISSILES);
    if (cleared >= 150) pthru_award(UL_MOD_TELEKINESIS);
    if (score > 0 && (g_save.n_endless == 0 || score > g_save.endless[0].score))
        pthru_award(UL_COSTUME_CATFISH);
    if (score > 0) {
        EndlessEntry e;
        snprintf(e.name, sizeof(e.name), "%s", name && name[0] ? name : "PLAYER");
        e.score = score;
        e.cleared = cleared;
        int pos = g_save.n_endless;
        for (int i = 0; i < g_save.n_endless; i++)
            if (score > g_save.endless[i].score) { pos = i; break; }
        if (pos < MAX_ENDLESS_SCORES) {
            if (g_save.n_endless < MAX_ENDLESS_SCORES) g_save.n_endless++;
            for (int j = g_save.n_endless - 1; j > pos; j--)
                g_save.endless[j] = g_save.endless[j - 1];
            g_save.endless[pos] = e;
        }
    }
    pthru_flush();
}
