// Behavioral test suite for 1.0 modifiers, special scenes, Endless Shuffle, and Bort Bash.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/game.h"
#include "../source/bort.h"
#include "../source/endless.h"
#include "../source/pthru.h"

static int tests_run = 0;

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } \
} while (0)

static void wait_active(Game *g) {
    GameInput in = {0};
    for (int i = 0; i < 120 && !g->active; i++) game_tick(g, &in);
}

static void make_basic_level(LevelDef *lv) {
    memset(lv, 0, sizeof(*lv));
    snprintf(lv->id, sizeof(lv->id), "T-1");
    lv->bounds = 256;
    lv->par = 3;
    lv->on_time = 10.0f;
    lv->nobjs = 3;
    lv->objs = calloc(3, sizeof(ObjDef));
    lv->objs[0].type = OBJ_PLAYER; lv->objs[0].pos = v2(0, 0); lv->objs[0].sx = lv->objs[0].sy = 1;
    lv->objs[1].type = OBJ_GOAL;   lv->objs[1].pos = v2(180, 0); lv->objs[1].sx = lv->objs[1].sy = 1;
    lv->objs[2].type = OBJ_GHOST;  lv->objs[2].pos = v2(-100, 0); lv->objs[2].sx = lv->objs[2].sy = 1;
}

