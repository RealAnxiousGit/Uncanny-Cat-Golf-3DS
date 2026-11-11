# Uncanny Cat Golf 3DS

A Nintendo 3DS port of [Uncanny Cat Golf](https://slappyhappy2000.itch.io/uncanny-cat-golf) by SH2K (SlappyHappy2000), Haze Weathers, mbx8rt, Penny Rigate, greenhead352, bibizu and the rest of the Canny Crew.

**This repository contains no part of the game.** No art, music, sounds, levels or text. Everything the 3DS build shows or plays is converted at build time from the game's own source download on itch.io, which you get yourself.

Status: early work in progress, private. World 0 is playable.

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
```

## Controls

- Touch anywhere on the course, pull back, and let go to shoot.
- Pause: the II button or START. Restart: the R button or Y. Skip a level: hold >> or X.
