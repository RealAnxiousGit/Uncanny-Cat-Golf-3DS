#!/usr/bin/env python3
"""Uncanny Cat Golf 3DS converter.

Reads the original game's Godot project (the "source" download from itch.io)
and writes everything the 3DS build loads into <out>/romfs. Nothing from the
game is stored in this repository; it all comes from the folder you point at.

  python3 convert.py --source <game source folder> --out build [--worlds 0,1] [--no-audio]
"""
import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

import bake as B  # noqa: E402
import godot  # noqa: E402
import hud as HUD  # noqa: E402
import credits as CR  # noqa: E402
import levels as L  # noqa: E402
import scene as S  # noqa: E402

def _ported_version():
    """The game version the port follows: first line of tools/ported-source.txt (game_update.py)."""
    rec = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "ported-source.txt")
    try:
        with open(rec, encoding="utf-8") as f:
            first = f.readline().split()
        if len(first) == 2 and first[0] == "version":
            return first[1]
    except OSError:
        pass
    return "1.0.3b"


EXPECTED_VERSION = _ported_version()


def _data_format():
    """DATA_FORMAT from source/data.h, so the game and the converter agree on one number."""
    h = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "source", "data.h")
    m = re.search(r"#define DATA_FORMAT (\d+)", open(h, encoding="utf-8").read())
    return int(m.group(1))


def write_stamp(out, version):
    """romfs/ucg-data.txt: which game version and data format the folder was made from."""
    with open(os.path.join(out, "romfs", "ucg-data.txt"), "w", newline="\n") as f:
        f.write("Uncanny Cat Golf 3DS data\nversion %s\nformat %d\n" % (version, _data_format()))
LEVEL_DIR = "res://assetsNEW/scenes/game/levels/regular"
SPRITE_PX_PER_UNIT = 2.0
TEX3DS = os.path.join(os.environ.get("DEVKITPRO", "/opt/devkitpro"), "tools", "bin", "tex3ds")


def log(*a):
    print(*a, flush=True)


def tex3ds(inputs, out, fmt, atlas=False, z="none"):
    # z="lz11" packs the file (lossless; citro2d unpacks it when loading). Level pictures use it:
    # reading the file is most of a level's load time, and floor art packs to a fraction.
    cmd = [TEX3DS, "-f", fmt, "-z", z, "-o", out]
    if atlas:
        cmd.append("--atlas")
    cmd += inputs
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("tex3ds failed for %s:\n%s%s" % (out, r.stdout, r.stderr))


def pot_pad(path):
    """tex3ds wants power-of-two sizes up to 1024; pad (never scale) when needed."""
    im = Image.open(path)
    w, h = im.size
    pw = 8
    while pw < w:
        pw *= 2
    ph = 8
    while ph < h:
        ph *= 2
    if (pw, ph) != (w, h):
        canvas = Image.new(im.mode, (pw, ph))
        canvas.paste(im, (0, 0))
        canvas.save(path)


# --- levels -----------------------------------------------------------------

