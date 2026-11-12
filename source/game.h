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
#define JUMP_MASK 65536u  // player.jump_collision_mask: only "Jump Walls" while in the air
#define JUMP_TIME 26
#define OB_RADIUS 24
#define SKIPCOUNT 30

enum { PS_SPAWN, PS_IDLE, PS_MOVING, PS_WIN, PS_LOSE, PS_ATTRACT, PS_JUMP };
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
    EV_DBLOCK = 1 << 15, EV_KEYDOOR = 1 << 16, EV_KEY = 1 << 17, EV_SKULL = 1 << 18,
    EV_SPLASH = 1 << 19, EV_WATER_DEATH = 1 << 20,
    EV_BOOST = 1 << 21, EV_JUMP = 1 << 22, EV_CAR = 1 << 23, EV_POLICE = 1 << 24, EV_SLOTS = 1 << 25,
};
// World 3 (past an int's bits, so events is 64 bits wide)
#define EV_SWITCH (1ull << 26)
#define EV_DOGBUMP (1ull << 27)
#define EV_DOGWIN (1ull << 28)
#define EV_CHUCKLE (1ull << 29)
#define EV_RING (1ull << 30)
#define EV_SCOIN (1ull << 31)
#define EV_RINGWIN (1ull << 32)
#define EV_RINGFAIL (1ull << 33)

// Thought animations the top screen should show (thought_displayer categories)
enum { TH_NONE, TH_IDLE, TH_MOVING, TH_WIN, TH_DEATH, TH_BACKROOMS, TH_MONEY, TH_SAND, TH_SKULL, TH_CAR, TH_JUMP, TH_POLICE,
       TH_CHUCKLE, TH_DOG };

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
#define MAX_BODIES 64   // blocks, key doors, skulls and cars (they collide)
#define MAX_PADS 16
#define MAX_SLOTS 4
// slot_machine.gd results: which marquee text and sound (slot_sound) to use
enum { SLOT_DEFAULT, SLOT_JACKPOT, SLOT_HAPPY, SLOT_OK, SLOT_QUESTION, SLOT_PHONE, SLOT_CHERRY, SLOT_EYE, SLOT_BLUE,
       SLOT_SAD, SLOT_SKULL, SLOT_CLOSE, SLOT_GOOD, SLOT_BAD, SLOT_LOSE };
#define MAX_SPAWNERS 16
#define MAX_TILES 8
#define MAX_DOGGOALS 4
#define MAX_RINGS 8
#define MAX_SCOINS 48
// smiley_ring.gd: the ring goes (or resets) when its win / lose sound finishes; these are their lengths
#define RING_WIN_TIME 1.826f
#define RING_FAIL_TIME 1.371f
#define MAX_KEYS 12
#define MAX_ZONES 16

// A block, key door or skull: a dynamic physics shape (index lv.nshapes + i)
typedef struct {
    int type;          // OBJ_DBLOCK, OBJ_KEYDOOR, OBJ_SKULL, OBJ_CAR
    int owner;         // the spawner that made it, or -1
    bool ignorey;      // baddie.ignorey: only leaving sideways despawns it
    bool alive;
    vec2 pos, vel;     // skulls move
    vec2 half;         // block half size
    float rot, radius; // block rotation, skull radius
    float color[3];    // key doors
    int score, spr, spr2;
    bool dblock_score;
    float pitch_lo, pitch_hi;
    bool chaser, chuckle;      // baddie.gd chaser (chuckle.tscn)
    float chase_accel, chase_limit;
    bool bounce, winner;       // dog_golf.gd bounce_off_player, winner
    int goal;                  // the dog goal it went into
    float dscale;              // dog_golf.gd sprite.scale (0.064, shrinking once it wins)
    float lock_a;      // the door's lock, fading while a matching key is held
    int popup_score;
    float popup;
} Body;

typedef struct {
    vec2 pos, vel;
    float accel, alpha;
    float color[3];
    int spr, score;
    bool alive, held;
    int popup_score;
    float popup;
} Key;

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
    // coins, and World 2's speed / stop tokens (collectible.gd with does_speed; spr >= 0)
    struct { vec2 pos; float r; bool taken; int score, mult, multval; float popup; bool does_speed; float set_speed; int spr; } coins[MAX_COINS];
    int npads;    // jump_pad.gd
    struct { vec2 pos; float r, dir, speed_granted; int spr; bool in; } pads[MAX_PADS];
    int nspawners; // baddie_spawner.gd
    struct { vec2 pos, dir; float speed, rotate; int score, rate, countdown, max, count, spr, car_spr; bool skull, ignorey, hidden; int enemy; } spawners[MAX_SPAWNERS];
    int jump_timer, jump_pad; // PJump.anim_timer; the pad whose player_jump is due (-1 = none)
    const ObjDef *police;     // police_event.gd settings (the level's "police" object), NULL when none
    bool police_done;         // the event frees itself after it fires
    // slot.gd reels and slot_machine.gd
    int nslots;
    struct { vec2 pos; int frame, rate, cur, coin, spr, nframes; bool spinning; } slots[MAX_SLOTS];
    bool has_slot_machine;
    vec2 slot_machine_pos;
    int skull_num, slots_stopped;
    int slot_result;          // SLOT_*: the marquee's text
    bool slot_eyes;           // GAME_SlotsEye: the eyes appear
    float blue;               // GAME_SlotsBlue: the blue screen's alpha (0 -> 1 over a second)
    // level.gd switch_tiles and dog_goal.gd dog_tiles: tile layers turned on and off
    int ntiles;
    struct { int group, role, idx, spr; bool on, over; } tiles[MAX_TILES];
    int switch_state, nswitch;
    float switch_pitch;
    int ndoggoals;
    struct { vec2 pos; float r; int group, spr; bool taken; } doggoals[MAX_DOGGOALS];
    // smiley_ring.gd and smiley_coin.gd
    int nrings;
    struct {
        vec2 pos;
        float r, time_given, left, after, color[3];
        int score, spr, grabbed, ncoins;
        bool bell, redoable, running, gone, won;
    } rings[MAX_RINGS];
    int nscoins;
    struct { vec2 pos; float r; int ring, spr, count; bool active; float popup; } scoins[MAX_SCOINS];
    float scoin_pitch;
    int moving_type;          // PMoving.enter(type): 1 keeps the coin multiplier
    int nbodies;
    Body bodies[MAX_BODIES];
    Shape body_shapes[MAX_BODIES];
    vec2 body_pts[MAX_BODIES][4];
    int nkeys;
    Key keys[MAX_KEYS];
    int held[MAX_KEYS], nheld; // player.has_keys, in pick-up order
    vec2 key_rotation_offset;
    int dblock_multiplier;
    bool zone_in[MAX_ZONES];   // overlapping last tick (body_entered/exited)
    int sand_claimer;          // sand_pit.gd player_claimer, -1 = none
    float skull_pitch;         // pitch for this tick's skull death sound

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

    uint64_t events;
    int thought;
} Game;

void game_start(Game *g, const LevelDef *lv, int mode, const GameSettings *set);
void game_tick(Game *g, const GameInput *in);
// Aim line colour as in PIdle.gd (r, g, b, a in 0..1)
void game_aim_color(const Game *g, float out[4]);
