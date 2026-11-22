#include "villa.h"
#include "data.h"
#include "pthru.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CostumeDef g_costumes[MAX_COSTUMES];
int g_ncostumes;
SoundTestTrack g_songs[MAX_SONGS];
int g_nsongs;

void villa_load_catalogs(void) {
    g_ncostumes = 0;
    FILE *f = fopen(dp("gfx/costumes.txt"), "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f) && g_ncostumes < MAX_COSTUMES) {
            line[strcspn(line, "\r\n")] = 0;
            CostumeDef *c = &g_costumes[g_ncostumes];
            memset(c, 0, sizeof(*c));
            char *p = line;
            char *t1 = strchr(p, '\t'); if (!t1) continue; *t1++ = 0;
            char *t2 = strchr(t1, '\t'); if (!t2) continue; *t2++ = 0;
            char *t3 = strchr(t2, '\t'); if (!t3) continue; *t3++ = 0;
            char *t4 = strchr(t3, '\t'); if (!t4) continue; *t4++ = 0;
            char *t5 = strchr(t4, '\t'); if (!t5) continue; *t5++ = 0;
            snprintf(c->id, sizeof(c->id), "%s", p);
            snprintf(c->name_key, sizeof(c->name_key), "%s", t1);
            snprintf(c->spr_tag, sizeof(c->spr_tag), "%s", t2);
            snprintf(c->ghost_tag, sizeof(c->ghost_tag), "%s", t3);
            snprintf(c->sfx, sizeof(c->sfx), "%s", t4);
            snprintf(c->unlock_key, sizeof(c->unlock_key), "%s", t5);
            g_ncostumes++;
        }
        fclose(f);
    }

    g_nsongs = 0;
    f = fopen(dp("music/soundtest.txt"), "r");
    if (f) {
        char line[320];
        while (fgets(line, sizeof(line), f) && g_nsongs < MAX_SONGS) {
            line[strcspn(line, "\r\n")] = 0;
            SoundTestTrack *s = &g_songs[g_nsongs];
            memset(s, 0, sizeof(*s));
            char *cols[9] = {0};
            char *p = line;
            int nc = 0;
            while (p && nc < 9) {
                cols[nc++] = p;
                char *tb = strchr(p, '\t');
                if (tb) *tb = 0, p = tb + 1;
                else p = NULL;
            }
            if (nc < 9) continue;
            snprintf(s->file, sizeof(s->file), "%s", cols[0]);
            snprintf(s->name, sizeof(s->name), "%s", cols[1]);
            snprintf(s->desc_key, sizeof(s->desc_key), "%s", cols[2]);
            snprintf(s->unlock_key, sizeof(s->unlock_key), "%s", cols[3]);
            s->loop = atoi(cols[4]) != 0;
            s->loop_offset = strtof(cols[5], NULL);
            s->r = strtof(cols[6], NULL);
            s->g = strtof(cols[7], NULL);
            s->b = strtof(cols[8], NULL);
            g_nsongs++;
        }
        fclose(f);
    }
}

const CostumeDef *villa_cur_costume(void) {
    for (int i = 0; i < g_ncostumes; i++)
        if (!strcmp(g_costumes[i].id, g_save.current_costume)) return &g_costumes[i];
    return g_ncostumes > 0 ? &g_costumes[0] : NULL;
}

bool villa_costume_unlocked(int idx) {
    if (idx < 0 || idx >= g_ncostumes) return false;
    int ul = pthru_unlock_by_key(g_costumes[idx].unlock_key);
    return ul < 0 || g_save.unlocks[ul];
}

bool villa_song_unlocked(int idx) {
    if (idx < 0 || idx >= g_nsongs) return false;
    int ul = pthru_unlock_by_key(g_songs[idx].unlock_key);
    return ul < 0 || g_save.unlocks[ul];
}

void phone_reset(PhoneState *p) {
    memset(p, 0, sizeof(*p));
}

void phone_press(PhoneState *p, char digit) {
    if (p->phase != PHONE_IDLE) return;
    size_t len = strlen(p->number);
    if (len < 10) {
        p->number[len] = digit;
        p->number[len + 1] = 0;
    }
}

void phone_del(PhoneState *p) {
    if (p->phase != PHONE_IDLE) return;
    size_t len = strlen(p->number);
    if (len > 0) p->number[len - 1] = 0;
}

