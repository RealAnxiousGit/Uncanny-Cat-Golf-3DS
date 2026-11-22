// 1.0 UI screens (Title, Main Menu, Playthroughs, Endless Over, Results, Summer Villa).
// Included by main.c after draw_hud, draw_top_bg, draw_displayer, button, text_ol, BLUE, etc.

static const char *player_spr(void) {
    const CostumeDef *c = villa_cur_costume();
    return (c && spr_find(c->spr_tag) >= 0) ? c->spr_tag : "costume_canny";
}

static const char *ghost_spr(void) {
    const CostumeDef *c = villa_cur_costume();
    return (c && spr_find(c->ghost_tag) >= 0) ? c->ghost_tag : "costume_uncanny";
}

static void play_jumpscare_sfx(void) {
    const CostumeDef *c = villa_cur_costume();
    sfx_play((c && c->sfx[0]) ? c->sfx : "ghost/defaultJumpscare.ogg", 1, 1);
}

static void draw_unlock_toast(void) {
    static int toast_ul = -1, toast_t = 0;
    if (toast_t <= 0) {
        toast_ul = pthru_pop_toast();
        if (toast_ul >= 0) {
            toast_t = 180;
            sfx_play("player/notif.ogg", 1, 1);
        }
    }
    if (toast_t > 0 && toast_ul >= 0) {
        toast_t--;
        char msg[80];
        snprintf(msg, sizeof(msg), "Unlocked: %s!", pthru_unlock_label(toast_ul));
        ui_z = 0.98f;
        C2D_DrawRectSolid(40, 4, ui_z, 320, 22, C2D_Color32(20, 20, 40, 220));
        ui_z = 0.99f;
        text_ol(200, 7, 0.44f, C2D_Color32(255, 240, 100, 255), C2D_AlignCenter, msg);
    }
}

// ---------------------------------------------------------------- Title & Attract
static const char *const SPLASHES[] = {
    "3DS Edition!", "Now on two screens!", "Stylus golf!", "100% C port!", "Press L to zoom!"
};

static void draw_title_bg(float w) {
    int gi = spr_find("title_grid"), hi = spr_find("title_hills");
    if (gi >= 0 && *sprinfo[gi].sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(*sprinfo[gi].sheet, sprinfo[gi].idx);
        C2D_DrawImageAt(img, 0, 0, 0.1f, NULL, w / img.subtex->width, (float)SCR_H / img.subtex->height);
    } else {
        C2D_DrawRectSolid(0, 0, 0.1f, w, SCR_H, C2D_Color32(60, 140, 220, 255));
    }
    if (hi >= 0 && *sprinfo[hi].sheet) {
        C2D_Image img = C2D_SpriteSheetGetImage(*sprinfo[hi].sheet, sprinfo[hi].idx);
        C2D_DrawImageAt(img, 0, SCR_H - img.subtex->height, 0.2f, NULL, w / img.subtex->width, 1.0f);
    }
}

static void draw_title_top(void) {
    draw_title_bg(TOP_W);
    ui_z = 0.5f;
    draw_hud("logo_big", 200, 104, 1.0f, WHITE);
    cur_font = font_ui;
    text_ol(10, 202, 0.40f, WHITE, 0, "v1.0.3b (3DS)\nSH2K + Canny Crew, 2026");
}

static void draw_title_bot(void) {
    draw_title_bg(BOT_W);
    ui_z = 0.5f;
    draw_sprite(player_spr(), v2(160, 98), 2.4f, 1.0f, WHITE, sinf(rainbow_t * 2.0f) * 0.1f);
    cur_font = font_ui;
    text_ol(160, 148, 0.48f, C2D_Color32(255, 240, 90, 255), C2D_AlignCenter, SPLASHES[title_splash_idx]);
    if (((int)(rainbow_t * 2) % 2) == 0)
        text_ol(160, 198, 0.50f, WHITE, C2D_AlignCenter, "Tap or Press A to Start");
}

// ---------------------------------------------------------------- Main Menu
static const char *const MAIN_BTNS[4] = {"Play", "Endless Shuffle", "Summer Villa", "Settings"};

