// Sound effects and music from the game's own Ogg files (decoded with Tremor).
#pragma once
#include <stdbool.h>

bool audio_init(void);
void audio_exit(void);
// Loads (once) and plays a sound from romfs:/sfx/<rel>. pitch 1 = normal.
void sfx_play(const char *rel, float pitch, float volume);
void sfx_preload(const char *rel);
// Music by the game's Master.Music number (index in romfs:/music/index.txt); -2 stops.
void music_play(int id);
void music_update(void);  // call once per frame
void audio_set_volumes(float master, float music, float sound);
