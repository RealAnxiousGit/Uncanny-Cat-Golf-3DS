"""Top-screen sidebar art and the cat's thoughts animations.

The original sidebar is a 256x512 strip; the 3DS top screen is 400x240, so the
pieces are re-laid out side by side: logo and stats on the left, the Cat
Thought Displayer 9000 on the right.

Thoughts: every clip listed by thought_displayer.tscn's category CSVs, read the
same way the game reads them (header line skipped, reading stops at the first
line with fewer than 4 fields). Each clip becomes one .uth file:

  "UTH1" u16 w, u16 h, u16 frames, u16 colours, f32 fps, i16 loops, u16 0,
  u16 palette[colours] (RGB565), u32 offset[frames + 1],
  per frame: u8 mode (0 = indices, 1 = XOR with the previous frame) + zlib data.

The console reads one frame at a time, so a clip never has to fit in memory.
"""
import csv
import io
import os
import struct
import zlib
from multiprocessing import Pool

import numpy as np
from PIL import Image

import scene as S

DISPLAYER = "res://assetsNEW/scenes/HUD/thought_displayer.tscn"
FRAME_W, FRAME_H = 109, 89  # ThoughtDisplayer.FRAME_SIZE
SCREEN_BG = (23, 23, 23)    # the ColorRect behind the thought, Color(0.0911, ...)
COLOURS = 128

# Top-screen layout (pixels). The Displayer is drawn at DISPLAYER_SCALE where
# the original used 0.148607, so the thought grows by the same factor.
DISPLAYER_SCALE = 0.17
ORIG_DISPLAYER_SCALE = 0.148607


def _rgb565(rgb):
    r, g, b = (int(c) for c in rgb)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def read_categories(project):
    root = S.instantiate(project, DISPLAYER)
    tex_path = root.get("textures_path")
    cats = []
    for csv_res in root.get("thought_categories") or []:
        cat = os.path.basename(csv_res)[:-4]
        clips = []
        with open(project.fs(csv_res), encoding="utf-8", newline="") as f:
            rows = list(csv.reader(f))
        for row in rows[1:]:
            if len(row) < 4:
                break  # same stop rule as load_animation_info()
            clips.append((row[0], int(row[1]), float(row[2]), int(row[3])))
        cats.append((cat, clips))
    return tex_path, cats