static void draw_main_menu_top(void) {
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    PthruSave *p = pthru_cur();
    char b[96];
    snprintf(b, sizeof(b), "Run: %s\nPrisms: %d / 565\nScore: %d", p->name, p->total_prisms, p->total_score);
    text_ol_wrap(10, 82, 0.46f, WHITE, 0, 196, b);
    if (main_cursor == 1 && !g_save.unlocks[UL_MODE_ENDLESS])
        text_ol_wrap(10, 174, 0.42f, grey(0.85f), 0, 196, "Clear World 4 to unlock Endless Shuffle.");
    ol_color = BLACK;
}

static void draw_main_menu(void) {
    draw_menu_bg(0);
    ui_z = 0.5f;
    draw_hud("logo_small", 160, 24, 0.85f, WHITE);
    for (int i = 0; i < 4; i++) {
        bool locked = (i == 1 && !g_save.unlocks[UL_MODE_ENDLESS]);
        button(40, 54 + i * 42, 240, 36, locked ? "? ? ? (Endless)" : MAIN_BTNS[i], i == main_cursor);
    }
}

// ---------------------------------------------------------------- Playthroughs & Modifiers
static const struct { uint32_t bit; int ul; const char *name; const char *desc; } MOD_LIST[11] = {
    {MOD_EASY, UL_MOD_EASY, "Easy Mode", "Removes regular ghosts & special hazards."},
    {MOD_HARD, UL_MOD_HARD, "Hard Mode", "Faster ghosts & double 3-18 coaster goal."},
    {MOD_BURST, UL_MOD_BURST, "Burst", "Re-launch mid-flight!"},
    {MOD_TELEKINESIS, UL_MOD_TELEKINESIS, "Telekinesis", "Steer the cat continuously with stylus/pad."},
    {MOD_BUDDY, UL_MOD_BUDDY, "Little Buddy", "A friend orbits you and blocks one ghost."},
    {MOD_PARRY, UL_MOD_PARRY, "Parry", "Press R/B right before ghost hit to parry!"},
    {MOD_JUMPY, UL_MOD_JUMPY, "Jumpy", "Auto-jumps every 1024 units rolled."},
    {MOD_DIZZY, UL_MOD_DIZZY, "Dizzy", "Your trajectory curves while rolling."},
    {MOD_MISSILES, UL_MOD_MISSILES, "Missiles", "Target reticle locks on & explodes every 4s."},
    {MOD_STICKY, UL_MOD_STICKY, "Sticky", "Stop dead on wall impact."},
    {MOD_GOD, UL_MOD_GOD, "God Mode", "Uncapped power & R+Touch teleports."},
};

static void draw_pthru_select_top(void) {
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    PthruSave *p = &g_save.pthrus[pthru_cursor];
    if (p->used) {
        char b[160];
        snprintf(b, sizeof(b), "%s\nPrisms: %d / 565\nScore: %d\nResets: %d%s",
                 p->name, p->total_prisms, p->total_score, p->total_resets,
                 p->peak_hunter ? "\n[Peak Hunter]" : "");
        text_ol_wrap(10, 80, 0.46f, WHITE, 0, 196, b);
    } else {
        text_ol(10, 96, 0.48f, grey(0.8f), 0, "Empty Playthrough Slot\nTap New Run to create.");
    }
    ol_color = BLACK;
}

static void draw_pthru_select(void) {
    draw_menu_bg(0);
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    text_ol(160, 6, 0.55f, WHITE, C2D_AlignCenter, "Playthroughs");
    ol_color = BLACK;
    for (int i = 0; i < MAX_PTHRUS; i++) {
        PthruSave *p = &g_save.pthrus[i];
        char lbl[48];
        if (p->used) snprintf(lbl, sizeof(lbl), "%s (%d P)", p->name, p->total_prisms);
        else snprintf(lbl, sizeof(lbl), "--- Empty ---");
        button(16, 34 + i * 28, 288, 25, lbl, i == pthru_cursor);
    }
    button(12, 206, 92, 28, "Play (A)", false);
    button(114, 206, 92, 28, "New (X)", false);
    button(216, 206, 92, 28, "Back (B)", false);
}