def convert_levels(project, tc, out, worlds, report):
    lv_root = os.path.join(out, "romfs", "levels")
    index = []
    for w in worlds:
        wdir = "%s/w%s" % (LEVEL_DIR, w)
        fs_dir = project.fs(wdir)
        if not os.path.isdir(fs_dir):
            report.append("world %s missing" % w)
            continue
        files = sorted(f for f in os.listdir(fs_dir) if f.endswith(".tscn"))

        def pos(f):
            m = re.search(r"_(\d+)\.tscn$", f)
            return int(m.group(1)) if m else 999
        files.sort(key=pos)
        out_dir = os.path.join(lv_root, "w" + w)
        names = []
        for f in files:
            name = f[:-5]
            t = time.time()
            info = L.convert_level(project, tc, "%s/%s" % (wdir, f), out_dir, name, report)
            for suffix, fmt in (("_u", "rgb565"), ("_o", "rgba8"), ("_u2", "rgb565"), ("_o2", "rgba8")):
                png = os.path.join(out_dir, name + suffix + ".png")
                if os.path.exists(png):
                    tex3ds([png], os.path.join(out_dir, name + suffix + ".t3x"), fmt, z="lz11")
                    if not KEEP_PNG:
                        os.remove(png)
            for gid, fp in info.get("tiles", []):  # switchable tile layers, one file each
                pot_pad(fp)
                tex3ds([fp], os.path.join(out_dir, "%s_t%d.t3x" % (name, gid)), "rgba8", z="lz11")
                if not KEEP_PNG:
                    os.remove(fp)
            if info["dyn"]:
                tex3ds(info["dyn"], os.path.join(out_dir, name + "_d.t3x"), "rgba8", atlas=True, z="lz11")
                if not KEEP_PNG:
                    for fp in info["dyn"]:
                        os.remove(fp)
            names.append(name)
            log("  level %-6s %-5s bounds %4d  shapes %4d  objects %2d  %.1fs" % (
                info["id"], name, info["bounds"], info["shapes"], info["objects"], time.time() - t))
        index.append("world %s %d %s" % (w, len(names), " ".join(names)))
    # Endless levels (E_00..E_09) -> levels/we, world 7 in index.txt
    edir = "res://assetsNEW/scenes/game/levels/endless"
    efs = project.fs(edir)
    if os.path.isdir(efs):
        efiles = sorted(f for f in os.listdir(efs) if f.endswith(".tscn"))
        eout = os.path.join(lv_root, "we")
        enames = []
        for f in efiles:
            name = f[:-5]
            t = time.time()
            info = L.convert_level(project, tc, "%s/%s" % (edir, f), eout, name, report)
            for suffix, fmt in (("_u", "rgb565"), ("_o", "rgba8"), ("_u2", "rgb565"), ("_o2", "rgba8")):
                png = os.path.join(eout, name + suffix + ".png")
                if os.path.exists(png):
                    tex3ds([png], os.path.join(eout, name + suffix + ".t3x"), fmt, z="lz11")
                    if not KEEP_PNG:
                        os.remove(png)
            for gid, fp in info.get("tiles", []):
                pot_pad(fp)
                tex3ds([fp], os.path.join(eout, "%s_t%d.t3x" % (name, gid)), "rgba8", z="lz11")
                if not KEEP_PNG:
                    os.remove(fp)
            if info["dyn"]:
                tex3ds(info["dyn"], os.path.join(eout, name + "_d.t3x"), "rgba8", atlas=True, z="lz11")
                if not KEEP_PNG:
                    for fp in info["dyn"]:
                        os.remove(fp)
            enames.append(name)
            log("  endless %-5s %-5s bounds %4d  shapes %4d  %.1fs" % (
                info["id"], name, info["bounds"], info["shapes"], time.time() - t))
        index.append("world 7 %d %s" % (len(enames), " ".join(enames)))
    with open(os.path.join(lv_root, "index.txt"), "w", newline="\n") as f:
        f.write("\n".join(index) + "\n")


# --- sprites ----------------------------------------------------------------

def render_scene_sprite(project, tc, res_path, skip_names=()):
    """Render an object scene's visuals (first animation frame) around its origin."""
    root = S.instantiate(project, res_path)
    items = []
    for n in root.walk():
        if n.name in skip_names or n.type in L.SKIP_TYPES or not S.visible(n):
            continue
        if any(x.name in skip_names for x in _ancestors(n)):
            continue
        if n.type in ("Sprite2D", "AnimatedSprite2D", "Polygon2D"):
            items.append(n)
    # bounds: draw once into a big canvas, crop to content
    ext = 256
    cv = B.Canvas(int(2 * ext * SPRITE_PX_PER_UNIT), -ext, -ext, SPRITE_PX_PER_UNIT)
    items.sort(key=lambda n: (S.global_z(n), n.index))
    for n in items:
        xf = S.global_xform(n)
        mod = S.global_modulate(n)
        if n.type == "Sprite2D":
            B.draw_sprite(cv, tc, n, xf, n.get("texture"), mod)
        elif n.type == "AnimatedSprite2D":
            B.draw_sprite(cv, tc, n, xf, B.frame_texture(n), mod)
        else:
            B.draw_polygon(cv, tc, n, xf, mod)
    return _crop(cv.image(), ext)


def _ancestors(n):
    x = n.parent
    while x is not None:
        yield x
        x = x.parent


def _crop(img, ext):
    a = np.asarray(img)[..., 3]
    ys, xs = np.nonzero(a)
    if len(xs) == 0:
        return img.crop((0, 0, 8, 8)), (0.0, 0.0)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    c = img.crop((x0, y0, x1, y1))
    origin = ext * SPRITE_PX_PER_UNIT
    # anchor = where the scene origin lies inside the cropped image, in pixels
    return c, (origin - x0, origin - y0)


def texture_sprite(project, tc, res_png, world_scale, max_px=256):
    arr = tc.image(res_png)
    h, w = arr.shape[:2]
    tw = max(1, int(round(w * world_scale * SPRITE_PX_PER_UNIT)))
    th = max(1, int(round(h * world_scale * SPRITE_PX_PER_UNIT)))
    if max(tw, th) > max_px:
        f = max_px / max(tw, th)
        tw, th = max(1, int(tw * f)), max(1, int(th * f))
    img = Image.open(project.fs(res_png)).convert("RGBA").resize((tw, th), Image.LANCZOS)
    return img, (tw / 2.0, th / 2.0), (w * world_scale / tw, h * world_scale / th)


