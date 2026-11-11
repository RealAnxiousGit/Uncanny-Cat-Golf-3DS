"""Converts one Godot level scene into the port's level files.

Output for level <id>:
  <id>.txt    level settings, collision shapes and dynamic objects (text, one item per line)
  <id>_u.png  everything drawn under the cat, baked flat
  <id>_o.png  static things drawn over the cat (only written if not empty)
"""
import math
import os

import numpy as np

import bake as B
import scene as S
from scene import Ext, Sub

# Instanced scenes the game code handles itself (drawn and run at runtime).
DYNAMIC = {
    "player.tscn": "player",
    "goal.tscn": "goal",
    "ghost.tscn": "ghost",
    "coin.tscn": "coin",
}
SKIP_SCENES = {"level_hud.tscn"}
SKIP_TYPES = {"Camera2D", "AudioStreamPlayer", "AudioStreamPlayer2D", "AnimationPlayer", "Timer",
              "CollisionShape2D", "CollisionPolygon2D", "Marker2D", "GPUParticles2D", "CPUParticles2D",
              "Label", "RichTextLabel", "Line2D", "TextureButton", "Button", "SubViewport",
              "SubViewportContainer", "Node3D", "MeshInstance3D", "Sprite3D", "Camera3D",
              "VisibleOnScreenNotifier2D", "RemoteTransform2D", "Path2D", "PathFollow2D"}

LEVEL_FLAGS = ("always_O_B", "never_O_B", "mirror_level", "hideScoring", "unskippable",
               "final_ghost_level", "metronome_level")


def _num(v):
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, (int, float)):
        return v
    return None


def _obj_params(node):
    """Simple numeric/bool exported values of an object root, for the game code."""
    out = []
    for k, v in node.props.items():
        if k in ("position", "rotation", "scale", "script", "level", "target", "z_index"):
            continue
        n = _num(v)
        if n is not None and "/" not in k:
            out.append("%s=%s" % (k, _fmt(n)))
    return out


def _fmt(x):
    if isinstance(x, float):
        s = ("%.4f" % x).rstrip("0").rstrip(".")
        return s if s not in ("", "-0") else "0"
    return str(x)


def shape_polys(node, xf, layer_bits, out):
    """Collision shapes of a static body child, as polygons / circles in world space."""
    if node.get("disabled"):
        return
    if node.type == "CollisionPolygon2D":
        pts = node.get("polygon") or []
        if len(pts) >= 3:
            out.append(("poly", layer_bits, [S.apply(xf, p) for p in pts]))
        return
    sh = node.get("shape")
    if not isinstance(sh, Sub):
        return
    t = sh.type
    if t == "RectangleShape2D":
        w, h = S.vec(sh.get("size"), (20.0, 20.0))
        pts = [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]
        out.append(("poly", layer_bits, [S.apply(xf, p) for p in pts]))
    elif t == "CircleShape2D":
        r = float(sh.get("radius", 10.0))
        c = S.apply(xf, (0, 0))
        scale = math.hypot(xf[0], xf[3])
        out.append(("circ", layer_bits, (c[0], c[1], r * scale)))
    elif t in ("ConvexPolygonShape2D", "ConcavePolygonShape2D"):
        pts = sh.get("points") or sh.get("segments") or []
        if len(pts) >= 3:
            out.append(("poly", layer_bits, [S.apply(xf, p) for p in pts]))


