// minigames/bartbash: bort_game.gd and objects/bort.gd
#include "bort.h"
#include <math.h>
#include <string.h>

#define ROUND_FRAMES (30 * 60)      // the Timer's wait_time
#define CONGRATS_FRAMES 120         // bart_congrats.ogg plays out before the next round
#define HALF_W 8.0f                 // the body: 26.667 x 60 scaled 0.6 x 0.533
#define HALF_H 16.0f
#define CLICK_HW 20.0f              // the Clickbox: 40 x 64
#define CLICK_HH 32.0f
#define BOOM_TICKS 2                // explode plays at 30 frames a second

static uint32_t rnd(BortGame *g) {
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}
static int randi(BortGame *g, int a, int b) { return a + (int)(rnd(g) % (uint32_t)(b - a + 1)); }
static float randf(BortGame *g, float a, float b) { return a + (b - a) * (rnd(g) & 0xFFFFFF) / 16777215.0f; }

void bort_start(BortGame *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x2545F491u;
    g->mode = BORT_READY;
    g->timer = ROUND_FRAMES;
}

static void next_round(BortGame *g) {
    g->round++;
    int n = g->round * 5;
    for (int i = 0, k = 0; i < n; i++) {
        while (k < BORT_MAX && g->b[k].on) k++;
        if (k >= BORT_MAX) break;
        Bort *b = &g->b[k];
        memset(b, 0, sizeof(*b));
        b->on = true;
        float speed = (float)randi(g, 100 + g->round * 30, 600 + g->round * 30);
        float a = randf(g, 0.261f, 1.309f) + randi(g, 0, 3) * (float)M_PI * 0.5f;
        b->vx = speed * cosf(a);
        b->vy = speed * sinf(a);
        b->x = (float)randi(g, 320, 704);
        b->y = (float)randi(g, 128, 448);
        b->wing_time = randi(g, 3, 10);
        b->hue = randi(g, 0, BORT_HUES - 1);
        g->left++;
    }
    g->timer = ROUND_FRAMES;
    g->mode = BORT_PLAYING;
}

void bort_go(BortGame *g) {
    if (g->mode != BORT_READY) return;
    g->events |= BEV_MUSIC;
    next_round(g);
}

int bort_seconds(const BortGame *g) { return (g->timer + 59) / 60; }

static void bash(BortGame *g, Bort *b) {
    b->bashed = true;
    b->vx = b->vy = 0;
    g->events |= BEV_OW;
    g->left--;
    g->score += 50;
    if (g->left == 0) { // the timer pauses, what is left of it scores
        g->score += bort_seconds(g) * 100;
        g->events |= BEV_CONGRATS;
        g->mode = BORT_CLEARED;
        g->clear_wait = CONGRATS_FRAMES;
    }
}

int bort_tap(BortGame *g, float x, float y) {
    if (g->mode != BORT_PLAYING) return 0;
    int n = 0; // every Bort under the pointer is bashed by the one click
    for (int i = 0; i < BORT_MAX && g->mode == BORT_PLAYING; i++) {
        Bort *b = &g->b[i];
        if (!b->on || b->bashed) continue;
        if (fabsf(x - b->x) <= CLICK_HW && fabsf(y - b->y) <= CLICK_HH) bash(g, b), n++;
    }
    return n;
}

void bort_tick(BortGame *g) {
    g->events = 0;
    for (int i = 0; i < BORT_MAX; i++) {
        Bort *b = &g->b[i];
        if (!b->on) continue;
        if (b->bashed) {
            if (++b->boom >= BORT_BOOM_FRAMES * BOOM_TICKS) b->on = false;
            continue;
        }
        if (g->mode == BORT_OVER) continue;
        if (b->wing > b->wing_time) b->frame ^= 1, b->wing = 0;
        else b->wing++;
        b->x += b->vx / 60;
        b->y += b->vy / 60;
        // move_and_collide against the cage, then velocity.bounce(normal)
        if (b->x - HALF_W < BORT_X0) b->x = BORT_X0 + HALF_W, b->vx = fabsf(b->vx);
        if (b->x + HALF_W > BORT_X1) b->x = BORT_X1 - HALF_W, b->vx = -fabsf(b->vx);
        if (b->y - HALF_H < BORT_Y0) b->y = BORT_Y0 + HALF_H, b->vy = fabsf(b->vy);
        if (b->y + HALF_H > BORT_Y1) b->y = BORT_Y1 - HALF_H, b->vy = -fabsf(b->vy);
    }
    if (g->mode == BORT_PLAYING && --g->timer <= 0) { // time_over()
        g->timer = 0;
        g->mode = BORT_OVER;
        for (int i = 0; i < BORT_MAX; i++) g->b[i].on = false;
        g->events |= BEV_LOSE;
        int r = g->score / 15000;
        g->rank = r < 0 ? 0 : r > 6 ? 6 : r;
    } else if (g->mode == BORT_CLEARED && --g->clear_wait <= 0) {
        next_round(g);
    }
}
