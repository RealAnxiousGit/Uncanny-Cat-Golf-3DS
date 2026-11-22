// Playthroughs, unlocks, and Endless leaderboard (save_data.gd + pthru_data.gd).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MAX_PTHRUS 6
#define MAX_ENDLESS_SCORES 10

// Unlock IDs matching save_data.gd
enum {
    UL_PEAK_HUNTER = 0,
    UL_MODE_ENDLESS,
    UL_MOD_EASY, UL_MOD_HARD, UL_MOD_BURST, UL_MOD_TELEKINESIS, UL_MOD_BUDDY,
    UL_MOD_PARRY, UL_MOD_JUMPY, UL_MOD_DIZZY, UL_MOD_MISSILES, UL_MOD_STICKY, UL_MOD_GOD,
    UL_MG_BORT, UL_MG_MEOWL, UL_MG_DASH,
    UL_EX_COSTUMES, UL_EX_PHONE, UL_EX_SOUNDTEST,
    UL_COSTUME_MS_CANNY, UL_COSTUME_RETRO, UL_COSTUME_DOG, UL_COSTUME_VETERAN,
    UL_COSTUME_CANNYDIGO, UL_COSTUME_RAINBOW, UL_COSTUME_KING, UL_COSTUME_CATFISH,
    UL_COSTUME_NOTHING, UL_COSTUME_COPYCAT,
    UL_COSTUME_WIRES, UL_COSTUME_BINGUS, UL_COSTUME_MINNIE, UL_COSTUME_ROBERT,
    UL_COSTUME_RUDY, UL_COSTUME_PLOOBIE, UL_COSTUME_PIG, UL_COSTUME_CHEN,
    UL_COSTUME_SWAG, UL_COSTUME_ARLELING, UL_COSTUME_TRUE,
    UL_SONGS_FIELDS, UL_SONGS_COSMIC,
    UL_COUNT
};

typedef struct {
    char name[16];
    int score;
    int cleared;
} EndlessEntry;

typedef struct {
    int best_score;
    int rank; // -1 unplayed, 0..5
    int best_strokes;
    float best_time;
} LevelRecord;

typedef struct {
    bool used;
    char name[24];
    uint32_t mods;
    bool peak_hunter;
    int total_prisms;
    int total_score;
    float total_time;
    int total_resets;
    int furthest_world;
    int furthest_level;
    bool can_swim;
    LevelRecord rec[7][32]; // W0..W6
} PthruSave;

typedef struct {
    char current_costume[24];
    int worlds_progress;
    int quest_progress;
    bool unlocks[UL_COUNT];
    int bort_best;
    int active_pthru;
    PthruSave pthrus[MAX_PTHRUS];
    int n_endless;
    EndlessEntry endless[MAX_ENDLESS_SCORES];
    // Toast notification queue
    int toast_q[8];
    int toast_n;
} GlobalSave;

extern GlobalSave g_save;

void pthru_init(void);
void pthru_flush(void);
int pthru_unlock_by_key(const char *key);
const char *pthru_unlock_key(int ul);
const char *pthru_unlock_label(int ul);
void pthru_award(int ul);
int pthru_pop_toast(void);

void pthru_create(int slot, const char *name, uint32_t mods, bool peak_hunter);
void pthru_delete(int slot);
PthruSave *pthru_cur(void);
void pthru_record_level(int w, int l, int score, float time, int rank, int strokes, bool hide_scoring,
                        const int wcount[7]);
void pthru_on_world_clear(int w);
void pthru_on_endless_progress(int cleared, int score, const char *name);
