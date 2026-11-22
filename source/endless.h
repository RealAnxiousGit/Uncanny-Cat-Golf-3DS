// Endless Shuffle mode controller (endless.gd + red_demon.gd).
#pragma once
#include "ucg.h"
#include <stdbool.h>
#include <stdint.h>

#define ENDLESS_POOL_MAX 128
#define DEMON_METER_MAX 120.0f

enum {
    EEV_PASS = 1 << 0,
    EEV_SKIP = 1 << 1,
    EEV_EXTRA_SKIP = 1 << 2,
    EEV_MOD_ACTIVE = 1 << 3,
    EEV_DEMON_START = 1 << 4,
    EEV_DEMON_END = 1 << 5,
    EEV_DEMON_HIT = 1 << 6,
    EEV_GAME_OVER = 1 << 7,
};

typedef struct {
    int world; // 7 for E_*, 0..4 for W0..W4
    int level; // index within wnames[world]
} EndlessLevelRef;

typedef struct {
    bool active;
    float total_time;
    int skips, total_score, score_til_skip;
    int cleared, rotations_done, slot;
    uint32_t mods;
    int mod_countdown, mod_level_base, mod_minimum;

    // Red / Radical Demon
    float demon_meter;
    bool demon_active, demon_radical, demon_intro_done;
    vec2 demon_pos;
    int demon_cooldown;
    int demon_jumpscare_t;

    int pool_n;
    EndlessLevelRef pool[ENDLESS_POOL_MAX];
    uint32_t rng;
    int events;
} EndlessRun;

void endless_start(EndlessRun *e, const int wcount[8], uint32_t seed);
void endless_tick(EndlessRun *e, vec2 player_pos, float dt);
// trans: 0 = win/normal, 1 = reset, 2 = skip
void endless_switch(EndlessRun *e, int trans, int level_score, float on_time, vec2 spawn_demon_at);
EndlessLevelRef endless_current(const EndlessRun *e);
EndlessLevelRef endless_next(const EndlessRun *e);
