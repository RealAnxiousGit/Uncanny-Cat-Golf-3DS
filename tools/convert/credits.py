"""Art for the intro credits, drawn at build time from nothing but this code.

The joke: the credits are a golf scorecard, and the original team are its holes (names and jobs from
the game's own credits room, MENU_Credits* in translations/UCG_T.csv; source/main.c writes them on the
card with the 3DS's system font). The bottom screen is a putting green whose flag carries the porter's
logo (port-art/nickbin-logo.jpg, made by NickBin for the port).

None of it uses the game's files, so it is built into the 3dsx/cia (romfs/port/) even when the game's
own assets come from sdmc:/3ds/Uncanny Cat Golf/. The same goes for the "Oops" screen shown when that
folder is missing: its Comic Sans is the build machine's own (Windows' comic.ttf), never the game's copy,
its cat is a CC0 photo (port-art/README.md), and its sad trombone is made right here.
"""
import math
import os
import struct
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFont

LOGO = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                    "port-art", "nickbin-logo.jpg")

PAPER = (244, 236, 210, 255)
RULE = (150, 186, 222, 255)
MARGIN = (214, 92, 92, 255)
GREEN_A = (86, 170, 72)
GREEN_B = (74, 154, 62)


def scorecard():
    """The blank card, 384x226: ruled paper, a red margin, a column for par, and a pencil circle
    around hole 1's par (SH2K's hole in one). main.c's intro text lines up with these rows."""
    W, H = 384, 226
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, W - 1, H - 1), fill=PAPER, outline=(190, 178, 150, 255))
    for y in range(52, H - 6, 22):
        d.line((2, y, W - 3, y), fill=RULE, width=1)
    d.line((34, 2, 34, H - 3), fill=MARGIN, width=2)
    d.line((W - 46, 30, W - 46, 162), fill=RULE, width=1)
    cy = 52 + 11
    d.ellipse((W - 39, cy - 10, W - 15, cy + 11), outline=MARGIN, width=2)
    return im


def green():
    """The bottom screen: a mowed putting green with its cup, 320x240."""
    im = Image.new("RGBA", (320, 240), GREEN_A + (255,))
    d = ImageDraw.Draw(im)
    for i in range(-12, 20):  # diagonal mowing stripes
        x = i * 32
        d.polygon([(x, 0), (x + 16, 0), (x + 16 + 120, 240), (x + 120, 240)], fill=GREEN_B + (255,))
    # the cup, seen from a little above
    d.ellipse((222, 168, 262, 182), fill=(34, 40, 30, 255))
    d.ellipse((222, 166, 262, 178), outline=(232, 232, 220, 255), width=2)
    return im


def logo():
    """The porter's logo for the flag, 128x128."""
    if not os.path.exists(LOGO):
        return None
    return Image.open(LOGO).convert("RGBA").resize((128, 128), Image.LANCZOS)


def entries():
    """(name, image, anchor) for the credits sheet."""
    out = [("cr_card", scorecard(), (192, 113)), ("cr_green", green(), (0, 0))]
    lg = logo()
    if lg is not None:
        out.append(("cr_logo", lg, (0, 0)))
    return out


# --- the "Oops" screen: shown when the game's files are not on the SD card -------------------------

OOPS_TEXT = ("Oops! We couldn't find the assets in order to launch this game, please ensure that you have "
             "downloaded the source code and put it within your \"sdmc:/3ds/Uncanny Cat Golf/\" folder.")
COMIC_PATHS = [os.environ.get("UCG_COMIC", ""), "/mnt/c/Windows/Fonts/comic.ttf", "C:/Windows/Fonts/comic.ttf",
               "/usr/share/fonts/truetype/msttcorefonts/Comic_Sans_MS.ttf",
               "/usr/share/fonts/truetype/msttcorefonts/comic.ttf"]
SKY = (150, 120, 190, 255)  # the game's menu purple
INK = (40, 30, 60, 255)


