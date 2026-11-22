# Uncanny Cat Golf 3DS

A Nintendo 3DS port of [Uncanny Cat Golf](https://slappyhappy2000.itch.io/uncanny-cat-golf) by SH2K (SlappyHappy2000), Haze Weathers, mbx8rt, Penny Rigate, greenhead352, bibizu and the rest of the Canny Crew.

Status: **v1.0.0 complete** — full main game playable on New 3DS and Old 3DS (113 levels across Worlds 0–6, Playthroughs & 15 Modifiers, Endless Shuffle, Summer Villa, Bort Bash, and self-contained `.3dsx` + `.cia`).

## Features (v1.0.0)

- **All 113 levels (Worlds 0–6 + W3–W6 special scenes)**: 3-18 roller coaster, 5-16 wizard spells, 5-18 golden tee & ending, P-15 vertical scroll, 2-17 slot machine, portals, nukes, prism gates, horse boss, metronome layers.
- **Title Screen & Attract Mode**: animated title, splash lines, and idle attract demos.
- **Playthroughs & Modifiers**: 6 playthrough slots (`pthru_0..5.ini`), Peak Hunter, and all 15 playthrough/endless modifiers (`Easy`, `Hard`, `Burst`, `Telekinesis`, `Little Buddy`, `Parry`, `Jumpy`, `Dizzy`, `Missiles`, `Sticky`, `God Mode`, plus Endless hazards).
- **Endless Shuffle**: `E_00`–`E_09` + W0–W4 pool, 61s timer, skips, modifier scaling, Demon meter, and Top 10 leaderboard.
- **Summer Villa & Minigames**: Costume House (25 costumes + paired ghost skins & jumpscare SFX), Sound Test (with pause & 0.75x mix toggle), Mystery Phone (20+ secret codes), Interactive Credits, and Bort Bash.
- **Old 3DS & New 3DS**: real-time fixed-timestep accumulator on Old 3DS, linear-heap guard for streaming audio, and multi-threaded audio/asset loading.

## How it works

- `tools/convert/` reads the original Godot 4 project (scenes, tile sets, textures, translations, demos) and writes the 3DS data into `build/romfs`: baked level images, collision shapes, sprites, music and sound effects.
- `source/` is the game, rewritten in C for the 3DS (citro2d, ndsp, Tremor). The golf rules, friction, bounces, scoring and ranks follow the original scripts.
- `pc/` replays the game's own attract-mode demos with the same C code, to check the physics against the original.

## Building (WSL / Linux)

Needs devkitARM with libctru, citro2d and libvorbisidec, Python 3 with Pillow and NumPy, plus makerom and bannertool for the CIA.

```
SOURCE=/path/to/uncanny-cat-golf-source ./build.sh convert
./build.sh 3ds          # UncannyCatGolf.3dsx
./build.sh 3ds cia      # UncannyCatGolf.cia (uses Banner1 by default, BANNER=2 or 3 to change)
./build.sh sim          # physics check against the attract demos
./build.sh modtest      # unit tests for all 15 modifiers, Endless Shuffle, Bort Bash & W3-W6 scenes
./build.sh gimtest 1    # tries shots at every World 1 gimmick
```

`./build.sh 3ds PROFILE=1` builds a copy that writes `sdmc:/ucg_prof.txt`: every frame whose work takes over 12 ms, split into parts, to find stutters. Music, sound decoding, the thoughts clips and save writes run on a second thread (`source/worker.c`); level loads happen while the screen is faded to black.

## Where the game's files live

The game reads its converted files from `sdmc:/3ds/Uncanny Cat Golf/` when that folder is there, and otherwise from inside the 3dsx (test builds carry them). With neither, it shows an "Oops" screen saying where to put them.

```
LEAN=1 ./build.sh 3ds   # the release kind: about 750 KB, only the port's own art inside (romfs/port/)
./build.sh sdcard       # the game's files as the "Uncanny Cat Golf" folder for the SD card's 3ds/ folder
```

The port's own art (the intro credits, NickBin's logo, the Oops screen's cat and sad trombone) is made by `tools/convert/credits.py` from `port-art/` and never uses the game's files; `port-art/README.md` lists where each picture comes from.

## When the game gets an update

See [UPDATING.md](UPDATING.md): `python tools/game_update.py diff <new source>` lists what changed and which port code each changed script maps to.

## Controls

- Touch anywhere on the course, pull back, and let go to shoot.
- Pause: the II button or START. Restart: the Y button (top right) or Y. Skip a level: hold >> or X.
- L: switch between the zoomed course (fills the bottom screen, follows the cat) and the whole course.