static void draw_pthru_make_top(void) {
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    if (make_cursor < 11) {
        bool open = g_save.unlocks[MOD_LIST[make_cursor].ul];
        text_ol(10, 82, 0.50f, WHITE, 0, open ? MOD_LIST[make_cursor].name : "? ? ?");
        text_ol_wrap(10, 110, 0.42f, WHITE, 0, 196, open ? MOD_LIST[make_cursor].desc : "Locked modifier.");
    } else if (make_cursor == 11) {
        bool open = g_save.unlocks[UL_PEAK_HUNTER];
        text_ol(10, 82, 0.50f, WHITE, 0, open ? "Peak Hunter" : "? ? ?");
        text_ol_wrap(10, 110, 0.42f, WHITE, 0, 196, open ? "Levels reset unless you score PEAK!" : "Clear World 4 to unlock.");
    } else {
        text_ol(10, 82, 0.50f, WHITE, 0, "Create New Run");
    }
    ol_color = BLACK;
}

static void draw_pthru_make(void) {
    draw_menu_bg(0);
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    text_ol(160, 4, 0.52f, WHITE, C2D_AlignCenter, "New Playthrough");
    ol_color = BLACK;
    for (int i = 0; i < 11; i++) {
        int col = i % 2, row = i / 2;
        bool open = g_save.unlocks[MOD_LIST[i].ul];
        bool on = (make_mods & MOD_LIST[i].bit) != 0;
        char lbl[32];
        snprintf(lbl, sizeof(lbl), "%s%s", open ? MOD_LIST[i].name : "???", on ? " [ON]" : "");
        button(10 + col * 152, 30 + row * 28, 148, 25, lbl, make_cursor == i);
    }
    bool ph_open = g_save.unlocks[UL_PEAK_HUNTER];
    char ph[32];
    snprintf(ph, sizeof(ph), "%s%s", ph_open ? "Peak Hunter" : "???", make_peak ? " [ON]" : "");
    button(162, 30 + 5 * 28, 148, 25, ph, make_cursor == 11);
    button(16, 206, 138, 28, "Create & Play", make_cursor == 12);
    button(166, 206, 138, 28, "Back (B)", false);
}

// ---------------------------------------------------------------- Endless HUD & Game Over
static void draw_endless_top(void) {
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    cur_font = font_ui;
    char b[64];
    int sec = (int)ceilf(endless.total_time);
    snprintf(b, sizeof(b), "%02d:%02d", sec / 60, sec % 60);
    u32 tc = endless.total_time <= 10.0f ? C2D_Color32(255, 80, 80, 255) : WHITE;
    text_ol(12, 76, 0.65f, tc, 0, b);
    snprintf(b, sizeof(b), "Score: %d", endless.total_score);
    text_ol(12, 104, 0.45f, WHITE, 0, b);
    snprintf(b, sizeof(b), "Cleared: %d   Skips: %d", endless.cleared, endless.skips);
    text_ol(12, 124, 0.42f, WHITE, 0, b);
    EndlessLevelRef nx = endless_next(&endless);
    snprintf(b, sizeof(b), "Next: %s", wnames[nx.world][nx.level]);
    text_ol(12, 144, 0.42f, grey(0.9f), 0, b);
    // Demon meter bar
    float ratio = 1.0f - clampf(endless.demon_meter / DEMON_METER_MAX, 0.0f, 1.0f);
    C2D_DrawRectSolid(12, 174, 0.5f, 160, 14, C2D_Color32(30, 10, 10, 220));
    C2D_DrawRectSolid(14, 176, 0.52f, 156 * ratio, 10,
                      endless.demon_active ? C2D_Color32(255, 40, 40, 255) : C2D_Color32(200, 60, 60, 255));
    text_ol(12, 192, 0.38f, endless.demon_active ? C2D_Color32(255, 90, 90, 255) : grey(0.85f), 0,
            endless.demon_active ? (endless.demon_radical ? "RADICAL DEMON!" : "RED DEMON!") : "Demon Meter");
}