def _comic(size, bold=False):
    for p in COMIC_PATHS:
        if not p:
            continue
        q = p.replace("comic.ttf", "comicbd.ttf") if bold else p
        for c in (q, p):
            if os.path.exists(c):
                return ImageFont.truetype(c, size)
    print("WARNING: no Comic Sans on this machine (set UCG_COMIC); the Oops screen uses a plain font")
    return ImageFont.load_default(size)


def _wrap(d, text, font, width):
    lines, cur = [], ""
    for w in text.split(" "):
        t = (cur + " " + w).strip()
        if d.textlength(t, font=font) <= width:
            cur = t
        else:
            lines.append(cur)
            cur = w
    lines.append(cur)
    return lines


CAT = os.path.join(os.path.dirname(LOGO), "upset-cat.jpg")  # CC0, see port-art/README.md


def oops_top():
    """400x240: a big Oops! and the hissing cat as a crooked snapshot."""
    im = Image.new("RGBA", (400, 240), SKY)
    d = ImageDraw.Draw(im)
    big = _comic(68, bold=True)
    d.text((12, 70), "Oops!", font=big, fill=(255, 255, 255, 255), stroke_width=5, stroke_fill=INK)
    if os.path.exists(CAT):
        cat = Image.open(CAT).convert("RGB")
        cat = cat.crop((0, 110, 330, 520))  # the face and the swiping paw
        cat = cat.resize((150, 186), Image.LANCZOS)
        photo = Image.new("RGBA", (162, 206), (252, 252, 248, 255))  # a snapshot's white border
        photo.paste(cat, (6, 6))
        photo = photo.rotate(-6, resample=Image.BICUBIC, expand=True)
        im.alpha_composite(photo, (400 - photo.width - 12, (240 - photo.height) // 2))
    return im


def oops_bottom(text=OOPS_TEXT):
    """320x240: the message, in Comic Sans."""
    im = Image.new("RGBA", (320, 240), (250, 246, 232, 255))
    d = ImageDraw.Draw(im)
    f = _comic(17)
    y = 14
    for line in _wrap(d, text, f, 290):
        d.text((16, y), line, font=f, fill=INK)
        y += 23
    small = _comic(14)
    d.text((16, 240 - 30), "Press START to quit the game", font=small, fill=(120, 110, 140, 255))
    return im


def oops_entries(version):
    wrong = ("Oops! The files in your \"sdmc:/3ds/Uncanny Cat Golf/\" folder don't match this version of the "
             "port. Please make the folder again from the Uncanny Cat Golf %s source code." % version)
    return [("oops_top", oops_top(), (0, 0)), ("oops_bottom", oops_bottom(), (0, 0)),
            ("oops_version", oops_bottom(wrong), (0, 0))]


def sad_trombone(path, rate=22050):
    """Wah, wah, wah, waaaah: four falling notes, the last one wobbling, as 16-bit mono WAV."""
    notes = [(293.66, 0.42), (277.18, 0.42), (261.63, 0.42), (246.94, 1.5)]  # D4 C#4 C4 B3
    out = []
    for n, (f, dur) in enumerate(notes):
        t = np.arange(int(rate * dur)) / rate
        vib = 1 + (0.012 * np.sin(2 * math.pi * 5.5 * t) * np.clip(t * 2, 0, 1) if n == 3 else 0)
        phase = 2 * math.pi * np.cumsum(f * vib) / rate
        # the "wah": a brassy tone whose bright partials open up and close again
        bright = np.sin(np.pi * np.clip(t / dur, 0, 1)) ** 0.6
        sig = np.zeros_like(t)
        for h in range(1, 9):
            sig += np.sin(h * phase) * (1.0 / h) * (bright ** (h * 0.5))
        env = np.clip(t / 0.03, 0, 1) * np.clip((dur - t) / 0.08, 0, 1)
        out.append(sig * env)
    s = np.concatenate(out)
    s = s / np.max(np.abs(s)) * 0.6
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes((s * 32767).astype("<i2").tobytes())
