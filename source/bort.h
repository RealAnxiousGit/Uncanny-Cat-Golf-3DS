// Bort Bash (minigames/bartbash): Borts fly around a cage, tap them before the 30 seconds run out.
// Plain logic in the original's pixels (the cage is x 256..768, y 64..512), no drawing, so the PC
// tools can run it too.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BORT_X0 256.0f
#define BORT_X1 768.0f
#define BORT_Y0 64.0f
#define BORT_Y1 512.0f
#define BORT_MAX 160
#define BORT_HUES 8
#define BORT_BOOM_FRAMES 17

enum { BORT_READY, BORT_PLAYING, BORT_CLEARED, BORT_OVER };
enum { BEV_OW = 1, BEV_CONGRATS = 2, BEV_LOSE = 4, BEV_MUSIC = 8 };

typedef struct {
    bool on, bashed;
    float x, y, vx, vy;
    int hue, frame, wing, wing_time;
    int boom; // explosion frames shown (30 a second)
} Bort;

typedef struct {
    int mode, round, score, left, rank;
    int timer;       // frames left in the round (30 s)
    int clear_wait;  // the congrats sound plays out before the next round
    int events;      // BEV_ bits from the last tick
    uint32_t rng;
    Bort b[BORT_MAX];
} BortGame;

void bort_start(BortGame *g, uint32_t seed); // the "Click all the Borts" prompt
void bort_go(BortGame *g);                   // its OK button: round 1
void bort_tick(BortGame *g);
int bort_tap(BortGame *g, float x, float y); // a tap at the original's pixels; how many were bashed
int bort_seconds(const BortGame *g);         // the timer label: ceil(time left)
