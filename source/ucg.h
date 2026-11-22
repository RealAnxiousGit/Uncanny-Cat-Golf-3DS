// Uncanny Cat Golf 3DS: shared types for the game code.
// game.c, level.c and physics.c build for both the 3DS and the PC checker,
// so nothing in them may touch 3DS libraries.
#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct { float x, y; } vec2;

static inline vec2 v2(float x, float y) { vec2 r = {x, y}; return r; }
static inline vec2 v2add(vec2 a, vec2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline vec2 v2sub(vec2 a, vec2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline vec2 v2mul(vec2 a, float s) { return v2(a.x * s, a.y * s); }
static inline float v2dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
static inline float v2len2(vec2 a) { return a.x * a.x + a.y * a.y; }
static inline float v2len(vec2 a) { return sqrtf(v2len2(a)); }
static inline vec2 v2norm(vec2 a) {
    float l = v2len(a);
    return l > 0 ? v2(a.x / l, a.y / l) : v2(0, 0);
}
static inline vec2 v2limit(vec2 a, float m) {
    float l = v2len(a);
    return (l > m && l > 0) ? v2mul(a, m / l) : a;
}
// Godot's Vector2.bounce(n): -reflect(n)
static inline vec2 v2bounce(vec2 v, vec2 n) { return v2sub(v, v2mul(n, 2.0f * v2dot(v, n))); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------- level data
enum { SHAPE_POLY, SHAPE_CIRCLE };
typedef struct {
    uint8_t kind;
    uint8_t group;   // a switchable tile layer's shapes (0: always there)
    uint32_t layers; // 0 while switched off
    uint32_t base;   // its layers when on
    int n;          // polygon points
    vec2 *pts;
    vec2 c;         // circle centre
    float r;
    float x0, y0, x1, y1; // bounding box
} Shape;

enum {
    OBJ_PLAYER, OBJ_GOAL, OBJ_GHOST, OBJ_COIN, OBJ_DBLOCK, OBJ_KEYDOOR, OBJ_KEY, OBJ_SKULL,
    OBJ_TOKEN, OBJ_JUMPPAD, OBJ_SPAWNER, OBJ_CAR, OBJ_POLICE, OBJ_SLOT, OBJ_SLOTMACHINE,
    OBJ_SWITCH, OBJ_DOG, OBJ_DOGGOAL, OBJ_RING, OBJ_SCOIN, OBJ_TILES, OBJ_LIGHTS, OBJ_ASHBABY,
    OBJ_PORTAL, OBJ_NUKE, OBJ_FAKEGOAL, OBJ_FGHOST, OBJ_PRISMGATE,
    OBJ_HORSE, OBJ_HGOAL, OBJ_MINE, OBJ_BOSSAPPEAR, OBJ_METRONOME,
    OBJ_COASTER, OBJ_WIZARD, OBJ_COUNT
};

#define MAX_PARAMS 24
typedef struct {
    int type;
    vec2 pos;
    float rot, sx, sy;
    bool hidden;
    int nparams;
    char pkey[MAX_PARAMS][24];
    float pval[MAX_PARAMS];
} ObjDef;

// Area zones (outline only; their art is in the floor picture)
enum { ZONE_SAND = 1, ZONE_WATER = 2, ZONE_POLICE = 3, ZONE_LIGHTS = 4, ZONE_ASHBABY = 5, ZONE_BOSSAPPEAR = 6,
       ZONE_COASTER = 7 };

// Pictures of dynamic objects in <id>_d.t3x
typedef struct { int w, h; float ax, ay; } DynSprite;
#define MAX_DSPR 64

enum {
    LF_ALWAYS_OB = 1, LF_NEVER_OB = 2, LF_MIRROR = 4, LF_HIDE_SCORING = 8, LF_UNSKIPPABLE = 16,
    LF_FINAL_GHOST = 32, // level.gd final_ghost_level: the Thinker is hidden
    LF_GOLDEN_TEE = 64,  // 5-18 golden_tee.gd: results_no_progress + golden tee drop
    LF_SANTA_SCROLL = 128, // P-15 santa.gd: upward vertical camera scroll + left/bottom OB
    LF_METRONOME = 256,  // level.gd metronome_level
};

// an animation that moves the game (levels.py level_tracks): wind on the cat, a goal on a path
enum { TRACK_WIND = 1, TRACK_GOAL = 2 };
#define MAX_TRACKS 4
typedef struct {
    int kind, n;
    bool loop, discrete;
    float length;
    float *t, *x, *y;
} Track2;

typedef struct {
    char id[16];
    char author[48];
    int bounds;
    int par;
    float on_time;
    int ranks[6];
    int music;
    float shade[4];
    uint32_t flags;
    int image_size;
    bool has_over;
    int nshapes;
    Shape *shapes;
    int nobjs;
    ObjDef *objs;
    int nzones;
    Shape *zones; // layers holds the ZONE_ kind
    int ndspr;
    DynSprite dspr[MAX_DSPR];
    int ntracks;
    Track2 tracks[MAX_TRACKS];
} LevelDef;

bool level_load(LevelDef *lv, const char *path);
void level_free(LevelDef *lv);
float obj_param(const ObjDef *o, const char *key, float def);

// ---------------------------------------------------------------- physics
typedef struct {
    bool hit;
    vec2 normal;
    int shape;
    float travel_frac;
} Collision;

// Moving and removable bodies (blocks, doors, skulls) checked along with the level's
// shapes. Their indices follow the level's: shape lv->nshapes + i is dynamic shape i.
// A dynamic shape with layers 0 is switched off. exclude skips one shape (a body's own).
void phys_set_dynamic(Shape *shapes, int n);
void phys_set_exclude(int shape_index);
void shape_update_bbox(Shape *s);
// true if a circle overlaps the shape (Area2D overlap)
bool phys_circle_overlaps(const Shape *s, vec2 p, float r);
// Godot-style move_and_collide for a circle. Moves *pos, returns contact info.
Collision phys_move_circle(const LevelDef *lv, vec2 *pos, float r, vec2 motion, uint32_t mask);
// true if the point is inside any shape on the given layers
bool phys_point_solid(const LevelDef *lv, vec2 p, uint32_t mask);
