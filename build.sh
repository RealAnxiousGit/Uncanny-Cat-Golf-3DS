#!/bin/bash
# Build Uncanny Cat Golf 3DS inside WSL.
#   ./build.sh convert [--worlds 0] [--keep-png]   convert the game's files into build/romfs
#   ./build.sh 3ds [make args]                      build UncannyCatGolf.3dsx (and `cia`)
#   ./build.sh sim                                  build and run the PC physics check
# SOURCE points at the unpacked "Uncanny Cat Golf (source)" folder.
set -e
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$HOME/fs3ds-tools/mr:$HOME/fs3ds-tools/bt:$PATH"
SOURCE=${SOURCE:-/mnt/c/Users/NickBin/Downloads/uncanny-cat-golf-source}
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
    rm -rf gen romfs && ln -s "$WORK/build" gen && ln -s "$WORK/build/romfs" romfs
    nice -n 10 make -j3 "$@"
    for f in UncannyCatGolf.3dsx UncannyCatGolf.cia; do [ -f "$f" ] && cp "$f" "$WORK/"; done
    ls -la "$WORK"/*.3dsx "$WORK"/*.cia 2>/dev/null || true
    ;;
  sim)
    nice -n 10 make -C pc -j3
    ./pc/ucgsim "$WORK/build/romfs" "$SOURCE" "$@"
    ;;
  *) echo "unknown command $cmd"; exit 1 ;;
esac