def convert_sprites(project, tc, out, report):
    gfx = os.path.join(out, "romfs", "gfx")
    tmp = os.path.join(out, "tmp_sprites")
    os.makedirs(gfx, exist_ok=True)
    os.makedirs(tmp, exist_ok=True)
    entries = []  # (name, Image, anchor px, units per px)

    # costumes: cat and its uncanny ghost (all costumes come later)
    cost_dir = "res://assetsNEW/scenes/utils/costumes"
    for cname in ("canny", "uncanny"):
        r = project.load("%s/%s.tres" % (cost_dir, cname))
        g = S.resolve(project, r, r.resource.props.get("graphic"))
        img, anchor, upp = texture_sprite(project, tc, g.path, 0.064)
        entries.append(("costume_" + cname, img, anchor, upp))
        if cname == "canny":
            # HOME Menu / Homebrew Launcher icon: Canny, 48x48
            meta = os.path.join(out, "meta")
            os.makedirs(meta, exist_ok=True)
            icon = Image.open(project.fs(g.path)).convert("RGBA").resize((44, 44), Image.LANCZOS)
            canvas = Image.new("RGBA", (48, 48), (0, 0, 0, 0))
            canvas.paste(icon, (2, 2), icon)
            canvas.save(os.path.join(meta, "icon.png"))

    for name, path, skip in (
        ("goal", "res://assetsNEW/scenes/objects/important/goal.tscn", ()),
        ("goal_hole", "res://assetsNEW/scenes/objects/important/goal.tscn", ("Flag",)),
        ("coin", "res://assetsNEW/scenes/objects/collectibles/coin.tscn", ("ScorePopup",)),
    ):
        img, anchor = render_scene_sprite(project, tc, path, skip)
        entries.append((name, img, anchor, (1 / SPRITE_PX_PER_UNIT, 1 / SPRITE_PX_PER_UNIT)))

    files = []
    lines = []
    for i, (name, img, anchor, upp) in enumerate(entries):
        p = os.path.join(tmp, "%02d_%s.png" % (i, name))
        img.save(p)
        files.append(p)
        lines.append("%s %d %d %d %s %s %s %s" % (name, i, img.size[0], img.size[1],
                                                L._fmt(anchor[0]), L._fmt(anchor[1]),
                                                L._fmt(upp[0]), L._fmt(upp[1])))
    tex3ds(files, os.path.join(gfx, "sprites.t3x"), "rgba8", atlas=True)
    with open(os.path.join(gfx, "sprites.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    if not KEEP_PNG:
        shutil.rmtree(tmp)
    log("  sprites: %d" % len(entries))


# --- minigames --------------------------------------------------------------

# extra music past master.tscn's songs (music/index.txt ids), for the minigames
MINIGAME_MUSIC = ((30, "res://assetsNEW/music/minigames/musBort.ogg"),)


def _hsv(h, s, v):
    import colorsys
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return (int(round(r * 255)), int(round(g * 255)), int(round(b * 255)), 255)


def convert_minigames(project, out):
    """Bort Bash: gfx/mg_bort.t3x (+ .txt like sprites.txt) and gfx/mg_bort_bg.t3x.
    The Borts' colorswap shader (pure red -> MidShade, pure blue -> ExtraShade, a random hue each)
    is baked into BORT_HUES copies. Frames are shrunk here (Lanczos) to about their size on the
    bottom screen; the txt's units-per-pixel column says by how much."""
    gfx = os.path.join(out, "romfs", "gfx")
    tmp = os.path.join(out, "tmp_minigames")
    os.makedirs(gfx, exist_ok=True)
    os.makedirs(tmp, exist_ok=True)
    g = "res://assetsNEW/graphics/minigames/bartbash/"
    entries = []  # name, image, anchor, upp

    def shrink(img, k):
        w, h = max(1, round(img.size[0] * k)), max(1, round(img.size[1] * k))
        return img.resize((w, h), Image.LANCZOS), (w / 2.0, h / 2.0), 1.0 / k

    bort = Image.open(project.fs(g + "bort.png")).convert("RGBA")
    src = np.array(bort)
    red = np.all(src == (255, 0, 0, 255), axis=-1)
    blue = np.all(src == (0, 0, 255, 255), axis=-1)
    for h in range(8):
        a = src.copy()
        a[red] = _hsv(h / 8.0, 0.5, 1.0)
        a[blue] = _hsv(h / 8.0, 1.0, 0.5)
        im = Image.fromarray(a, "RGBA")
        for f in range(2):
            img, anc, upp = shrink(im.crop((f * 40, 0, f * 40 + 40, 60)), 0.6)
            entries.append(("bort_%d_%d" % (h, f), img, anc, upp))
    boom = Image.open(project.fs(g + "boom.png")).convert("RGBA")
    for f in range(17):
        img, anc, upp = shrink(boom.crop((f * 71, 0, f * 71 + 71, 100)), 0.6)
        entries.append(("boom_%d" % f, img, anc, upp))
    ranks = Image.open(project.fs(g + "bortranks.png")).convert("RGBA")
    for f in range(7):
        img, anc, upp = shrink(ranks.crop((f * 192, 0, f * 192 + 192, 192)), 0.5)
        entries.append(("bortrank_%d" % f, img, anc, upp))
    files, lines = [], []
    for i, (name, img, anc, upp) in enumerate(entries):
        p = os.path.join(tmp, "%02d_%s.png" % (i, name))
        img.save(p)
        files.append(p)
        lines.append("%s %d %d %d %s %s %s %s" % (name, i, img.size[0], img.size[1], L._fmt(anc[0]),
                                                L._fmt(anc[1]), L._fmt(upp), L._fmt(upp)))
    tex3ds(files, os.path.join(gfx, "mg_bort.t3x"), "rgba8", atlas=True)
    with open(os.path.join(gfx, "mg_bort.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    # the cage's floor: meowlBG2.png at scale (1, 0.875) fills exactly x 256..768, y 64..512
    bg = os.path.join(tmp, "bort_bg.png")
    Image.open(project.fs("res://assetsNEW/graphics/minigames/meowl/meowlBG2.png")).convert("RGB") \
        .resize((256, 256), Image.LANCZOS).save(bg)
    tex3ds([bg], os.path.join(gfx, "mg_bort_bg.t3x"), "rgb565")
    if not KEEP_PNG:
        shutil.rmtree(tmp)
    log("  minigames: bort bash, %d pictures" % len(entries))


# --- top-screen sidebar -----------------------------------------------------

def convert_hud(project, out):
    gfx = os.path.join(out, "romfs", "gfx")
    tmp = os.path.join(out, "tmp_hud")
    os.makedirs(gfx, exist_ok=True)
    os.makedirs(tmp, exist_ok=True)
    bg = os.path.join(tmp, "top_bg.png")
    HUD.top_background(project).save(bg)
    tex3ds([bg], os.path.join(gfx, "top_bg.t3x"), "rgb565")
    files, lines = [], []
    for i, (name, img, anchor) in enumerate(HUD.hud_entries(project)):
        p = os.path.join(tmp, "%02d_%s.png" % (i, name))
        img.save(p)
        files.append(p)
        lines.append("%s %d %d %d %s %s 1 1" % (name, i, img.size[0], img.size[1],
                                               L._fmt(anchor[0]), L._fmt(anchor[1])))
    tex3ds(files, os.path.join(gfx, "hud.t3x"), "rgba8", atlas=True)
    with open(os.path.join(gfx, "hud.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    if not KEEP_PNG:
        shutil.rmtree(tmp)
    log("  hud: %d pieces" % len(lines))


MKBCFNT = os.path.join(os.environ.get("DEVKITPRO", "/opt/devkitpro"), "tools", "bin", "mkbcfnt")


def convert_menu(project, out):
    gfx = os.path.join(out, "romfs", "gfx")
    tmp = os.path.join(out, "tmp_menu")
    os.makedirs(gfx, exist_ok=True)
    os.makedirs(tmp, exist_ok=True)
    bg = os.path.join(tmp, "settings_bg.png")
    HUD.settings_background(project).save(bg)
    tex3ds([bg], os.path.join(gfx, "settings_bg.t3x"), "rgb565")
    # jumpscare: the ghost costume's picture at full size, drawn across both screens
    r = project.load("res://assetsNEW/scenes/utils/costumes/uncanny.tres")
    g = S.resolve(project, r, r.resource.props.get("graphic"))
    js = os.path.join(tmp, "jumpscare.png")
    Image.open(project.fs(g.path)).convert("RGB").resize((512, 512), Image.LANCZOS).save(js)
    tex3ds([js], os.path.join(gfx, "jumpscare.t3x"), "rgb565")
    # ash_baby_event.gd: 3-10's ASHBABY!!!!! sprite (512x512), drawn across both screens like the jumpscare
    ab = os.path.join(tmp, "ashbaby.png")
    Image.open(project.fs("res://assetsNEW/graphics/gimmicks/ashbabyjumpscare.png")).convert("RGB").save(ab)
    tex3ds([ab], os.path.join(gfx, "ashbaby.t3x"), "rgb565")
    files, lines = [], []
    for i, (name, img, anchor) in enumerate(HUD.menu_entries(project)):
        p = os.path.join(tmp, "%02d_%s.png" % (i, name))
        img.save(p)
        files.append(p)
        lines.append("%s %d %d %d %s %s 1 1" % (name, i, img.size[0], img.size[1],
                                               L._fmt(anchor[0]), L._fmt(anchor[1])))
    tex3ds(files, os.path.join(gfx, "menu.t3x"), "rgba8", atlas=True)
    with open(os.path.join(gfx, "menu.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    made = set()
    for name, res, size in HUD.FONTS:
        out_f = os.path.join(gfx, name + ".bcfnt")
        if (res, size) in made:  # the same font again: the game falls back to ui.bcfnt
            if os.path.exists(out_f):
                os.remove(out_f)
            continue
        made.add((res, size))
        r = subprocess.run([MKBCFNT, "-s", str(size), "-o", out_f, project.fs(res)], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit("mkbcfnt failed for %s:\n%s%s" % (res, r.stdout, r.stderr))
    if not KEEP_PNG:
        shutil.rmtree(tmp)
    log("  menu: %d pieces, %d fonts" % (len(lines), len(HUD.FONTS)))


# --- the port's own files: intro credits and the Oops screen -------------------

def convert_port(out):
    """romfs/port/: nothing from the game, so release builds keep it in the 3dsx/cia (build.sh LEAN=1)."""
    port = os.path.join(out, "romfs", "port")
    tmp = os.path.join(out, "tmp_port")
    os.makedirs(port, exist_ok=True)
    os.makedirs(tmp, exist_ok=True)
    for sheet, items in (("credits", CR.entries()), ("oops", CR.oops_entries(EXPECTED_VERSION))):
        files, lines = [], []
        for i, (name, img, anchor) in enumerate(items):
            fp = os.path.join(tmp, "%s%02d_%s.png" % (sheet, i, name))
            img.save(fp)
            files.append(fp)
            lines.append("%s %d %d %d %s %s 1 1" % (name, i, img.size[0], img.size[1],
                                                   L._fmt(anchor[0]), L._fmt(anchor[1])))
        tex3ds(files, os.path.join(port, sheet + ".t3x"), "rgba8", atlas=True, z="lz11")
        with open(os.path.join(port, sheet + ".txt"), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    CR.sad_trombone(os.path.join(port, "oops.wav"))
    for old in ("credits.t3x", "credits.txt"):  # where an earlier converter put the credits
        fp = os.path.join(out, "romfs", "gfx", old)
        if os.path.exists(fp):
            os.remove(fp)
    if not KEEP_PNG:
        shutil.rmtree(tmp)
    log("  port: intro credits, Oops screen")


def _save_atlas(entries, tmp, t3x_path, txt_path, keep_png=False):
    os.makedirs(tmp, exist_ok=True)
    os.makedirs(os.path.dirname(t3x_path), exist_ok=True)
    files, lines = [], []
    for i, (name, img, anc, upp) in enumerate(entries):
        p = os.path.join(tmp, "%02d_%s.png" % (i, name))
        img.save(p)
        files.append(p)
        lines.append("%s %d %d %d %s %s %s %s" % (name, i, img.size[0], img.size[1],
                                                L._fmt(anc[0]), L._fmt(anc[1]),
                                                L._fmt(upp[0]), L._fmt(upp[1])))
    tex3ds(files, t3x_path, "rgba8", atlas=True, z="lz11")
    with open(txt_path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    if not keep_png:
        shutil.rmtree(tmp)


# Costumes from save_data.gd in display order: (id, unlock_key_or_none)
PLAYABLE_COSTUMES = [
    ("canny", ""), ("uncanny", ""), ("_blank", ""),
    ("ms_canny", "costume_ms_canny"), ("retro", "costume_retro"), ("dog", "costume_dog"),
    ("veteran", "costume_veteran"), ("cannydigo", "costume_cannydigo"), ("rainbow", "costume_rainbow"),
    ("king", "costume_king"), ("catfish", "costume_catfish"), ("builder", "costume_builder"),
    ("nothing", "costume_nothing"), ("copycat", "costume_copycat"), ("wires", "costume_wires"),
    ("bingus", "costume_bingus"), ("minnie", "costume_minnie"), ("robert", "costume_robert"),
    ("rudy", "costume_rudy"), ("ploobie", "costume_ploobie"), ("pig", "costume_pig"),
    ("chen", "costume_chen"), ("swag", "costume_swag"), ("arleling", "costume_arleling"),
    ("true", "costume_true"),
]


def convert_costumes(project, tc, out):
    """romfs/gfx/costumes.t3x + costumes.txt: all 25 costumes + paired ghost skins (max 96x96)."""
    gfx = os.path.join(out, "romfs", "gfx")
    tmp = os.path.join(out, "tmp_costumes")
    cost_dir = "res://assetsNEW/scenes/utils/costumes"
    entries = []
    seen_spr = {}
    meta_lines = []

    def add_tex(res_path, tag):
        if res_path in seen_spr:
            return seen_spr[res_path]
        img, anc, upp = texture_sprite(project, tc, res_path, 0.064, max_px=96)
        idx = len(entries)
        entries.append((tag, img, anc, upp))
        seen_spr[res_path] = tag
        return tag

    for cid, ukey in PLAYABLE_COSTUMES:
        r = project.load("%s/%s.tres" % (cost_dir, cid))
        pr = r.resource.props
        cname = str(pr.get("name", cid))
        g_ext = S.resolve(project, r, pr.get("graphic"))
        g_path = g_ext.path if isinstance(g_ext, S.Ext) else "res://assetsNEW/graphics/player/costume_canny.png"
        spr_tag = add_tex(g_path, "c_" + cid)
        ghost_id = str(pr.get("ghost_costume", "uncanny") or "uncanny")
        gr = project.load("%s/%s.tres" % (cost_dir, ghost_id))
        gg_ext = S.resolve(project, gr, gr.resource.props.get("graphic"))
        gg_path = gg_ext.path if isinstance(gg_ext, S.Ext) else "res://assetsNEW/graphics/player/costume_uncanny.png"
        gspr_tag = add_tex(gg_path, "g_" + ghost_id)
        snd_ext = S.resolve(project, r, pr.get("sound"))
        snd_rel = "ghost/cannyJumpscare.ogg"
        if isinstance(snd_ext, S.Ext) and "/sounds/" in snd_ext.path:
            snd_rel = snd_ext.path.split("/sounds/", 1)[1]
        meta_lines.append("%s\t%s\t%s\t%s\t%s\t%s" % (cid, cname, spr_tag, gspr_tag, snd_rel, ukey or "-"))

    _save_atlas(entries, tmp, os.path.join(gfx, "costumes.t3x"), os.path.join(gfx, "costumes_atlas.txt"), KEEP_PNG)
    with open(os.path.join(gfx, "costumes.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(meta_lines) + "\n")
    log("  costumes: %d costumes, %d sprites" % (len(meta_lines), len(entries)))


def convert_extras(project, tc, out):
    """romfs/gfx/title.t3x and romfs/gfx/extras.t3x for title, modifiers, endless, villa & special levels."""
    gfx = os.path.join(out, "romfs", "gfx")
    g = "res://assetsNEW/graphics/"

    def load_scaled(res, max_w, max_h=None, center=True):
        im = Image.open(project.fs(res)).convert("RGBA")
        if max_h is None:
            max_h = max_w
        if im.size[0] > max_w or im.size[1] > max_h:
            im.thumbnail((max_w, max_h), Image.LANCZOS)
        anc = (im.size[0] / 2.0, im.size[1] / 2.0) if center else (0.0, 0.0)
        return im, anc, (1.0, 1.0)

    # Title screen atlas
    t_entries = []
    for nm, res, mw, mh, ctr in (
        ("title_grid", g + "menu/title_bg_grid.png", 400, 240, False),
        ("title_hills", g + "menu/title_bg_hills2.png", 400, 160, False),
        ("logo_big", g + "HUD/logo/logo_en.png", 280, 140, True),
        ("logo_small", g + "HUD/logo_small/logo_small_en.png", 260, 48, True),
    ):
        im, anc, upp = load_scaled(res, mw, mh, ctr)
        t_entries.append((nm, im, anc, upp))
    _save_atlas(t_entries, os.path.join(out, "tmp_title"), os.path.join(gfx, "title.t3x"),
                os.path.join(gfx, "title.txt"), KEEP_PNG)

    # Extras / modifiers / endless / special-level atlas
    e_entries = []
    bee_sheet = Image.open(project.fs(g + "modifier/bee.png")).convert("RGBA")
    bw = bee_sheet.size[0] // 2
    for i in range(2):
        fr = bee_sheet.crop((i * bw, 0, (i + 1) * bw, bee_sheet.size[1]))
        fr.thumbnail((24, 24), Image.LANCZOS)
        e_entries.append(("bee_%d" % i, fr, (fr.size[0] / 2.0, fr.size[1] / 2.0), (1.0, 1.0)))

    for nm, res, mw in (
        ("gravestone", g + "gimmicks/uncanny/gravestone.png", 36),
        ("reticle", g + "modifier/missile_reticle.png", 48),
        ("explosion", g + "gimmicks/mine_explosion.png", 96),
        ("red_demon", g + "player/redDemon.png", 64),
        ("rad_demon", g + "player/radDemon.png", 64),
        ("sparkle", g + "HUD/peak_sparkle.png", 20),
        ("cd", g + "misc/sound_test_cd.png", 112),
        ("cd_hand", g + "misc/sound_test_hand.png", 48),
        ("play_btn", g + "HUD/play_sprite.png", 40),
        ("pause_btn", g + "HUD/pause_sprite.png", 40),
        ("slot_eye", g + "gimmicks/eyes.png", 64),
        ("golden_tee", g + "stage/deco/W5/golden_tee.png", 48),
        ("coaster_warn_coin", g + "gimmicks/coaster_money_warning.png", 36),
        ("coaster_warn_ghost", g + "gimmicks/coaster_ghost_warning.png", 36),
        ("coaster_car", g + "gimmicks/roller_coaster.png", 128),
        ("demon_back", g + "HUD/demon_meter_back.png", 160),
        ("demon_fill", g + "HUD/demon_meter_fill.png", 160),
        ("icon_sound", g + "menu/extras_menu/icon_sound_test.png", 48),
    ):
        im, anc, upp = load_scaled(res, mw, mw, True)
        e_entries.append((nm, im, anc, upp))
    _save_atlas(e_entries, os.path.join(out, "tmp_extras"), os.path.join(gfx, "extras.t3x"),
                os.path.join(gfx, "extras.txt"), KEEP_PNG)
    log("  extras: title (%d), extras (%d)" % (len(t_entries), len(e_entries)))


# --- audio ------------------------------------------------------------------

def import_loop(project, res_path):
    imp = project.fs(res_path) + ".import"
    loop, offset, bpm = False, 0.0, 0.0
    if os.path.exists(imp):
        for line in open(imp, encoding="utf-8"):
            if line.startswith("loop="):
                loop = line.strip().endswith("true")
            if line.startswith("loop_offset="):
                offset = float(line.split("=")[1])
            if line.startswith("bpm="):
                bpm = float(line.split("=")[1])
    return loop, offset, bpm


def convert_audio(project, out):
    master = S.instantiate(project, "res://assetsNEW/scenes/master.tscn")
    songs = master.get("songs") or []
    mdir = os.path.join(out, "romfs", "music")
    os.makedirs(mdir, exist_ok=True)
    idx = []
    for i, np_ in enumerate(songs):
        node = master.find(str(np_))
        st = node.get("stream") if node else None
        if isinstance(st, S.Ext) and st.path.endswith(".ogg"):
            fn = os.path.basename(st.path)
            shutil.copyfile(project.fs(st.path), os.path.join(mdir, fn))
            loop, off, bpm = import_loop(project, st.path)
            idx.append("%d %s %d %s %s" % (i, fn, int(loop), L._fmt(off), L._fmt(bpm)))
    for i, path in MINIGAME_MUSIC:
        fn = os.path.basename(path)
        shutil.copyfile(project.fs(path), os.path.join(mdir, fn))
        loop, off, bpm = import_loop(project, path)
        idx.append("%d %s %d %s %s" % (i, fn, int(loop), L._fmt(off), L._fmt(bpm)))

    # Extra OGGs for World Clear, Ending, Credits, Costume House, Phone & Sound Test
    extra_dirs = (
        ("res://assetsNEW/music", ""),
        ("res://assetsNEW/music/phone", "phone/"),
        ("res://assetsNEW/music/soundTestExclusive", "soundTestExclusive/"),
        ("res://assetsNEW/music/costume", "costume/"),
    )
    copied = {fn for fn in os.listdir(mdir) if fn.endswith(".ogg")}
    for res_dir, sub in extra_dirs:
        fsd = project.fs(res_dir)
        if not os.path.isdir(fsd):
            continue
        for fn in sorted(os.listdir(fsd)):
            if not fn.lower().endswith(".ogg"):
                continue
            rel = sub + fn
            if not sub and fn in copied:
                continue
            dst = os.path.join(mdir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(fsd, fn), dst)

    # Parse sound_test.tscn for Sound Test song list
    st_res = S.instantiate(project, "res://assetsNEW/scenes/game/menus/extras/sound_test.tscn")
    st_lines = []
    for item in (st_res.get("songs") or []):
        sub = item if hasattr(item, "props") else S.resolve(project, st_res, item)
        if sub is None:
            continue
        sp = sub.get("song")
        spath = getattr(sp, "path", "") if sp is not None else ""
        if "/music/" not in spath:
            continue
        rel = spath.split("/music/", 1)[1]
        sname = str(sub.get("name", rel)).replace("\t", " ")
        dkey = str(sub.get("desc_key", ""))
        ru = str(sub.get("required_unlock", "") or "")
        col = S.color(sub.get("cd_color"))
        loop, off, bpm = import_loop(project, spath)
        st_lines.append("%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s" % (
            rel, sname, dkey, ru or "-", int(loop), L._fmt(off), L._fmt(col[0]), L._fmt(col[1]), L._fmt(col[2])))
    with open(os.path.join(mdir, "soundtest.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(st_lines) + "\n")

    with open(os.path.join(mdir, "index.txt"), "w", newline="\n") as f:
        f.write("\n".join(idx) + "\n")
    # sound effects: copied as they are (Ogg Vorbis), decoded on the console
    sdir = os.path.join(out, "romfs", "sfx")
    src = project.fs("res://assetsNEW/sounds")
    n = 0
    for dp, ds, fs in os.walk(src):
        for fn in fs:
            if fn.lower().endswith(".ogg"):
                rel = os.path.relpath(os.path.join(dp, fn), src).replace(os.sep, "/")
                dst = os.path.join(sdir, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(os.path.join(dp, fn), dst)
                n += 1
    log("  music: %d tracks, sfx: %d" % (len(idx), n))


# --- attract demos (also the physics check's test cases) --------------------

def convert_demos(project, out):
    src = "res://assetsNEW/scenes/game/levels/attract_data"
    ddir = os.path.join(out, "romfs", "demos")
    os.makedirs(ddir, exist_ok=True)
    lines = []
    for fn in sorted(os.listdir(project.fs(src))):
        if not fn.endswith(".tres") or "TEMPLATE" in fn:
            continue
        r = project.load("%s/%s" % (src, fn))
        p = r.resource.props
        shots = []
        for ref in p.get("inputs") or []:
            sub = S.resolve(project, r, ref)
            d = S.vec(sub.get("direction"), (1.0, 0.0))
            shots.append("%d %s %s %s" % (int(sub.get("wait_time", 0)), L._fmt(float(sub.get("power", 0.0))),
                                          L._fmt(d[0]), L._fmt(d[1])))
        lines.append("%s %d %d %d %d %s" % (fn[:-5], int(p.get("world_ID", 0)), int(p.get("level_ID", 0)),
                                           int(p.get("demo_time", 0)), len(shots), " ".join(shots)))
    with open(os.path.join(ddir, "index.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    log("  demos: %d" % len(lines))


# --- text -------------------------------------------------------------------

def convert_text(project, out):
    src = project.fs("res://assetsNEW/translations/UCG_T.csv")
    ldir = os.path.join(out, "romfs", "lang")
    os.makedirs(ldir, exist_ok=True)
    with open(src, encoding="utf-8-sig", newline="") as f:
        rows = list(csv.reader(f))
    head = rows[0]
    langs = [h for h in head[2:] if h]
    for li, lang in enumerate(langs):
        col = head.index(lang)
        with open(os.path.join(ldir, lang + ".txt"), "w", encoding="utf-8", newline="\n") as f:
            for r in rows[1:]:
                if not r or not r[0] or len(r) <= col or not r[col]:
                    continue
                val = r[col].replace("\\", "\\\\").replace("\n", "\\n")
                f.write("%s\t%s\n" % (r[0], val))
    log("  languages: %s" % ", ".join(langs))


KEEP_PNG = False


def main():
    global KEEP_PNG
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True, help="unpacked Uncanny Cat Golf source folder")
    ap.add_argument("--out", required=True)
    ap.add_argument("--worlds", default="0,1,2,3,4,5,6")
    ap.add_argument("--no-audio", action="store_true")
    ap.add_argument("--no-thoughts", action="store_true", help="skip the thoughts clips (slowest step)")
    ap.add_argument("--keep-png", action="store_true", help="keep baked PNGs next to the .t3x files")
    ap.add_argument("--only", default="", help="comma list of steps: levels,sprites,costumes,extras,hud,menu,port,thoughts,text,demos,audio,minigames")
    a = ap.parse_args()
    KEEP_PNG = a.keep_png
    only = {s.strip() for s in a.only.split(",") if s.strip()}

    def want(step):
        return not only or step in only
    t0 = time.time()
    project = godot.Project(a.source)
    ver = project.settings.get("application/config/version")
    if ver != EXPECTED_VERSION:
        log("WARNING: this converter was made for version %s, the source says %r" % (EXPECTED_VERSION, ver))
    tc = B.TexCache(project)
    report = []
    log("Uncanny Cat Golf %s -> %s" % (ver, a.out))
    if want("levels"):
        log("levels:")
        convert_levels(project, tc, a.out, [w.strip() for w in a.worlds.split(",") if w.strip()], report)
    if want("sprites"):
        log("sprites:")
        convert_sprites(project, tc, a.out, report)
    if want("costumes"):
        log("costumes:")
        convert_costumes(project, tc, a.out)
    if want("extras"):
        log("extras:")
        convert_extras(project, tc, a.out)
    if want("minigames"):
        log("minigames:")
        convert_minigames(project, a.out)
    if want("hud"):
        log("hud:")
        convert_hud(project, a.out)
    if want("menu"):
        log("menu:")
        convert_menu(project, a.out)
    if want("port"):
        log("port:")
        convert_port(a.out)
    os.makedirs(os.path.join(a.out, "romfs"), exist_ok=True)
    write_stamp(a.out, ver)
    if want("thoughts") and not a.no_thoughts:
        log("thoughts:")
        HUD.convert_thoughts(project, a.out, log, report)
    if want("text"):
        log("text:")
        convert_text(project, a.out)
    if want("demos"):
        log("demos:")
        convert_demos(project, a.out)
    if want("audio") and not a.no_audio:
        log("audio:")
        convert_audio(project, a.out)
    if tc.unsupported:
        report.append("not drawn yet: " + ", ".join(sorted(tc.unsupported)))
    report_name = "convert-report.txt" if not only else "convert-report-partial.txt"
    with open(os.path.join(a.out, report_name), "w", newline="\n") as f:
        f.write("\n".join(report) + "\n")
    log("done in %.0fs, %d report lines (convert-report.txt)" % (time.time() - t0, len(report)))


if __name__ == "__main__":
    main()
