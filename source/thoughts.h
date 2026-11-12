// The Cat Thought Displayer 9000: the clips on the top screen.
// Same behaviour as the original's thought_displayer.gd: play_thought() picks a
// random clip of a category (unless that category is already playing), TV static
// shows for at least 0.2 s, and when a clip has looped enough times the
// fallback category starts.
#pragma once
#include <citro2d.h>
#include <stdbool.h>

void thoughts_init(void);
void thoughts_exit(void);
void thoughts_enable(bool on);
void thoughts_play(const char *category);
void thoughts_fallback(const char *category);
void thoughts_update(float dt);
// current picture (thought or static); false while the displayer is off
bool thoughts_image(C2D_Image *out);
