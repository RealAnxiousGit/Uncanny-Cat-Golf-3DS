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

enum { PS_SPAWN, PS_IDLE, PS_MOVING, PS_WIN, PS_LOSE, PS_ATTRACT, PS_JUMP,
       PS_CUSTOM,   // PCustom: held still, hitbox off (the horse's defeat)
       PS_WINFAKE,  // PWinFake: 5-3's fake stage clear
       PS_PARRY,    // PParry: parry freeze-frame on ghost contact
       PS_TELEKINETIC }; // PTelekinetic: telekinesis steering mode
enum { DEATH_NORMAL, DEATH_OB, DEATH_BACKROOMS, DEATH_WALL, DEATH_HORSE, DEATH_WATER };
enum { MODE_NORMAL, MODE_ENDLESS, MODE_DEMO };
enum { RANK_UNCANNY, RANK_ATROCIOUS, RANK_STINKY, RANK_OK, RANK_SWAG, RANK_PEAK };
enum { PROG_NONE, PROG_RESET, PROG_NORMAL, PROG_SKIP, PROG_WIN_RESET, PROG_GOLDEN_TEE };

// Playthrough & Endless modifiers (modifiers.gd)
enum {
    MOD_EASY        = 1u << 0,
    MOD_HARD        = 1u << 1,
    MOD_BURST       = 1u << 2,
    MOD_TELEKINESIS = 1u << 3,
    MOD_BUDDY       = 1u << 4,
    MOD_PARRY       = 1u << 5,
    MOD_JUMPY       = 1u << 6,
    MOD_DIZZY       = 1u << 7,
    MOD_MISSILES    = 1u << 8,
    MOD_STICKY      = 1u << 9,
    MOD_GOD         = 1u << 10,
    MOD_E_PHARAOH   = 1u << 11,
    MOD_E_BORT      = 1u << 12,
    MOD_E_BEES      = 1u << 13,
    MOD_E_OVERHEAT  = 1u << 14,
};

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
#define EV_LIGHTS (1ull << 34)
#define EV_ASHBABY (1ull << 35)
// World 4
#define EV_PORTAL (1ull << 36)
#define EV_NUKE (1ull << 37)
#define EV_FAKEGOAL (1ull << 38)
#define EV_FAKEALT (1ull << 39)   // the fake goal's own sounds_to_play
#define EV_FGHOST (1ull << 40)    // final_ghost.gd sfx_activate
#define EV_SHATTER (1ull << 41)   // prism_gate.gd
#define EV_TOOPOOR (1ull << 42)
// World 5: horse_boss.gd, horse_goal.gd, land_mine.gd
#define EV_HCHARGEUP (1ull << 43)
#define EV_HCHARGE (1ull << 44)
#define EV_HBOUNCE (1ull << 45)
#define EV_HHURT (1ull << 46)
#define EV_HBOOM (1ull << 47)     // horse_explosion_fx (resultsBang)
#define EV_HBOOMQ (1ull << 48)    // the same, quiet
#define EV_HLAUNCH (1ull << 49)
#define EV_HDIE (1ull << 50)
#define EV_MINE (1ull << 51)
#define EV_MUTE (1ull << 52)      // the music stops
#define EV_METRO_BEAT (1ull << 53) // metronome.gd _beat_sound (pitch metro_beat_pitch)
#define EV_METRO_TRIG (1ull << 54) // a metronome's trigger sound (metro_trig: which ones)
// Modifiers & W3-W6 special events
#define EV_BURST        (1ull << 55)
#define EV_PARRY        (1ull << 56)
#define EV_BUDDY_DIE    (1ull << 57)
#define EV_MISSILE_DING (1ull << 58)
#define EV_MISSILE_BOOM (1ull << 59)
#define EV_LAUNCH_DIZZY (1ull << 60)
#define EV_MAGIC        (1ull << 61)
#define EV_GOLDEN_TEE   (1ull << 62)
#define EV_COASTER_START (1ull << 63)

// Thought animations the top screen should show (thought_displayer categories)
enum { TH_NONE, TH_IDLE, TH_MOVING, TH_WIN, TH_DEATH, TH_BACKROOMS, TH_MONEY, TH_SAND, TH_SKULL, TH_CAR, TH_JUMP, TH_POLICE,
       TH_CHUCKLE, TH_DOG, TH_LIGHTS, TH_PORTAL, TH_HORSE, TH_METRONOME, TH_SWIM };

