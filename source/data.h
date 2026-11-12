// Where the game's own files (converted from the itch.io download) are read from.
// A release 3dsx/cia carries only the port's code and its own art (romfs:/port/); the game's files
// live in sdmc:/3ds/Uncanny Cat Golf/. Test builds carry them in romfs instead, which works too.
#pragma once
#include <stdbool.h>

#define DATA_SD "sdmc:/3ds/Uncanny Cat Golf"
// Bumped whenever the converter's output changes in a way older folders can't follow; the converter
// reads it from here and writes it into the folder's ucg-data.txt.
#define DATA_FORMAT 1
#ifndef UCG_GAME_VERSION
#define UCG_GAME_VERSION "?"
#endif

enum { DATA_OK, DATA_MISSING, DATA_WRONG_VERSION };
int data_find(void);              // where the game's files are, and whether they fit this build
const char *data_root(void);      // DATA_SD or "romfs:"
const char *dp(const char *rel);  // data_root() + "/" + rel; the last 8 results stay valid
