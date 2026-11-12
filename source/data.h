// Where the game's own files (converted from the itch.io download) are read from.
// A release 3dsx/cia carries only the port's code and its own art (romfs:/port/); the game's files
// live in sdmc:/3ds/Uncanny Cat Golf/. Test builds carry them in romfs instead, which works too.
#pragma once
#include <stdbool.h>

#define DATA_SD "sdmc:/3ds/Uncanny Cat Golf"

bool data_find(void);             // false when the game's files are in neither place
const char *data_root(void);      // DATA_SD or "romfs:"
const char *dp(const char *rel);  // data_root() + "/" + rel; the last 8 results stay valid
