"""Makes a HOME Menu banner (banner.png 256x128 + banner.wav under 3 s) from a banner.json recipe.

The recipe is ours; the pictures and sounds it names come from the player's own copy of the game, so
the generated banner.png / banner.wav are never committed (git ignores them).

Recipe:
{
  "background": [r, g, b] | {"image": "graphics/menu/menu_bg.png", "tile": true, "scale": 0.5},
  "layers": [{"image": "graphics/player/costume_canny.png", "region": [x, y, w, h],   (optional)
              "x": 60, "y": 70, "width": 70, "rotate": 0, "flip": false, "alpha": 1.0},
             {"line": [x0, y0, x1, y1], "color": [255, 255, 255, 220], "width": 3}],
  "sounds": [{"file": "sounds/player/phwing.ogg", "at": 0.0, "from": 0.0, "to": 0.8, "gain": 1.0}],
  "length": 2.8
}
Paths are relative to the source's assetsNEW folder. x / y place the layer's centre.

Usage: banner.py --source <unpacked source> <BannerN folder> [...]
"""
import argparse
import json
import os
import subprocess
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw

W, H = 256, 128
RATE = 32000  # bannertool turns it into the banner's BCWAV


def _img(assets, rel, region=None):
    im = Image.open(os.path.join(assets, rel)).convert("RGBA")
    if region:
        x, y, w, h = region
        im = im.crop((x, y, x + w, y + h))
    return im


def make_png(assets, recipe, out):
    bg = recipe.get("background", [0, 0, 0, 0])
    if isinstance(bg, dict):
        canvas = Image.new("RGBA", (W, H))
        tile = _img(assets, bg["image"])
        s = float(bg.get("scale", 1.0))
        if s != 1.0:
            tile = tile.resize((max(1, int(tile.width * s)), max(1, int(tile.height * s))), Image.LANCZOS)
        if bg.get("tile", True):
            for y in range(0, H, tile.height):
                for x in range(0, W, tile.width):
                    canvas.alpha_composite(tile, (x, y))
        else:
            canvas.alpha_composite(tile.resize((W, H), Image.LANCZOS))
    else:
        canvas = Image.new("RGBA", (W, H), tuple(bg) + ((255,) if len(bg) == 3 else ()))
    for L in recipe.get("layers", []):
        if "line" in L:
            d = ImageDraw.Draw(canvas, "RGBA")
            d.line(L["line"], fill=tuple(L.get("color", [255, 255, 255, 255])), width=int(L.get("width", 2)))
            continue
        im = _img(assets, L["image"], L.get("region"))
        if L.get("flip"):
            im = im.transpose(Image.FLIP_LEFT_RIGHT)
        w = float(L.get("width", im.width))
        h = w * im.height / im.width
        im = im.resize((max(1, round(w)), max(1, round(h))), Image.LANCZOS)
        if L.get("rotate"):
            im = im.rotate(-float(L["rotate"]), resample=Image.BICUBIC, expand=True)
        if float(L.get("alpha", 1.0)) < 1.0:
            a = np.asarray(im).copy()
            a[..., 3] = (a[..., 3] * float(L["alpha"])).astype(np.uint8)
            im = Image.fromarray(a)
        canvas.alpha_composite(im, (round(L["x"] - im.width / 2), round(L["y"] - im.height / 2)))
    canvas.save(out)


def _decode(path):
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "s16le", "-ac", "1", "-ar", str(RATE), "-"],
                       capture_output=True)
    if r.returncode != 0:
        raise SystemExit("ffmpeg could not read %s: %s" % (path, r.stderr.decode(errors="replace")))
    return np.frombuffer(r.stdout, dtype=np.int16).astype(np.float32) / 32768.0


def make_wav(assets, recipe, out):
    length = min(float(recipe.get("length", 2.8)), 2.95)
    mix = np.zeros(int(length * RATE), dtype=np.float32)
    for S in recipe.get("sounds", []):
        pcm = _decode(os.path.join(assets, S["file"]))
        a = int(float(S.get("from", 0)) * RATE)
        b = int(float(S["to"]) * RATE) if "to" in S else len(pcm)
        pcm = pcm[a:b] * float(S.get("gain", 1.0))
        fade = min(len(pcm), int(0.03 * RATE))  # no click where a cut ends
        if fade:
            pcm[-fade:] *= np.linspace(1, 0, fade)
        at = int(float(S.get("at", 0)) * RATE)
        n = max(0, min(len(pcm), len(mix) - at))
        mix[at:at + n] += pcm[:n]
    peak = float(np.max(np.abs(mix))) if len(mix) else 0
    if peak > 0.89:  # keep it from clipping (and from being loud on the HOME Menu)
        mix *= 0.89 / peak
    with wave.open(out, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes((mix * 32767).astype(np.int16).tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True)
    ap.add_argument("folders", nargs="+")
    a = ap.parse_args()
    assets = os.path.join(a.source, "assetsNEW")
    for folder in a.folders:
        with open(os.path.join(folder, "banner.json"), encoding="utf-8") as f:
            recipe = json.load(f)
        make_png(assets, recipe, os.path.join(folder, "banner.png"))
        make_wav(assets, recipe, os.path.join(folder, "banner.wav"))
        print("banner:", folder)


if __name__ == "__main__":
    sys.exit(main())