static void draw_endless_over_top(void) {
    draw_top_bg();
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    text_ol(285, 28, 0.58f, WHITE, C2D_AlignCenter, "Endless Top 10");
    ol_color = BLACK;
    for (int i = 0; i < g_save.n_endless && i < 7; i++) {
        char line[64];
        snprintf(line, sizeof(line), "%d. %-8s  %7d  (%d)", i + 1, g_save.endless[i].name,
                 g_save.endless[i].score, g_save.endless[i].cleared);
        text_ol(24, 82 + i * 21, 0.44f, WHITE, 0, line);
    }
}

static void draw_endless_over(void) {
    draw_menu_bg(0);
    ui_z = 0.5f;
    cur_font = font_ui;
    text_ol(160, 28, 0.72f, C2D_Color32(255, 100, 100, 255), C2D_AlignCenter, "TIME UP!");
    char b[64];
    snprintf(b, sizeof(b), "Final Score: %d\nLevels Cleared: %d", endless.total_score, endless.cleared);
    text_ol(160, 80, 0.52f, WHITE, C2D_AlignCenter, b);
    button(40, 146, 240, 36, "Play Again (A)", false);
    button(40, 190, 240, 36, "Main Menu (B)", false);
}

// ---------------------------------------------------------------- World Results / Ending / Final Results
static void draw_cutscene_top(void) {
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    cur_font = font_ui;
    if (state == ST_WORLD_RESULTS) {
        char k[32], b[64];
        snprintf(k, sizeof(k), "GAME_World%d", cutscene_world);
        snprintf(b, sizeof(b), "World %d Clear!\n%s", cutscene_world, tr(k));
        text_ol_wrap(10, 86, 0.52f, WHITE, 0, 196, b);
    } else if (state == ST_ENDING) {
        text_ol_wrap(10, 86, 0.52f, C2D_Color32(255, 240, 100, 255), 0, 196,
                     "Golden Tee Claimed!\nWorld P Unlocked!");
    } else if (state == ST_FINAL_RESULTS) {
        char b[64];
        snprintf(b, sizeof(b), "Final Prisms:\n%d / 565", pthru_cur()->total_prisms);
        text_ol_wrap(10, 86, 0.55f, C2D_Color32(255, 240, 100, 255), 0, 196, b);
    }
}

static void draw_cutscene_bot(void) {
    draw_menu_bg(0);
    ui_z = 0.5f;
    cur_font = font_ui;
    draw_sprite(player_spr(), v2(160, 86), 2.2f, 1.0f, WHITE, sinf(cutscene_t * 0.08f) * 0.15f);
    if (state == ST_WORLD_RESULTS) {
        char k[32], b[96];
        snprintf(k, sizeof(k), "GAME_World%d", cutscene_world + 1);
        snprintf(b, sizeof(b), "Next: World %d - %s", cutscene_world + 1, tr(k));
        text_ol_wrap(160, 140, 0.48f, WHITE, C2D_AlignCenter, 280, b);
        button(40, 190, 240, 36, "Continue (A)", false);
    } else if (state == ST_ENDING) {
        text_ol_wrap(160, 140, 0.46f, WHITE, C2D_AlignCenter, 280,
                     "Thanks for playing Uncanny Cat Golf!\nYou can now swim in World P!");
        button(40, 190, 240, 36, "Continue (A)", false);
    } else {
        text_ol_wrap(160, 140, 0.46f, WHITE, C2D_AlignCenter, 280,
                     pthru_cur()->total_prisms >= 565 ? "ALL 565 PRISMS! U-R-NUMBER-1!" : "Thanks for playing!");
        button(40, 190, 240, 36, "Level Select (A)", false);
    }
}

// ---------------------------------------------------------------- Summer Villa
static const char *const VILLA_BTNS[6] = {"Costume House", "Sound Test", "Mystery Phone", "Credits", "Minigames", "Back (B)"};

static void draw_villa_bg(float w) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    bool night = tm && (tm->tm_hour < 6 || tm->tm_hour >= 21);
    draw_menu_bg(night ? 0.0f : 0.0f);
    if (night) C2D_DrawRectSolid(0, 0, 0.12f, w, SCR_H, C2D_Color32(10, 20, 90, 110));
}