void phone_call(PhoneState *p) {
    if (p->phase != PHONE_IDLE || !p->number[0]) return;
    const char *n = p->number;
    const char *k = "N/A";
    if (!strcmp(n, "67")) k = "Unfunny";
    else if (!strcmp(n, "1312") || !strcmp(n, "9653")) k = "Pronouns";
    else if (!strcmp(n, "1225574663")) k = "First";
    else if (!strcmp(n, "22745453")) k = "Zinglo";
    else if (!strcmp(n, "2787827693")) k = "Waltz";
    else if (!strcmp(n, "5535374268")) k = "ElephantASMR";
    else if (!strcmp(n, "3862243287")) k = "Cheater";
    else if (!strcmp(n, "7737334263")) k = "Cracktro";
    else if (!strcmp(n, "20240328") || !strcmp(n, "28032024") || !strcmp(n, "03282024")) k = "BetaAniversary";
    else if (!strcmp(n, "2083")) k = "Revo83";
    else if (!strcmp(n, "19800522") || !strcmp(n, "22051980") || !strcmp(n, "05221980")) k = "EerieFeline";
    else if (!strcmp(n, "1234567890")) k = "New";
    else if (!strcmp(n, "824") || !strcmp(n, "2284653") || !strcmp(n, "8622669")) k = "UCG";
    else if (!strcmp(n, "58259777")) k = "Winner";
    else if (!strcmp(n, "1847463757")) k = "Tipperson";
    else if (!strcmp(n, "5512251255")) k = "Beverly";
    else if (!strcmp(n, "1993984628")) k = "Meowls";
    else if (!strcmp(n, "18009222")) k = "Flintstone";
    else if (!strcmp(n, "09091996")) k = "Bandicoot";
    else if (!strcmp(n, "9605")) k = "Radio";
    else if (!strcmp(n, "876862371")) k = "Congrats";
    else if (!strcmp(n, "47346455")) k = "Friend";

    snprintf(p->call_key, sizeof(p->call_key), "%s", k);
    p->progress = 0;
    p->phase = PHONE_CALLING;
    p->timer = 180 + (rand() % 120);
    p->has_audio = false;
    p->quit_after = false;
    p->dialogue[0] = 0;
}

static void phone_load_line(PhoneState *p, const char *(*tr_fn)(const char *)) {
    char key[48];
    if (!strcmp(p->call_key, "Friend"))
        snprintf(key, sizeof(key), "EXTRA_Phone%dFriend%d", g_save.quest_progress, p->progress);
    else
        snprintf(key, sizeof(key), "EXTRA_Phone%s%d", p->call_key, p->progress);
    const char *txt = tr_fn ? tr_fn(key) : key;
    if (!txt || !strcmp(txt, key)) {
        p->dialogue[0] = 0;
    } else {
        snprintf(p->dialogue, sizeof(p->dialogue), "%s", txt);
    }
}

bool phone_tick(PhoneState *p, const char *(*tr_fn)(const char *)) {
    if (p->phase != PHONE_CALLING) return false;
    if (p->timer > 0) {
        p->timer--;
        return false;
    }
    p->phase = PHONE_IN_CALL;
    const char *k = p->call_key;
    p->has_audio = true;
    if (!strcmp(k, "ElephantASMR")) snprintf(p->audio_rel, sizeof(p->audio_rel), "phone/pmusElephantASMR.ogg");
    else if (!strcmp(k, "Waltz")) snprintf(p->audio_rel, sizeof(p->audio_rel), "phone/pmusWaltz.ogg");
    else if (!strcmp(k, "BetaAniversary")) snprintf(p->audio_rel, sizeof(p->audio_rel), "phone/pmusPlaytest2.ogg");
    else if (!strcmp(k, "Revo83")) snprintf(p->audio_rel, sizeof(p->audio_rel), "phone/pmusMsx.ogg");
    else if (!strcmp(k, "EerieFeline")) snprintf(p->audio_rel, sizeof(p->audio_rel), "phone/pmusEerieFelineLabyrinth.ogg");
    else if (!strcmp(k, "Beverly")) snprintf(p->audio_rel, sizeof(p->audio_rel), "phone/pmusBeverlyJones.ogg");
    else if (!strcmp(k, "Radio")) snprintf(p->audio_rel, sizeof(p->audio_rel), "../sfx/menu/radiocanny.ogg");
    else if (!strcmp(k, "Flintstone")) snprintf(p->audio_rel, sizeof(p->audio_rel), "../sfx/menu/phone_flintstone.ogg");
    else if (!strcmp(k, "Bandicoot")) snprintf(p->audio_rel, sizeof(p->audio_rel), "../sfx/menu/phone_bandicoot.ogg");
    else if (!strcmp(k, "Pronouns")) snprintf(p->audio_rel, sizeof(p->audio_rel), "../sfx/gimmicks/pronouns.ogg");
    else if (!strcmp(k, "Unfunny")) {
        snprintf(p->audio_rel, sizeof(p->audio_rel), "../sfx/game/veryloud.ogg");
        p->quit_after = true;
    } else if (!strcmp(k, "Cheater")) {
        p->has_audio = false;
        for (int i = 0; i < UL_COUNT; i++) pthru_award(i);
        pthru_flush();
        snprintf(p->dialogue, sizeof(p->dialogue), "All unlocks awarded!");
    } else if (!strcmp(k, "Tipperson")) {
        p->has_audio = false;
        snprintf(p->dialogue, sizeof(p->dialogue), "Tip: Hold X to skip, L to toggle zoom, R to parry!");
    } else {
        p->has_audio = false;
        phone_load_line(p, tr_fn);
        if (!p->dialogue[0]) snprintf(p->dialogue, sizeof(p->dialogue), "...nobody answered.");
    }
    return true;
}

void phone_next(PhoneState *p, const char *(*tr_fn)(const char *)) {
    if (p->phase != PHONE_IN_CALL) return;
    p->progress++;
    if (!p->has_audio) {
        phone_load_line(p, tr_fn);
        if (p->dialogue[0]) return;
    }
    phone_reset(p);
}
