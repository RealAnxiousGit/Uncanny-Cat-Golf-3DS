// Save files, kept like the original's Godot ConfigFiles: [SECTION] key=value.
//   config.ini     settings (Settings.save_settings)
//   save_data.ini  progress (SaveData.write_save)
// The 3DSX keeps them in sdmc:/3ds/UncannyCatGolf/, the CIA in its own save data.
#pragma once
#include <stdbool.h>

typedef struct Cfg Cfg;

void save_init(void);
void save_exit(void);

Cfg *cfg_open(const char *name); // loads the file if it exists
bool cfg_write(Cfg *c);          // writes the whole file (and commits CIA save data)
// the same, from the worker thread once save_async(true) is on; save_bg_step() is the worker's share
void cfg_write_later(Cfg *c);
void save_async(bool on);
bool save_bg_step(void);
int cfg_get_int(Cfg *c, const char *sec, const char *key, int def);
float cfg_get_float(Cfg *c, const char *sec, const char *key, float def);
const char *cfg_get_str(Cfg *c, const char *sec, const char *key, const char *def);
void cfg_set_int(Cfg *c, const char *sec, const char *key, int v);
void cfg_set_float(Cfg *c, const char *sec, const char *key, float v);
void cfg_set_str(Cfg *c, const char *sec, const char *key, const char *v);