static void draw_villa_top(void) {
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    text_ol(10, 80, 0.54f, WHITE, 0, "Summer Villa");
    if (villa_cursor == 0 && !g_save.unlocks[UL_EX_COSTUMES])
        text_ol_wrap(10, 114, 0.42f, grey(0.85f), 0, 196, "Clear World 0 to unlock Costume House.");
    else if (villa_cursor == 1 && !g_save.unlocks[UL_EX_SOUNDTEST])
        text_ol_wrap(10, 114, 0.42f, grey(0.85f), 0, 196, "Clear World 4 to unlock Sound Test.");
    else if (villa_cursor == 2 && !g_save.unlocks[UL_EX_PHONE])
        text_ol_wrap(10, 114, 0.42f, grey(0.85f), 0, 196, "Clear World 1 to unlock Mystery Phone.");
    ol_color = BLACK;
}

static void draw_villa_bot(void) {
    draw_villa_bg(BOT_W);
    ui_z = 0.5f;
    for (int i = 0; i < 6; i++) {
        bool locked = (i == 0 && !g_save.unlocks[UL_EX_COSTUMES]) ||
                      (i == 1 && !g_save.unlocks[UL_EX_SOUNDTEST]) ||
                      (i == 2 && !g_save.unlocks[UL_EX_PHONE]);
        button(40, 18 + i * 36, 240, 31, locked ? "? ? ?" : VILLA_BTNS[i], i == villa_cursor);
    }
}

static void draw_costumes_top(void) {
    draw_top_bg();
    ui_z = 0.5f;
    cur_font = font_ui;
    if (cost_cursor >= 0 && cost_cursor < g_ncostumes) {
        const CostumeDef *c = &g_costumes[cost_cursor];
        bool open = villa_costume_unlocked(cost_cursor);
        text_ol(200, 82, 0.56f, WHITE, C2D_AlignCenter, open ? tr(c->name_key) : "? ? ?");
        if (open) {
            draw_sprite(c->spr_tag, v2(130, 148), 1.8f, 1.0f, WHITE, 0);
            draw_sprite(c->ghost_tag, v2(270, 148), 1.8f, 1.0f, WHITE, 0);
            text_ol(130, 204, 0.40f, grey(0.9f), C2D_AlignCenter, "Cat");
            text_ol(270, 204, 0.40f, grey(0.9f), C2D_AlignCenter, "Ghost");
        } else {
            text_ol(200, 134, 0.46f, grey(0.85f), C2D_AlignCenter, "Keep playing to unlock!");
        }
    }
}

static void draw_costumes_bot(void) {
    draw_menu_bg(0);
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    text_ol(160, 6, 0.52f, WHITE, C2D_AlignCenter, "Costume House");
    ol_color = BLACK;
    for (int r = 0; r < 5; r++) {
        int i = cost_scroll + r;
        if (i >= g_ncostumes) break;
        bool open = villa_costume_unlocked(i);
        bool eq = !strcmp(g_costumes[i].id, g_save.current_costume);
        char lbl[48];
        snprintf(lbl, sizeof(lbl), "%s%s", open ? tr(g_costumes[i].name_key) : "???", eq ? " [EQUIPPED]" : "");
        button(20, 34 + r * 33, 280, 29, lbl, i == cost_cursor);
    }
    button(20, 204, 280, 28, "Back (B)", false);
}

static void draw_soundtest_top(void) {
    draw_top_bg();
    ui_z = 0.5f;
    cur_font = font_ui;
    if (st_playing >= 0 && st_playing < g_nsongs) {
        const SoundTestTrack *s = &g_songs[st_playing];
        u32 col = C2D_Color32f(s->r, s->g, s->b, 1.0f);
        draw_spr(spr_find("cd"), v2(312, 74), 1.0f, 1.0f, col, st_cd_rot);
        draw_sprite(player_spr(), v2(312, 174), 1.6f, 1.0f, WHITE,
                    (st_playing >= 0 && !st_paused) ? sinf(rainbow_t * 6.0f) * 0.2f : 0);
        text_ol(16, 82, 0.52f, WHITE, 0, s->name);
        char dk[48];
        snprintf(dk, sizeof(dk), "SONGDESC_%s", s->desc_key);
        text_ol_wrap(16, 108, 0.40f, grey(0.92f), 0, 210, tr(dk));
    } else {
        text_ol(200, 124, 0.52f, WHITE, C2D_AlignCenter, "Pick a track to play!");
    }
}

