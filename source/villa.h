// Summer Villa extras: Costume House, Sound Test, Mystery Phone, Interactive Credits.
#pragma once
#include <stdbool.h>

#define MAX_COSTUMES 32
#define MAX_SONGS 64

typedef struct {
    char id[24];
    char name_key[40];
    char spr_tag[32];
    char ghost_tag[32];
    char sfx[64];
    char unlock_key[32];
} CostumeDef;

typedef struct {
    char file[80];
    char name[48];
    char desc_key[32];
    char unlock_key[32];
    bool loop;
    float loop_offset;
    float r, g, b;
} SoundTestTrack;

enum { PHONE_IDLE = 0, PHONE_CALLING = 1, PHONE_IN_CALL = 2 };

typedef struct {
    char number[16];
    char call_key[24];
    int phase;
    int timer;
    int progress;
    char dialogue[256];
    char audio_rel[80];
    bool has_audio;
    bool quit_after;
} PhoneState;

extern CostumeDef g_costumes[MAX_COSTUMES];
extern int g_ncostumes;
extern SoundTestTrack g_songs[MAX_SONGS];
extern int g_nsongs;

void villa_load_catalogs(void);
const CostumeDef *villa_cur_costume(void);
bool villa_costume_unlocked(int idx);
bool villa_song_unlocked(int idx);

void phone_reset(PhoneState *p);
void phone_press(PhoneState *p, char digit);
void phone_del(PhoneState *p);
void phone_call(PhoneState *p);
// returns true if a call just connected (caller plays p->audio_rel or shows p->dialogue)
bool phone_tick(PhoneState *p, const char *(*tr_fn)(const char *));
void phone_next(PhoneState *p, const char *(*tr_fn)(const char *));
