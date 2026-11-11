#!/usr/bin/env python3
"""Fails if any file from the original game is tracked by git.

The repository must only hold our own code: every image, sound, font, level
and text comes from the player's own copy of the game at build time.
"""
import os
import subprocess
import sys

BLOCKED_EXT = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".ogg", ".wav", ".mp3", ".ogv", ".ttf", ".otf",
               ".tscn", ".tres", ".gd", ".gdshader", ".import", ".translation", ".csv", ".t3x", ".bcwav",
               ".glb", ".blend", ".xcf", ".zip"}

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
files = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True, text=True, check=True).stdout.split("\0")
bad = [f for f in files if f and os.path.splitext(f)[1].lower() in BLOCKED_EXT]
if bad:
    print("Game files must not be in the repository:")
    for f in bad:
        print("  " + f)
    sys.exit(1)
print("ok: %d tracked files, no game assets" % len([f for f in files if f]))
