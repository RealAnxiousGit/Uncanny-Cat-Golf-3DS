"""Finds what a new Uncanny Cat Golf release changed, and what the 3DS port must do about it.

  python tools/game_update.py snapshot <source folder>     record the version the port is built from
  python tools/game_update.py diff <new source folder>     compare a new release against that record

The record (tools/ported-source.txt) holds only file paths and SHA-1 hashes: no game content.
The diff sorts every added, changed and removed file into:
  - convert: levels, art, sound, fonts, text. `./build.sh convert` picks these up by itself.
  - port:    GDScript. The game logic is hand-ported to C, so each changed script is listed with
             the port code that names it (every ported function says which .gd it comes from).
  - check:   anything else (project settings, shaders, scenes outside levels).
It writes UPDATE-REPORT.md next to this repo's README.
"""
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RECORD = os.path.join(ROOT, "tools", "ported-source.txt")
REPORT = os.path.join(ROOT, "UPDATE-REPORT.md")
PORT_DIRS = ("source", "tools/convert", "pc")
SKIP_DIRS = {".godot", ".import", "addons", ".git"}

# Scripts the port follows that its comments do not name by file (or names under another name).
PORT_MAP = {
    "PMoving.gd": "source/game.c (state_update, PS_MOVING; hit_moving_wall)",
    "PIdle.gd": "source/game.c (state_update, PS_IDLE; drag_vector, friction)",
    "PWin.gd": "source/game.c (state_update, PS_WIN)",
    "level_hud.gd": "source/main.c (draw_top_hud, draw_results_top, jumpscare, death banner)",
    "settings_menu.gd": "source/main.c (settings: set_label, set_change, load_settings)",
    "pause_menu.gd": "source/main.c (draw_pause, ST_PAUSE input)",
    "level_select.gd": "source/main.c (draw_select, ST_SELECT input)",
    "master.gd": "source/main.c (save data, costumes) and tools/convert/convert.py (version check)",
}


def files(folder):
    out = {}
    for dp, dn, fn in os.walk(folder):
        dn[:] = [d for d in dn if d not in SKIP_DIRS]
        for f in fn:
            if f.endswith((".import", ".uid")):
                continue  # Godot's editor bookkeeping
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, folder).replace(os.sep, "/")
            h = hashlib.sha1()
            with open(p, "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    h.update(chunk)
            out[rel] = h.hexdigest()
    return out


def version(folder):
    try:
        for line in open(os.path.join(folder, "project.godot"), encoding="utf-8"):
            m = re.match(r'config/version="(.*)"', line.strip())
            if m:
                return m.group(1)
    except OSError:
        pass
    return "?"


def kind(rel):
    low = rel.lower()
    if low.endswith(".gd"):
        return "port"
    if "/scenes/game/levels/" in low or low.endswith((".png", ".jpg", ".webp", ".svg", ".ogg", ".wav", ".mp3",
                                                      ".ttf", ".otf", ".csv", ".translation", ".ogv", ".webm")):
        return "convert"
    return "check"


def port_refs(script):
    """Port files and lines that name this .gd file."""
    name = os.path.basename(script)
    hits = []
    for d in PORT_DIRS:
        base = os.path.join(ROOT, d)
        for dp, _, fn in os.walk(base):
            for f in fn:
                if not f.endswith((".c", ".h", ".py")) or f == os.path.basename(__file__):
                    continue
                p = os.path.join(dp, f)
                for i, line in enumerate(open(p, encoding="utf-8", errors="replace"), 1):
                    if name in line:
                        hits.append("%s:%d" % (os.path.relpath(p, ROOT).replace(os.sep, "/"), i))
    if name in PORT_MAP:
        hits.insert(0, PORT_MAP[name])
    return hits


def snapshot(folder):
    fs = files(folder)
    with open(RECORD, "w", newline="\n") as f:
        f.write("version %s\n" % version(folder))
        for rel in sorted(fs):
            f.write("%s %s\n" % (fs[rel], rel))
    print("recorded %d files of version %s in %s" % (len(fs), version(folder), os.path.relpath(RECORD, ROOT)))


def diff(folder):
    old, old_ver = {}, "?"
    for line in open(RECORD, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("version "):
            old_ver = line[8:]
        elif line:
            h, rel = line.split(" ", 1)
            old[rel] = h
    new = files(folder)
    new_ver = version(folder)
    groups = {"convert": [], "port": [], "check": []}
    for rel in sorted(set(old) | set(new)):
        if rel in old and rel in new and old[rel] == new[rel]:
            continue
        state = "added" if rel not in old else "removed" if rel not in new else "changed"
        groups[kind(rel)].append((state, rel))
    lines = ["# Game update: %s -> %s" % (old_ver, new_ver), ""]
    total = sum(len(v) for v in groups.values())
    if not total:
        lines.append("Nothing changed.")
    lines += ["## 1. Gameplay scripts to port by hand (%d)" % len(groups["port"]), ""]
    for state, rel in groups["port"]:
        refs = port_refs(rel)
        lines.append("- **%s** `%s`" % (state, rel))
        lines.append("  - port: " + ("; ".join(refs[:6]) + (" ..." if len(refs) > 6 else "") if refs
                                     else "not ported yet; check whether the 3DS game uses it"))
    lines += ["", "## 2. Picked up by `./build.sh convert` (%d)" % len(groups["convert"]), ""]
    for state, rel in groups["convert"]:
        lines.append("- %s `%s`" % (state, rel))
    lines += ["", "## 3. Look at by hand (%d)" % len(groups["check"]), ""]
    for state, rel in groups["check"]:
        lines.append("- %s `%s`" % (state, rel))
    lines += ["", "Then follow UPDATING.md from step 3."]
    with open(REPORT, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%s -> %s: %d scripts to port, %d to convert, %d to check; see %s" % (
        old_ver, new_ver, len(groups["port"]), len(groups["convert"]), len(groups["check"]),
        os.path.relpath(REPORT, ROOT)))


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("snapshot", "diff"):
        print(__doc__)
        sys.exit(2)
    (snapshot if sys.argv[1] == "snapshot" else diff)(sys.argv[2])