def encode_clip(job):
    png, out, frames, fps, loops = job
    im = Image.open(png)
    if im.mode in ("RGBA", "LA", "P"):
        im = im.convert("RGBA")
        bg = Image.new("RGBA", im.size, SCREEN_BG + (255,))
        im = Image.alpha_composite(bg, im)
    im = im.convert("RGB")
    cols = max(1, im.size[0] // FRAME_W)
    rows_needed = (frames + cols - 1) // cols
    im = im.crop((0, 0, cols * FRAME_W, min(im.size[1], rows_needed * FRAME_H)))
    q = im.quantize(COLOURS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:COLOURS * 3]
    ncol = len(pal) // 3
    a = np.asarray(q, dtype=np.uint8)
    blobs = []
    prev = None
    for i in range(frames):
        x, y = (i % cols) * FRAME_W, (i // cols) * FRAME_H
        fr = np.zeros((FRAME_H, FRAME_W), np.uint8)
        part = a[y:y + FRAME_H, x:x + FRAME_W]
        fr[:part.shape[0], :part.shape[1]] = part
        plain = zlib.compress(fr.tobytes(), 9)
        blob = b"\0" + plain
        if prev is not None:
            delta = zlib.compress((fr ^ prev).tobytes(), 9)
            if len(delta) < len(plain):
                blob = b"\1" + delta
        blobs.append(blob)
        prev = fr
    head = struct.pack("<4sHHHHfhH", b"UTH1", FRAME_W, FRAME_H, frames, ncol, fps, loops, 0)
    palb = b"".join(struct.pack("<H", _rgb565(pal[i * 3:i * 3 + 3])) for i in range(ncol))
    base = len(head) + len(palb) + 4 * (frames + 1)
    offs, o = [], base
    for b in blobs:
        offs.append(o)
        o += len(b)
    offs.append(o)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(head)
        f.write(palb)
        f.write(struct.pack("<%dI" % len(offs), *offs))
        for b in blobs:
            f.write(b)
    return o


def convert_thoughts(project, out, log, report, workers=3):
    tex_path, cats = read_categories(project)
    tdir = os.path.join(out, "romfs", "thoughts")
    jobs, lines = [], []
    for cat, clips in cats:
        for name, frames, fps, loops in clips:
            png = project.fs("%s/%s/%s.png" % (tex_path, cat, name))
            if not os.path.exists(png):
                report.append("thought %s/%s: no image" % (cat, name))
                continue
            jobs.append((png, os.path.join(tdir, cat, name + ".uth"), frames, fps, loops))
            lines.append("%s %s %d %s %d" % (cat, name, frames, repr(fps), loops))
    # TV static between clips: the Static node's SpriteFrames, 4 frames at 30 fps
    static_png = project.fs("res://assetsNEW/graphics/HUD/thoughts/Static/static.png")
    jobs.append((static_png, os.path.join(tdir, "static.uth"), 4, 30.0, -1))
    with Pool(workers) as p:
        sizes = p.map(encode_clip, jobs, chunksize=1)
    with open(os.path.join(tdir, "index.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    log("  thoughts: %d clips in %d categories, %.1f MB" % (len(lines), len(cats), sum(sizes) / 1e6))


# --- sidebar art --------------------------------------------------------------

def _img(project, res):
    return Image.open(project.fs(res)).convert("RGBA")


def _scaled(im, w=None, h=None, scale=None):
    if scale is not None:
        w, h = im.size[0] * scale, im.size[1] * scale
    return im.resize((max(1, int(round(w))), max(1, int(round(h)))), Image.LANCZOS)


def hud_entries(project):
    """(name, image, anchor px) for the hud atlas; drawn 1:1 on the top screen."""
    g = "res://assetsNEW/graphics/HUD/"
    e = []
    disp = _scaled(_img(project, g + "catthoughtidisplayer9000.png"), scale=DISPLAYER_SCALE)
    e.append(("displayer", disp, (disp.size[0] / 2, disp.size[1] / 2)))
    timer = _scaled(_img(project, g + "timer.png"), 128, 62)
    e.append(("timer", timer, (timer.size[0] / 2, timer.size[1] / 2)))
    balls = _img(project, g + "par.png")
    fw = balls.size[0] // 4
    for i in range(4):
        b = _scaled(balls.crop((i * fw, 0, (i + 1) * fw, balls.size[1])), 36, 37)
        e.append(("par_ball%d" % i, b, (18, 18.5)))
    rb = _scaled(_img(project, g + "results_background.png"), 392, 236)
    e.append(("results_bg", rb, (rb.size[0] / 2, rb.size[1] / 2)))
    clear = _scaled(_img(project, g + "results_cleartext.png"), 290, 33)
    e.append(("results_clear", clear, (clear.size[0] / 2, clear.size[1] / 2)))
    for nm in ("results_rankbarback", "results_rankbarfill"):
        bar = _scaled(_img(project, g + nm + ".png"), 288, 12)
        e.append((nm, bar, (0, 0)))
    for r in range(7):
        sheet = _img(project, g + "rank_%d.png" % r)
        fh = sheet.size[1]
        fw = 448
        n = max(1, sheet.size[0] // fw)
        last = sheet.crop(((n - 1) * fw, 0, n * fw, fh))  # the frame the animation ends on
        im = _scaled(last, scale=0.36)
        e.append(("rank_%d" % r, im, (im.size[0] / 2, im.size[1] / 2)))
    return e


# --- menus -------------------------------------------------------------------
# The original's own menu art (themes/default.tres, button_1.tres, settings.tres,
# level_select.tscn, pause_menu.tscn). Nine-patch pieces keep anchor (0, 0).

# (output name, font file, size in points) for mkbcfnt
# Every font is the game's own Comic Sans (assetsNEW/fonts/COMIC.TTF), as the user asked; the
# original faces were Arial Rounded MT Bold (theme), SuperBubble (settings) and Square 721 (level numbers).
COMIC = "res://assetsNEW/fonts/COMIC.TTF"
FONTS = [
    ("ui", COMIC, 17),     # near the size it is drawn at: shrinking Comic's thin strokes loses them
    ("bubble", COMIC, 17),
    ("square", COMIC, 17),
]


def _brighten(im, k):
    a = np.asarray(im).astype(np.float32)
    a[..., :3] = np.clip(a[..., :3] * k, 0, 255)
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def menu_entries(project):
    m = "res://assetsNEW/graphics/menu/"
    h = "res://assetsNEW/graphics/HUD/"
    ed = "res://assetsNEW/graphics/editor/"
    e = []
    btn = _img(project, m + "button.png")
    e.append(("btn", btn, (0, 0)))
    # hover/focus: the focus texture with the theme's hover modulate (1.5)
    e.append(("btn_hi", _brighten(_img(project, m + "button_focus.png"), 1.5), (0, 0)))
    for st in ("", "_hover", "_pressed", "_disabled"):
        im = _img(project, m + "level_select_button%s.png" % st)
        e.append(("go" + st, im, (32, 32)))
    rs = _img(project, h + "ranks_small.png")
    fw = rs.size[0] // 6  # hframes = 6 in level_select.tscn
    for r in range(6):
        im = _scaled(rs.crop((r * fw, 0, (r + 1) * fw, rs.size[1])), scale=0.5)
        e.append(("rs_%d" % r, im, (im.size[0] / 2, im.size[1] / 2)))
        # full size, for the big badge on the bottom screen's results
        big = rs.crop((r * fw, 0, (r + 1) * fw, rs.size[1]))
        e.append(("rsb_%d" % r, big, (big.size[0] / 2, big.size[1] / 2)))
    paused = _scaled(_img(project, h + "paused_new.png"), 240, 80)
    e.append(("paused", paused, (120, 40)))
    e.append(("slider", _img(project, m + "slider.png"), (0, 0)))
    e.append(("toggle", _img(project, m + "toggle_normal.png"), (0, 0)))
    for nm in ("on", "off", "norank", "peak"):
        im = _img(project, ed + nm + ".png")
        e.append((nm, im, (im.size[0] / 2, im.size[1] / 2)))
    e.append(("menu_tile", _img(project, m + "menu_bg.png"), (0, 0)))
    pz = _img(project, h + "pause_sprite.png")
    e.append(("pause_icon", pz, (pz.size[0] / 2, pz.size[1] / 2)))
    sk = _img(project, h + "skipping.png")
    fw = sk.size[0] // 3
    frames = [sk.crop((i * fw, 0, (i + 1) * fw, sk.size[1])) for i in range(3)]
    boxes = [f.getbbox() or (0, 0, fw, sk.size[1]) for f in frames]
    bb = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
    for i, im in enumerate(frames):  # one shared box, so the frames don't jump around
        im = _scaled(im.crop(bb), scale=0.75)
        e.append(("skip%d" % i, im, (im.size[0] / 2, im.size[1] / 2)))
    return e


def settings_background(project):
    """settings_bg.png's first frame (the gears), fitted to the 320x240 bottom screen."""
    sheet = _img(project, "res://assetsNEW/graphics/menu/settings_bg.png")
    f = sheet.crop((0, 0, sheet.size[1], sheet.size[1])).resize((320, 320), Image.LANCZOS)
    f = f.crop((0, 40, 320, 280))
    canvas = Image.new("RGB", (512, 256))
    canvas.paste(f.convert("RGB"), (0, 0))
    return canvas


def top_background(project):
    g = "res://assetsNEW/graphics/HUD/"
    bg = _img(project, g + "hud_bg.png").resize((400, 240), Image.BILINEAR)
    logo = _scaled(_img(project, "res://assetsNEW/graphics/HUD/logo/logo_en.png"), 140, 70)
    bg.alpha_composite(logo, (20, 4))
    canvas = Image.new("RGB", (512, 256))
    canvas.paste(bg.convert("RGB"), (0, 0))
    return canvas