typedef struct {
    // positions are in the original's viewport pixels: the course is 0..512 on both axes
    vec2 mouse;
    vec2 tk_delta;       // telekinesis steering delta (stylus delta or Circle Pad)
    bool click_pressed, click_down, click_released;
    bool parry_pressed, teleport_pressed;
    bool restart_pressed, skip_down, pause_pressed;
    int beat;            // the music's beat number (metronome.gd), -1 if unknown
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
#define MAX_PORTALS 8
#define MAX_HGOALS 8
#define MAX_METROS 4
#define MAX_MINES 16
enum { HORSE_NORMAL, HORSE_LOSS, HORSE_WIN, HORSE_DEFEAT };
#define MAX_FAKEGOALS 16
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
    int prisms;        // prism_gate.gd prism_number
    bool passable, touch; // enough prisms (no collision); the cat in its break hitbox last tick
    float bulge;       // the lock's "too poor" bulge (1 -> 0)
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
    int total_prisms; // the playthrough's prisms (pthru total_prisms: the best ranks added up)
    bool can_swim;    // pthru can_swim: the golden tee at 5-18 gives it, so every P level has it
    uint32_t mods;    // active MOD_* bitmask
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
    // final_ghost.gd: a ghost that waits (inactive) until a fake goal wakes it, at its node's scale
    struct { vec2 pos, vel; bool alive, graze, inactive, final; float scale, hit;
             int met, mslot, parry_stun; bool frozen; } ghosts[MAX_GHOSTS]; // metronome.gd: frozen and see-through when off
    bool has_goal;
    vec2 goal_pos;
    float goal_r, goal_ry;
    int ncoins;
    // coins, and World 2's speed / stop tokens (collectible.gd with does_speed; spr >= 0)
    struct { vec2 pos; float r; bool taken; int score, mult, multval; float popup; bool does_speed; float set_speed; int spr;
             bool nuke, nuke_spawners; } coins[MAX_COINS];
    float nuke_flash;         // nuke.gd NukeExplosions: a flash over the course (1 -> 0)
    // portal.gd (and player.gd portal_sick)
    int nportals;
    struct { vec2 pos, half; int target, spr; bool in; } portals[MAX_PORTALS];
    bool portal_sick;
    int portal_sick_timer;
    float portal_rot;         // graphic.rotation += 0.1 a frame (every linked portal turns alike)
    // fake_goal.gd
    int nfakegoals;
    struct { vec2 pos; float r; int score, spr, spr2, group; bool ghost, alt, taken; float popup; } fakegoals[MAX_FAKEGOALS];
    bool alt_floor;           // the fake goal's objects_to_show and new_color (<id>_u2)
    // horse_boss.gd (boss_level.gd: no goal; the level is won when the beaten horse flies off the top)
    struct {
        bool on;
        vec2 pos, vel, dir;
        float scale, base_speed, sx, sy, sat, shaker, fade;
        int health, start_health, attack_time, attack_timer, target_delay, mode, spr, defeat_t;
        bool targetted, end_of_life;
    } horse;
    int nhgoals; // horse_goal.gd
    struct { vec2 pos, vel; float r, scale, rot, sx, sy; int next, spr; bool active, charging, gone, pin, hin; } hgoals[MAX_HGOALS];
    int nmines;  // land_mine.gd
    struct { vec2 pos; float r; int spr; bool spent, hin; float boom; } mines[MAX_MINES];
    // metronome.gd
    int nmetros;
    struct { int m, every, n, cur, counted, last, snd; float vol, pitch; bool visual, tile, started, swing; } metros[MAX_METROS];
    unsigned metro_trig;
    float metro_beat_pitch;
    float anim_t;             // the level's autoplaying animations (lv.tracks), from the start
    int wind_key;             // the wind key last applied (a discrete track sets the value once per key)
    vec2 winfake_target;      // boss_appear_event.gd: where PWinFake drifts to
    int winfake_count;
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
    // lights_out_event.gd / ash_baby_event.gd (CustomEvent areas: once each)
    bool lights_out, lights_done, lights_goal, ash_done;
    int lights_score, ash_score;
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

    // modifiers (player.gd, PMoving.gd, PParry.gd, PTelekinetic.gd, little_buddy.gd, missile_attacker.gd, bee.gd)
    float tk_strokes, tk_pitch;
    int parry_active, parry_timer;
    vec2 parry_stored_vel;
    float jumpy_dist, sprite_rot;
    vec2 jumpy_last_pos, last_touch_world;
    bool buddy_on, buddy_dead;
    vec2 buddy_pos;
    float buddy_scale, buddy_angle;
    bool missile_on;
    vec2 missile_pos;
    int missile_timer, missile_hitbox_t;
    int nbees;
    struct { vec2 pos, vel; float accel, limit; } bees[20];
    int neborts;
    struct { vec2 pos, vel; int hue; } eborts[25];

    // 3-18 coaster, 5-16 wizard, 5-18 golden tee, P-15 santa scroll
    bool coaster_on, coaster_started, coaster_done;
    int coaster_coins, coaster_target, coaster_timer, coaster_step;
    float coaster_speed, coaster_spawn_x, coaster_screen_edge, coaster_kill_x;
    float coaster_slot_y[4];
    struct { vec2 pos; bool active, is_ghost; int slot; } coaster_objs[24];
    bool wizard_on, wizard_wind_on;
    vec2 wizard_pos;
    int wizard_spr, wizard_hp, wizard_cast_t;
    bool golden_tee_dropped, golden_tee_anim;
    int golden_tee_timer;
    float santa_cam_y, santa_min_y;
    bool mirror_active; // runtime mirror (lv.flags & LF_MIRROR or 5-16 wizard spell)

    uint64_t events;
    int thought;
} Game;

void game_start(Game *g, const LevelDef *lv, int mode, const GameSettings *set);
void game_tick(Game *g, const GameInput *in);
// Aim line colour as in PIdle.gd (r, g, b, a in 0..1)
void game_aim_color(const Game *g, float out[4]);