int main(int argc, char **argv) {
    const char *romfs = argc > 1 ? argv[1] : NULL;
    static Game g;
    LevelDef lv;

    // 1. MOD_EASY removes regular ghosts
    make_basic_level(&lv);
    GameSettings set = {1.0f, false, true, 0, false, MOD_EASY};
    game_start(&g, &lv, MODE_NORMAL, &set);
    CHECK(g.nghosts == 1 && !g.ghosts[0].alive, "MOD_EASY disables regular ghost");
    level_free(&lv);

    // 2. MOD_HARD increases ghost speed/accel vs normal
    make_basic_level(&lv);
    GameSettings set_norm = {1.0f, false, true, 0, false, 0};
    game_start(&g, &lv, MODE_NORMAL, &set_norm);
    wait_active(&g);
    for (int i = 0; i < 20; i++) game_tick(&g, &(GameInput){0});
    float norm_x = g.ghosts[0].pos.x;

    GameSettings set_hard = {1.0f, false, true, 0, false, MOD_HARD};
    game_start(&g, &lv, MODE_NORMAL, &set_hard);
    wait_active(&g);
    for (int i = 0; i < 20; i++) game_tick(&g, &(GameInput){0});
    CHECK(g.ghosts[0].pos.x > norm_x, "MOD_HARD ghost chases faster than normal");
    level_free(&lv);

    // 3. MOD_BURST allows mid-flight re-launch
    make_basic_level(&lv);
    lv.objs[2].pos = v2(-240, -240);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_BURST};
    game_start(&g, &lv, MODE_NORMAL, &set);
    wait_active(&g);
    g.vel = v2(300, 0);
    g.pstate = PS_MOVING;
    g.burst_buffer = 2;
    GameInput bin = {.mouse = v2(0, 0), .click_pressed = true, .click_down = true};
    game_tick(&g, &bin);
    bin = (GameInput){.mouse = v2(-50, 0), .click_released = true};
    game_tick(&g, &bin);
    CHECK((g.events & EV_BURST) && g.vel.x > 0 && g.strokes == 1, "MOD_BURST re-launches mid-flight");
    level_free(&lv);

    // 4. MOD_TELEKINESIS enters PS_TELEKINETIC, steers and counts tk_strokes
    make_basic_level(&lv);
    lv.objs[2].pos = v2(-240, -240);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_TELEKINESIS};
    game_start(&g, &lv, MODE_NORMAL, &set);
    wait_active(&g);
    CHECK(g.pstate == PS_TELEKINETIC, "MOD_TELEKINESIS enters PS_TELEKINETIC");
    for (int i = 0; i < 10; i++)
        game_tick(&g, &(GameInput){.tk_delta = v2(40, 0)});
    CHECK(g.vel.x > 10.0f && g.tk_strokes > 0.0f, "MOD_TELEKINESIS steers velocity and tracks tk_strokes");
    level_free(&lv);

    // 5. MOD_BUDDY spawns buddy and turns into gravestone on ghost hit
    make_basic_level(&lv);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_BUDDY};
    game_start(&g, &lv, MODE_NORMAL, &set);
    wait_active(&g);
    CHECK(g.buddy_on && !g.buddy_dead, "MOD_BUDDY spawns active buddy");
    g.ghosts[0].pos = g.buddy_pos;
    game_tick(&g, &(GameInput){0});
    CHECK(g.buddy_dead && (g.events & EV_BUDDY_DIE), "MOD_BUDDY blocks ghost and dies");
    level_free(&lv);

    // 6. MOD_PARRY stuns ghost and enters PS_PARRY instead of dying
    make_basic_level(&lv);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_PARRY};
    game_start(&g, &lv, MODE_NORMAL, &set);
    wait_active(&g);
    g.ghosts[0].pos = g.pos;
    game_tick(&g, &(GameInput){.parry_pressed = true});
    CHECK(!g.lose && g.pstate == PS_PARRY && g.ghosts[0].parry_stun > 0, "MOD_PARRY parries ghost");
    level_free(&lv);

    // 7. MOD_JUMPY triggers PS_JUMP after rolling 1024 units
    make_basic_level(&lv);
    lv.flags = LF_NEVER_OB;
    lv.objs[1].pos = v2(0, -240);
    lv.objs[2].pos = v2(-240, -240);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_JUMPY};
    game_start(&g, &lv, MODE_NORMAL, &set);
    wait_active(&g);
    g.pstate = PS_MOVING;
    g.vel = v2(1200, 0);
    g.jumpy_last_pos = g.pos;
    bool jumped = false;
    for (int i = 0; i < 200; i++) {
        if (g.pstate == PS_IDLE) { g.pstate = PS_MOVING; g.vel = v2(-1200, 0); }
        game_tick(&g, &(GameInput){0});
        if (g.pstate == PS_JUMP) { jumped = true; break; }
    }
    CHECK(jumped, "MOD_JUMPY triggers PS_JUMP after rolling 1024 units");
    level_free(&lv);

    // 8. MOD_DIZZY rotates velocity and sprite_rot
    make_basic_level(&lv);
    lv.objs[2].pos = v2(-240, -240);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_DIZZY};
    game_start(&g, &lv, MODE_NORMAL, &set);
    wait_active(&g);
    g.pstate = PS_MOVING;
    g.vel = v2(300, 0);
    game_tick(&g, &(GameInput){0});
    CHECK(fabsf(g.vel.y) > 0.1f && fabsf(g.sprite_rot) > 0.01f, "MOD_DIZZY curves trajectory");
    level_free(&lv);

    // 9. MOD_MISSILES explodes at 240f and kills player if in radius
    make_basic_level(&lv);
    lv.objs[2].pos = v2(-240, -240);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_MISSILES};
    game_start(&g, &lv, MODE_NORMAL, &set);
    wait_active(&g);
    for (int i = 0; i < 245 && !g.lose; i++) game_tick(&g, &(GameInput){0});
    CHECK(g.lose && g.death_type == DEATH_WALL, "MOD_MISSILES explodes and kills stationary player");
    level_free(&lv);

    // 10. MOD_STICKY stops dead on wall hit
    make_basic_level(&lv);
    lv.objs[2].pos = v2(-240, -240);
    lv.nshapes = 1;
    lv.shapes = calloc(1, sizeof(Shape));
    lv.shapes[0].kind = SHAPE_POLY;
    lv.shapes[0].layers = lv.shapes[0].base = 1;
    lv.shapes[0].n = 4;
    lv.shapes[0].pts = malloc(4 * sizeof(vec2));
    lv.shapes[0].pts[0] = v2(40, -100); lv.shapes[0].pts[1] = v2(80, -100);
    lv.shapes[0].pts[2] = v2(80, 100);  lv.shapes[0].pts[3] = v2(40, 100);
    shape_update_bbox(&lv.shapes[0]);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_STICKY};
    game_start(&g, &lv, MODE_NORMAL, &set);
    wait_active(&g);
    g.pstate = PS_MOVING;
    g.vel = v2(600, 0);
    for (int i = 0; i < 10; i++) game_tick(&g, &(GameInput){0});
    CHECK(v2len(g.vel) < 1.0f, "MOD_STICKY stops cat on wall collision");
    level_free(&lv);

    // 11. MOD_GOD uncapped drag & teleport
    make_basic_level(&lv);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_GOD};
    game_start(&g, &lv, MODE_NORMAL, &set);
    wait_active(&g);
    game_tick(&g, &(GameInput){.mouse = v2(123, -45), .teleport_pressed = true});
    CHECK(fabsf(g.pos.x - 123.0f) < 0.1f && fabsf(g.pos.y + 45.0f) < 0.1f, "MOD_GOD teleports");
    level_free(&lv);

    // 12. MOD_E_PHARAOH / MOD_E_BEES / MOD_E_BORT
    make_basic_level(&lv);
    lv.objs[2].pos = v2(-240, -240);
    set = (GameSettings){1.0f, false, true, 0, false, MOD_E_PHARAOH | MOD_E_BEES | MOD_E_BORT};
    game_start(&g, &lv, MODE_ENDLESS, &set);
    wait_active(&g);
    CHECK(g.nbees == 20 && g.neborts == 25, "Endless mods spawn 20 bees and 25 Borts");
    g.vel = v2(200, 0);
    game_tick(&g, &(GameInput){0});
    CHECK(g.vel.x < 175.0f, "MOD_E_PHARAOH applies sand friction");
    level_free(&lv);

    // 13. Endless Shuffle controller
    EndlessRun er;
    int wc[8] = {5, 18, 18, 18, 18, 18, 18, 10};
    endless_start(&er, wc, 42);
    CHECK(er.total_time == 61.0f && er.skips == 3 && endless_current(&er).world == 7 && endless_current(&er).level == 0,
          "Endless starts on E_00 with 61s and 3 skips");
    endless_switch(&er, 0, 200000, 5.0f, v2(100, 100));
    CHECK(er.cleared == 1 && er.skips == 4 && er.total_time > 65.0f, "Endless win adds time and awards skip at 200k");
    for (int i = 0; i < 4; i++) endless_switch(&er, 1, 0, 0, v2(100, 100));
    CHECK(er.demon_active, "4 resets drain demon meter and spawn Red Demon");
    float bt = er.total_time;
    endless_tick(&er, er.demon_pos, 1.0f / 60.0f);
    CHECK((er.events & EEV_DEMON_HIT) && er.total_time < bt - 4.0f, "Demon hit subtracts 5s");

    // 14. Bort Bash scoring
    BortGame bg;
    bort_start(&bg, 123);
    bort_go(&bg);
    CHECK(bg.mode == BORT_PLAYING && bg.left == 5, "Bort Bash round 1 starts with 5 Borts");
    for (int i = 0; i < BORT_MAX && bg.left > 0; i++)
        if (bg.b[i].on && !bg.b[i].bashed) bort_tap(&bg, bg.b[i].x, bg.b[i].y);
    CHECK(bg.left == 0 && bg.score >= 250, "Bort Bash bashing all 5 awards score");

    // 15. Special converted levels (if romfs provided)
    if (romfs) {
        char p[512];
        snprintf(p, sizeof(p), "%s/levels/w3/3_18.txt", romfs);
        if (level_load(&lv, p)) {
            set = (GameSettings){1.0f, false, true, 0, false, MOD_HARD};
            game_start(&g, &lv, MODE_NORMAL, &set);
            CHECK(g.coaster_on && g.coaster_target == 50, "3-18 coaster loads and MOD_HARD doubles target to 50");
            level_free(&lv);
        }
        snprintf(p, sizeof(p), "%s/levels/w5/5_16.txt", romfs);
        if (level_load(&lv, p)) {
            set = (GameSettings){1.0f, false, true, 0, false, 0};
            game_start(&g, &lv, MODE_NORMAL, &set);
            CHECK(g.wizard_on && g.wizard_hp == 4, "5-16 wizard ghost loads with 4 HP");
            level_free(&lv);
        }
        snprintf(p, sizeof(p), "%s/levels/w5/5_18.txt", romfs);
        if (level_load(&lv, p)) {
            CHECK((lv.flags & LF_GOLDEN_TEE) != 0, "5-18 exports LF_GOLDEN_TEE");
            level_free(&lv);
        }
        snprintf(p, sizeof(p), "%s/levels/w6/P_15.txt", romfs);
        if (level_load(&lv, p)) {
            CHECK((lv.flags & LF_SANTA_SCROLL) != 0, "P-15 exports LF_SANTA_SCROLL");
            set = (GameSettings){1.0f, false, true, 0, true, 0};
            game_start(&g, &lv, MODE_NORMAL, &set);
            wait_active(&g);
            g.pos.x = -260.0f;
            game_tick(&g, &(GameInput){0});
            CHECK(g.lose && g.death_type == DEATH_OB, "P-15 triggers OB when pos.x < -256");
            level_free(&lv);
        }
    }

    printf("modtest: all %d checks passed!\n", tests_run);
    return 0;
}