static void draw_soundtest_bot(void) {
    draw_menu_bg(0);
    ui_z = 0.5f;
    cur_font = font_ui;
    for (int r = 0; r < 5; r++) {
        int i = st_scroll + r;
        if (i >= g_nsongs) break;
        bool open = villa_song_unlocked(i);
        char lbl[56];
        snprintf(lbl, sizeof(lbl), "%d: %s", i, open ? g_songs[i].name : "???");
        button(16, 10 + r * 34, 288, 30, lbl, i == st_cursor);
    }
    button(16, 190, 90, 36, st_paused ? "Play" : "Pause", false);
    button(114, 190, 104, 36, st_pause_mix ? "Mix 0.75x" : "Mix 1.0x", false);
    button(226, 190, 78, 36, "Back (B)", false);
}

static void draw_phone_top(void) {
    draw_top_bg();
    draw_displayer();
    ui_z = 0.5f;
    cur_font = font_ui;
    ol_color = BLUE;
    text_ol(10, 78, 0.52f, WHITE, 0, "Mystery Phone");
    ol_color = BLACK;
    C2D_DrawRectSolid(10, 108, 0.5f, 196, 28, C2D_Color32(15, 25, 15, 220));
    text_ol(16, 113, 0.48f, C2D_Color32(120, 255, 120, 255), 0,
            phone.phase == PHONE_CALLING ? "CALLING..." : phone.number);
    if (phone.phase == PHONE_IN_CALL && phone.dialogue[0])
        text_ol_wrap(10, 146, 0.42f, WHITE, 0, 196, phone.dialogue);
}

static void draw_phone_bot(void) {
    draw_menu_bg(0);
    ui_z = 0.5f;
    static const char *KEYS[12] = {"1","2","3","4","5","6","7","8","9","*","0","#"};
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++)
            button(40 + c * 82, 12 + r * 42, 76, 36, KEYS[r * 3 + c], false);
    button(24, 190, 86, 38, "DEL", false);
    button(116, 190, 96, 38, phone.phase == PHONE_IN_CALL ? "HANG UP" : "CALL", false);
    button(218, 190, 78, 38, "Back", false);
}

static void draw_credits_top(void) {
    draw_top_bg();
    ui_z = 0.5f;
    cur_font = font_ui;
    static const char *const ROOM_TITLES[4] = {
        "Canny Crew Lounge", "Art & Level Design", "Music & Audio", "3DS Port Credits"
    };
    static const char *const ROOM_TEXT[4] = {
        "SH2K (SlappyHappy2000)\nHaze Weathers, mbx8rt\nOriginal Uncanny Cat Golf (Godot 4.4)",
        "Penny Rigate, greenhead352,\nbibizu, and the Canny Crew",
        "Original OST & SFX by the Canny Crew\nStreamed with Tremor + NDSP",
        "Uncanny Cat Golf 3DS v1.0\nPorted by NickBin\nBuilt with devkitARM + citro2d"
    };
    text_ol(200, 82, 0.60f, C2D_Color32(255, 240, 100, 255), C2D_AlignCenter, ROOM_TITLES[credits_room]);
    text_ol_wrap(200, 114, 0.46f, WHITE, C2D_AlignCenter, 340, ROOM_TEXT[credits_room]);
    text_ol(200, 206, 0.40f, grey(0.85f), C2D_AlignCenter, "L / R to switch rooms  -  START / B to exit");
}

static void draw_credits_bot(void) {
    draw_menu_bg(0);
    ui_z = 0.4f;
    C2D_DrawRectSolid(24, 24, 0.2f, 272, 192, C2D_Color32(35, 30, 50, 200));
    draw_sprite(player_spr(), credits_pos, 1.8f, 1.0f, WHITE, 0);
    button(20, 200, 90, 32, "< Room", false);
    button(115, 200, 90, 32, "Exit (B)", false);
    button(210, 200, 90, 32, "Room >", false);
}
