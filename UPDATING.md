# Bringing a new Uncanny Cat Golf release to the 3DS port

The port is split so that most of an update is automatic:

- **Converted at build time** (no hand work): levels, collision, objects, floor art, sprites, HUD and menu art, fonts, sounds, music, thoughts clips, translations and attract demos. All of it is read from the game's own files by `tools/convert`.
- **Ported by hand**: the game logic (GDScript to C in `source/`). Every ported part names the `.gd` file it comes from in a comment, so a changed script leads straight to the code to update.
- **Recorded**: `tools/ported-source.txt` lists every file of the version the port follows, with its hash (paths and hashes only, no game content). Its first line is that version; the converter warns when a source folder says otherwise.

## Steps

1. **Get the new source.** Download the new "Uncanny Cat Golf (source)" from the itch.io page and unpack it into its own folder. Keep the old one until the update is done.

2. **See what changed.**

   ```bash
   python tools/game_update.py diff "<new source folder>"
   ```

   This writes `UPDATE-REPORT.md` with three lists:
   1. gameplay scripts to port, each with the port lines that name it (or "not ported yet");
   2. files the converter picks up by itself (levels, art, sound, text);
   3. anything else to look at by hand (`project.godot`, shaders, non-level scenes).

3. **Convert the new files.** From WSL:

   ```bash
   SOURCE="/mnt/c/.../new source folder" ./build.sh convert
   ```

   Then read `build/convert-report.txt`: a node type, shader or object the converter does not know yet shows up there.

4. **Port the scripts from list 1.** Follow the original's logic line by line, as the rest of `source/game.c` does. Keep the comment naming the `.gd` file, so the next update finds it again. A new object type also needs its line in `tools/convert/levels.py` (the object table) and its pictures in the `_d.t3x` sheet.

5. **Check it.**
   - `./build.sh sim`: replays the original's attract demos through the port's physics and says how each ends. Compare the results against the previous version's run.
   - `./build.sh gimtest <world>`: tries shots at every gimmick on each level.
   - `./build.sh 3ds`, then play the changed levels in Azahar.

6. **Record the new version.**

   ```bash
   python tools/game_update.py snapshot "<new source folder>"
   ```

   This updates `tools/ported-source.txt` and the converter's expected version together. Commit it with the port changes.

## Where things live

| Original | Port |
|---|---|
| `scenes/game/levels/**.tscn` | converted by `tools/convert/levels.py` and `bake.py` |
| `scenes/objects/player/P*.gd`, `player.gd` | `source/game.c` (player states) |
| `scenes/game/levels/level.gd` | `source/game.c` (scoring, deaths), `source/main.c` (level flow) |
| `scenes/objects/**` gimmick scripts | `source/game.c`, each under a comment naming its script |
| `scenes/HUD/level_hud.gd`, `pause_menu.gd` | `source/main.c` (top screen, results, pause) |
| `scenes/game/menus/**` | `source/main.c` (level select, settings) |
| `graphics/**`, `sounds/**`, `fonts/**`, translations | converted by `tools/convert` |
| text baked into pictures (PC controls) | `tools/convert/rewords.py` redraws it for the 3DS |

`tools/convert/rewords.py` edits pictures by position. If a new version redraws one of those pictures, the diff lists it under list 2; check the edit still lands in the right place.
