// Uncanny Cat Golf 3DS: one level being played. Ported from level.gd, player.gd,
// the player state scripts (PSpawn, PIdle, PMoving, PWin, PLose, PAttractIdle),
// ghost.gd, goal.gd and collectible.gd. Runs at Godot's fixed 60 ticks a second.
#pragma once
#include "ucg.h"

#define TICK (1.0f / 60.0f)

// Constants from player.gd
#define MAX_DRAG 175.0f
#define DRAG_MULTIPLIER 5.0f
#define DRAG_CANCEL 20.0f
#define FRIC_COEFF 0.04f
#define FRIC_POW 1.06f
#define STOP_SPEED 20.0f
#define SHOT_BUFFER_FRAMES 12
#define WALL_DEATH_TIME 10
#define LAUNCH_POWER_MIN (DRAG_CANCEL * DRAG_MULTIPLIER)
#define LAUNCH_POWER_MAX (MAX_DRAG * DRAG_MULTIPLIER)
#define PLAYER_RADIUS 15.8f
#define PLAYER_MASK 9u
#define OB_RADIUS 24
#define SKIPCOUNT 30

enum { PS_SPAWN, PS_IDLE, PS_MOVING, PS_WIN, PS_LOSE, PS_ATTRACT };
enum { DEATH_NORMAL, DEATH_OB, DEATH_BACKROOMS, DEATH_WALL, DEATH_HORSE, DEATH_WATER };
enum { MODE_NORMAL, MODE_ENDLESS, MODE_DEMO };
enum { RANK_UNCANNY, RANK_ATROCIOUS, RANK_STINKY, RANK_OK, RANK_SWAG, RANK_PEAK };
enum { PROG_NONE, PROG_RESET, PROG_NORMAL, PROG_SKIP, PROG_WIN_RESET };

// Sound and effect events raised during a tick (bit flags)
enum {
    EV_JOIN = 1 << 0, EV_GRAB = 1 << 1, EV_CANCEL = 1 << 2, EV_LAUNCH = 1 << 3,
    EV_BUMP = 1 << 4, EV_PUSH = 1 << 5, EV_PICKUP = 1 << 6, EV_WIN = 1 << 7,
    EV_DEATH_WALL = 1 << 8, EV_COIN = 1 << 9, EV_CAUGHT = 1 << 10, EV_BACKROOMS = 1 << 11,
    EV_OB = 1 << 12, EV_PAUSE = 1 << 13, EV_GRAZE = 1 << 14,
};

// Thought animations the top screen should show (thought_displayer categories)
enum { TH_NONE, TH_IDLE, TH_MOVING, TH_WIN, TH_DEATH, TH_BACKROOMS };

typedef struct {
    // positions are in the original's viewport pixels: the course is 0..512 on both axes
    vec2 mouse;
    bool click_pressed, click_down, click_released;
    bool restart_pressed, skip_down, pause_pressed;
} GameInput;

typedef struct {
    int wait_time;
    float power;
    vec2 dir;
} DemoShot;

#define MAX_GHOSTS 8
#define MAX_COINS 64
#define MAX_DEMO_SHOTS 16

typedef struct {
    float power_sensitivity;
    bool invert_shot;
    bool buffer_shot;
} GameSettings;

typedef struct {
    LevelDef lv;
    int mode;
    GameSettings set;
    uint32_t frame;

    // player
    vec2 pos, vel, add_vel;
    int pstate;
    float sprite_scale;
    bool sprite_visible;
    vec2 click_pos;
    vec2 hit;            // aim vector (viewport px), shown while dragging
    bool hit_visible;
    vec2 buffered_shot;
    uint32_t buffered_frame;
    int wall_death_count, wall_death_timer;
    bool moving_first_frame, spawn_first_frame;
    int burst_buffer;
    float bump_pitch, launch_pitch;
    vec2 goal_target;
    int last_bump_shape;

    // things in the level
    int nghosts;
    struct { vec2 pos, vel; bool alive, graze; } ghosts[MAX_GHOSTS];
    bool has_goal;
    vec2 goal_pos;
    float goal_r;
    int ncoins;
    struct { vec2 pos; float r; bool taken; int score, mult, multval; float popup; } coins[MAX_COINS];

    // level state (level.gd)
    bool active, victory, lose;
    int death_type;
    int strokes, par;
    int coin_multiplier, level_bonus;
    float time;
    int stroke_bonus, time_bonus, final_score, rank;
    int skipcounter;
    int progress;

    // attract demo
    int ndemo;
    DemoShot demo[MAX_DEMO_SHOTS];
    int demo_timer;

    uint32_t events;
    int thought;
} Game;

void game_start(Game *g, const LevelDef *lv, int mode, const GameSettings *set);
void game_tick(Game *g, const GameInput *in);
// Aim line colour as in PIdle.gd (r, g, b, a in 0..1)
void game_aim_color(const Game *g, float out[4]);
