#!/bin/bash
# Build Uncanny Cat Golf 3DS inside WSL.
#   ./build.sh convert [--worlds 0] [--keep-png]   convert the game's files into build/romfs
#   ./build.sh 3ds [make args]                      build UncannyCatGolf.3dsx (and `cia`)
#       LEAN=1 ./build.sh 3ds                       the release kind: only romfs/port/ inside, the game's
#                                                   files come from sdmc:/3ds/Uncanny Cat Golf/
#   ./build.sh banner                               make Banner1..3 (banner.png + banner.wav) from banner.json
#   ./build.sh sdcard                             make that folder: $WORK/sdcard/Uncanny Cat Golf/
#   ./build.sh sim                                  build and run the PC physics check
#   ./build.sh gimtest <world> [level]              try shots at each gimmick on the PC
#   ./build.sh modtest                              run the 1.0 modifiers, endless & special-level tests
# SOURCE points at the unpacked "Uncanny Cat Golf (source)" folder.
set -e
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$HOME/fs3ds-tools/mr:$HOME/fs3ds-tools/bt:$HOME/fs3ds-tools/bt/bannertool-1.2.3-linux:$PATH"
if [ -z "${SOURCE:-}" ]; then
  if [ -d "/mnt/c/Users/NickBin/Downloads/uncanny-cat-golf-source" ]; then
    SOURCE="/mnt/c/Users/NickBin/Downloads/uncanny-cat-golf-source"
  else
    SOURCE="/mnt/f/uncanny-cat-golf-source"
  fi
fi
REPO="$(cd "$(dirname "$0")" && pwd)"
WORK=${WORK:-$HOME/ucg3ds}

mkdir -p "$WORK"
rsync -a --delete --exclude build --exclude .git --exclude '*.3dsx' --exclude '*.cia' --exclude '*.elf' \
      --exclude 'Banner*/banner.png' --exclude 'Banner*/banner.wav' --exclude graphify-out \
      "$REPO/" "$WORK/src/"
cd "$WORK/src"

cmd=${1:-3ds}; shift || true
case "$cmd" in
  convert)
    nice -n 10 python3 -I tools/convert/convert.py --source "$SOURCE" --out "$WORK/build" "$@"
    ;;
  3ds)
    rm -rf gen romfs && ln -s "$WORK/build" gen
    if [ "${LEAN:-0}" = 1 ]; then # only the port's own files (intro credits, Oops screen)
      rm -rf "$WORK/lean" && mkdir -p "$WORK/lean/romfs" && cp -r "$WORK/build/romfs/port" "$WORK/lean/romfs/"
      ln -s "$WORK/lean/romfs" romfs
    else
      ln -s "$WORK/build/romfs" romfs
    fi
    nice -n 10 make -j3 "$@"
    for f in UncannyCatGolf.3dsx UncannyCatGolf.cia; do [ -f "$f" ] && cp "$f" "$WORK/"; done
    ls -la "$WORK"/*.3dsx "$WORK"/*.cia 2>/dev/null || true
    ;;
  banner)
    # Banner1..3/banner.png + banner.wav from their banner.json, in the repo (to look at and listen to,
    # git ignores them) and here for `make cia BANNER=n`
    python3 -I tools/convert/banner.py --source "$SOURCE" "$REPO"/Banner1 "$REPO"/Banner2 "$REPO"/Banner3
    for n in 1 2 3; do cp "$REPO/Banner$n/banner.png" "$REPO/Banner$n/banner.wav" "Banner$n/"; done
    ;;
  sdcard)
    # the game's converted files, for sdmc:/3ds/Uncanny Cat Golf/ (everything but the port's own)
    rm -rf "$WORK/sdcard" && mkdir -p "$WORK/sdcard/Uncanny Cat Golf"
    rsync -a --exclude port "$WORK/build/romfs/" "$WORK/sdcard/Uncanny Cat Golf/"
    du -sh "$WORK/sdcard/Uncanny Cat Golf"
    ;;
  sim)
    nice -n 10 make -C pc -j3
    ./pc/ucgsim "$WORK/build/romfs" "$SOURCE" "$@"
    ;;
  gimtest)
    nice -n 10 make -C pc -j3 gimtest
    ./pc/gimtest "$WORK/build/romfs" "$@"
    ;;
  modtest)
    nice -n 10 make -C pc -j3 modtest
    ./pc/modtest "$WORK/build/romfs" "$@"
    ;;
  *) echo "unknown command $cmd"; exit 1 ;;
esac
