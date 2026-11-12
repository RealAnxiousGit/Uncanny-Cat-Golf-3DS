"""3DS wording for text baked into the original's pictures.

The World 0 tutorial art names PC controls (left click, mouse, Esc, R, S). These edits
erase those words and letter the 3DS ones in the same colour and place, with the game's
own Comic Sans (assetsNEW/fonts/COMIC.TTF). Positions were measured on the 512 px art.
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT = "res://assetsNEW/fonts/COMIC.TTF"

GREY = (112, 128, 128)
BLUE = (0, 160, 240)
PINK = (208, 48, 96)
GREEN = (96, 176, 80)
WHITE = (240, 240, 240)


def _near(rgb, col, tol=60):
    return np.abs(rgb.astype(np.int32) - np.array(col)).sum(axis=-1) < tol


def _erase(a, box, col):
    """Clears pixels of colour col (and their soft edges) inside box (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = box
    sub = a[y0:y1, x0:x1]
    hit = _near(sub[..., :3], col, 90) | (sub[..., 3] < 200)
    sub[hit] = 0


def _letter(im, font_path, text, box, col, stroke=1, halo=2):
    """Draws text as large as fits inside box, centred, with the art's white outline around it."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    d = ImageDraw.Draw(im)
    out = stroke + halo
    size = h + 8
    while size > 6:
        f = ImageFont.truetype(font_path, size)
        l, t, r, b = d.textbbox((0, 0), text, font=f, stroke_width=out)
        if r - l <= w and b - t <= h:
            break
        size -= 1
    xy = (x0 + (w - (r - l)) / 2 - l, y0 + (h - (b - t)) / 2 - t)
    if halo:
        d.text(xy, text, font=f, fill=WHITE + (255,), stroke_width=out, stroke_fill=WHITE + (255,))
    d.text(xy, text, font=f, fill=col + (255,), stroke_width=stroke, stroke_fill=col + (255,))


def _tutorial5(im, font):
    # PRESS [esc] TO PAUSE / PRESS [R] TO RESET / HOLD [S] TO SKIP: new letters inside the boxes
    a = np.array(im)
    # "START" is too long for the esc box at 3DS size, so the box goes and the word fills its place
    a[239:275, 129:182] = 0  # the box and its white outline (the Y box starts at 275)
    for x0, y0, x1, y1 in ((142, 279, 164, 300), (140, 313, 162, 335)):
        a[y0:y1, x0:x1] = 0
    im = Image.fromarray(a, "RGBA")
    _letter(im, font, "START", (129, 247, 182, 273), BLUE)
    _letter(im, font, "Y", (143, 280, 163, 299), PINK)
    _letter(im, font, "X", (141, 314, 161, 334), GREEN)
    return im


def _tutorial1(im, font):
    a = np.array(im)
    # frame 0: HOLD DOWN / LEFT CLICK / ANYWHERE -> TOUCH AND / HOLD / ANYWHERE (short lines, so the
    # letters can be as big and heavy as the hand-drawn ones and still read at 3DS size)
    a[219:268, 82:234] = 0  # the old two lines and their white outline (ANYWHERE starts at 268)
    # their lowest pixels share rows with ANYWHERE: keep only what lies near its green letters
    band = a[268:274, 82:234]
    green = _near(band[..., :3], GREEN, 90) & (band[..., 3] > 100)
    near = np.zeros_like(green)
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            near |= np.roll(np.roll(green, dy, 0), dx, 1)
    band[~near] = 0
    # frame 1 (telekinesis): MOUSE -> STYLUS, twice
    _erase(a, (582, 244, 666, 273), GREEN)
    _erase(a, (689, 424, 761, 461), GREEN)
    im = Image.fromarray(a, "RGBA")
    _letter(im, font, "TOUCH AND", (84, 220, 232, 244), GREY)
    _letter(im, font, "HOLD", (84, 242, 232, 267), GREY)
    _letter(im, font, "STYLUS", (584, 246, 665, 270), GREEN)
    _letter(im, font, "STYLUS", (691, 430, 760, 457), GREEN)
    return im


EDITS = {
    "res://assetsNEW/graphics/stage/deco/W0/tutorialbg5.png": _tutorial5,
    "res://assetsNEW/graphics/stage/deco/W0/tutorialbg1extra.png": _tutorial1,
}


def apply(project, path, im):
    fn = EDITS.get(path)
    if fn is None:
        return im
    return fn(im.convert("RGBA"), project.fs(FONT))