def convert_level(project, tc, res_path, out_dir, out_name, report):
    root = S.instantiate(project, res_path)
    p = root.props
    bounds = int(p.get("bounds", 256))
    lid = p.get("ID", out_name)

    # dynamic objects
    objects = []
    dynamic_roots = set()
    player_index = None
    win_conf = p.get("win_confetti")
    for n in root.walk():
        if n.scene:
            base = os.path.basename(n.scene)
            if base in SKIP_SCENES:
                dynamic_roots.add(id(n))
                continue
            kind = DYNAMIC.get(base)
            if kind:
                dynamic_roots.add(id(n))
                xf = S.global_xform(n)
                pos = S.apply(xf, (0, 0))
                rot = math.atan2(xf[3], xf[0])
                sx = math.hypot(xf[0], xf[3])
                sy = math.hypot(xf[1], xf[4])
                if kind == "player":
                    player_index = n.index
                objects.append((kind, pos, rot, sx, sy, _obj_params(n), n.get("visible", True) is not False))
                continue
    if isinstance(win_conf, str) and win_conf:
        wc = root.find(win_conf)
        if wc is not None:
            dynamic_roots.add(id(wc))

    def in_dynamic(n):
        x = n
        while x is not None:
            if id(x) in dynamic_roots:
                return True
            x = x.parent
        return False

    if player_index is None:
        player_index = 1 << 30
        report.append("%s: no player found" % lid)

    # collision + drawables
    shapes = []
    draw_items = []
    modulate_canvas = None
    for n in root.walk():
        if n is root or in_dynamic(n):
            continue
        if n.type == "CanvasLayer" or S.in_canvas_layer(n):
            continue
        if n.type == "TileMapLayer":
            xf = S.global_xform(n)
            if n.get("enabled", True) is not False:
                polys = []
                B.draw_tilemap(None, tc, project, n, xf, (1, 1, 1, 1), collect=polys)
                for bits, pts in polys:
                    shapes.append(("poly", bits, pts))
        if n.type in ("StaticBody2D", "AnimatableBody2D", "CharacterBody2D", "RigidBody2D"):
            bits = int(n.get("collision_layer", 1))
            for c in n.children:
                if c.type in ("CollisionShape2D", "CollisionPolygon2D"):
                    shape_polys(c, S.global_xform(c), bits, shapes)
            if n.type != "StaticBody2D":
                report.append("%s: moving body %s treated as static" % (lid, n.path()))
        if n.type == "CanvasModulate" and S.visible(n):
            modulate_canvas = S.color(n.get("color"))
        if n.type in SKIP_TYPES:
            continue
        if not S.visible(n):
            continue
        if n.type in ("Sprite2D", "AnimatedSprite2D", "Polygon2D", "TileMapLayer", "PointLight2D"):
            draw_items.append(n)
        elif n.scene and not in_dynamic(n):
            pass  # instanced deco: its children are walked and drawn individually
        if n.scene and os.path.basename(n.scene) not in DYNAMIC and n.script is not None and n.scene not in report_scripts:
            report_scripts.add(n.scene)
            report.append("note: scripted scene drawn as static: " + n.scene)

    size = 512 if bounds <= 256 else 1024
    scale = size / (2.0 * bounds)
    under = B.Canvas(size, -bounds, -bounds, scale)
    over = B.Canvas(size, -bounds, -bounds, scale)
    lights = []
    draw_items.sort(key=lambda n: (S.global_z(n), n.index))
    for n in draw_items:
        z = S.global_z(n)
        target = under if (z < 0 or (z == 0 and n.index < player_index)) else over
        xf = S.global_xform(n)
        mod = S.global_modulate(n)
        if n.type == "Sprite2D":
            B.draw_sprite(target, tc, n, xf, n.get("texture"), mod)
        elif n.type == "AnimatedSprite2D":
            B.draw_sprite(target, tc, n, xf, B.frame_texture(n), mod)
        elif n.type == "Polygon2D":
            B.draw_polygon(target, tc, n, xf, mod)
        elif n.type == "TileMapLayer":
            B.draw_tilemap(target, tc, project, n, xf, mod)
        elif n.type == "PointLight2D":
            lights.append((n, xf))
    if modulate_canvas is not None:
        under.multiply(modulate_canvas)
        over.multiply(modulate_canvas)
    for n, xf in lights:
        B.draw_light(under, tc, n, xf)

    os.makedirs(out_dir, exist_ok=True)
    img_u = under.image().convert("RGB")
    img_u.save(os.path.join(out_dir, out_name + "_u.png"))
    has_over = bool((over.px[..., 3] > 0.004).any())
    if has_over:
        over.image().save(os.path.join(out_dir, out_name + "_o.png"))

    shade = S.color(p.get("level_shade"))
    lines = ["ucgl 1",
             "id %s" % lid,
             "author %s" % p.get("lvl_author", ""),
             "bounds %d" % bounds,
             "par %d" % int(p.get("par", 0)),
             "ontime %s" % _fmt(float(p.get("on_time", 0.0))),
             "ranks %s" % " ".join(str(int(x)) for x in (p.get("rank_reqs") or [0, 1, 4500, 5500, 6000, 7500])),
             "music %d" % int(p.get("music", -1)),
             "shade %s" % " ".join(_fmt(c) for c in shade),
             "flags %s" % " ".join(f for f in LEVEL_FLAGS if p.get(f)),
             "image %d %s" % (size, "uo" if has_over else "u")]
    for kind, bits, data in shapes:
        if kind == "poly":
            lines.append("poly %d %d %s" % (bits, len(data), " ".join("%s %s" % (_fmt(x), _fmt(y)) for x, y in data)))
        else:
            lines.append("circ %d %s %s %s" % (bits, _fmt(data[0]), _fmt(data[1]), _fmt(data[2])))
    for kind, pos, rot, sx, sy, params, vis in objects:
        lines.append("obj %s %s %s %s %s %s%s%s" % (kind, _fmt(pos[0]), _fmt(pos[1]), _fmt(rot), _fmt(sx), _fmt(sy),
                                                   "" if vis else " hidden=1",
                                                   (" " + " ".join(params)) if params else ""))
    lines.append("end")
    with open(os.path.join(out_dir, out_name + ".txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return {"id": lid, "bounds": bounds, "shapes": len(shapes), "objects": len(objects), "over": has_over}


report_scripts = set()
