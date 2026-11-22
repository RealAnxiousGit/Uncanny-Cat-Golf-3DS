#include "endless.h"
#include "game.h"
#include <string.h>

static uint32_t erand(EndlessRun *e) {
    e->rng = e->rng * 1664525u + 1013904223u;
    return e->rng;
}

static void shuffle_pool(EndlessRun *e) {
    for (int i = e->pool_n - 1; i > 0; i--) {
        int j = (int)(erand(e) % (uint32_t)(i + 1));
        EndlessLevelRef t = e->pool[i];
        e->pool[i] = e->pool[j];
        e->pool[j] = t;
    }
}

void endless_start(EndlessRun *e, const int wcount[8], uint32_t seed) {
    memset(e, 0, sizeof(*e));
    e->rng = seed ? seed : 0x1337C0DEu;
    e->active = true;
    e->total_time = 61.0f;
    e->skips = 3;
    e->score_til_skip = 200000;
    e->demon_meter = DEMON_METER_MAX;
    e->mod_countdown = 8;

    // E_01..E_09 (E_00 is starting_level) + W0..W4 excluding 3-18 (idx 17) and 4-18 (idx 17)
    for (int i = 1; i < wcount[7] && e->pool_n < ENDLESS_POOL_MAX; i++)
        e->pool[e->pool_n++] = (EndlessLevelRef){7, i};
    for (int w = 0; w <= 4; w++) {
        for (int i = 0; i < wcount[w] - 1 && e->pool_n < ENDLESS_POOL_MAX; i++) {
            if ((w == 3 && i == 17) || (w == 4 && i == 17)) continue;
            e->pool[e->pool_n++] = (EndlessLevelRef){w, i};
        }
    }
    shuffle_pool(e);
}

static void pick_mods(EndlessRun *e) {
    static const uint32_t POOL[9] = {
        MOD_HARD, MOD_JUMPY, MOD_DIZZY, MOD_MISSILES, MOD_STICKY,
        MOD_E_BORT, MOD_E_BEES, MOD_E_OVERHEAT, MOD_E_PHARAOH
    };
    uint32_t avail[9];
    memcpy(avail, POOL, sizeof(POOL));
    int n = 9;
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(erand(e) % (uint32_t)(i + 1));
        uint32_t t = avail[i];
        avail[i] = avail[j];
        avail[j] = t;
    }
    e->mods = 0;
    for (int i = 0; i < e->mod_minimum && n > 0; i++)
        e->mods |= avail[--n];

    e->mod_countdown--;
    if (e->mod_countdown <= 0) {
        e->events |= EEV_MOD_ACTIVE;
        int beaten = e->rotations_done * e->pool_n + e->slot;
        static const struct { int at, freq; } FREQ[4] = {{0, 8}, {24, 4}, {48, 2}, {72, 1}};
        for (int i = 0; i < 4; i++) {
            if (beaten - e->mod_level_base >= FREQ[i].at) {
                e->mod_countdown = FREQ[i].freq;
                if (e->mod_countdown == 1) {
                    e->mod_level_base = beaten;
                    e->mod_minimum++;
                    e->mod_countdown = FREQ[0].freq;
                }
            }
        }
        if (n > 0) e->mods |= avail[--n];
    }
}

void endless_switch(EndlessRun *e, int trans, int level_score, float on_time, vec2 spawn_demon_at) {
    if (!e->active) return;
    bool add_time = true, change_mod = true;
    if (trans == 1) { // RESET
        add_time = false;
        change_mod = false;
        e->demon_meter -= (e->demon_active && e->demon_meter < DEMON_METER_MAX) ? 5.0f : 30.0f;
    } else if (trans == 2) { // SKIP
        e->events |= EEV_SKIP | EEV_PASS;
        add_time = false;
        if (e->skips > 0) e->skips--;
        e->slot++;
        if (e->slot > e->pool_n) {
            shuffle_pool(e);
            e->rotations_done++;
            e->slot = 0;
        }
    } else { // NORMAL win
        e->events |= EEV_PASS;
        e->cleared++;
        e->slot++;
        if (e->slot > e->pool_n) {
            shuffle_pool(e);
            e->rotations_done++;
            e->slot = 0;
        }
        e->total_score += level_score;
        e->score_til_skip -= level_score;
        if (e->score_til_skip <= 0) {
            e->score_til_skip += 500000;
            e->skips++;
            e->events |= EEV_EXTRA_SKIP;
        }
    }
    if (add_time) e->total_time += on_time;

    if (e->demon_meter <= 0.0f && !e->demon_active) {
        e->demon_active = true;
        e->demon_radical = (erand(e) % 100u) == 0u;
        e->events |= EEV_DEMON_START;
    } else if (e->demon_meter >= DEMON_METER_MAX && e->demon_active) {
        e->demon_active = false;
        e->demon_intro_done = false;
        e->events |= EEV_DEMON_END;
    }
    if (change_mod) pick_mods(e);
    if (e->demon_active) {
        e->demon_pos = spawn_demon_at;
        e->demon_cooldown = 0;
    }
}

void endless_tick(EndlessRun *e, vec2 player_pos, float dt) {
    e->events = 0;
    if (!e->active) return;
    e->total_time -= dt;
    if (e->total_time <= 0.0f) {
        e->total_time = 0.0f;
        e->active = false;
        e->events |= EEV_GAME_OVER;
        return;
    }
    if (e->demon_meter < 0.0f) {
        if (e->demon_active) e->demon_meter = 0.0f;
    } else {
        e->demon_meter += (e->mods & MOD_E_OVERHEAT) ? -dt : dt;
    }
    if (e->demon_meter > DEMON_METER_MAX) e->demon_meter = DEMON_METER_MAX;
    if (e->demon_jumpscare_t > 0) e->demon_jumpscare_t--;
    if (e->demon_active) {
        if (e->demon_cooldown > 0) e->demon_cooldown--;
        vec2 dir = v2norm(v2sub(player_pos, e->demon_pos));
        float spd = e->demon_radical ? 110.0f : 65.0f;
        e->demon_pos = v2add(e->demon_pos, v2mul(dir, spd * dt));
        float rr = 14.0f + PLAYER_RADIUS;
        if (e->demon_cooldown <= 0 && v2len2(v2sub(e->demon_pos, player_pos)) < rr * rr) {
            e->total_time -= 5.0f;
            e->demon_cooldown = 60;
            e->demon_jumpscare_t = 40;
            e->events |= EEV_DEMON_HIT;
        }
    }
}

EndlessLevelRef endless_current(const EndlessRun *e) {
    if (e->slot <= 0 || e->pool_n <= 0) return (EndlessLevelRef){7, 0};
    return e->pool[(e->slot - 1) % e->pool_n];
}

EndlessLevelRef endless_next(const EndlessRun *e) {
    if (e->slot >= e->pool_n || e->pool_n <= 0) return (EndlessLevelRef){7, 0};
    return e->pool[e->slot];
}
