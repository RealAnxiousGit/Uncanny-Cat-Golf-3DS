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
    "d_block.tscn": "dblock",
    "key_door.tscn": "keydoor",
    "key.tscn": "key",
    "skull.tscn": "skull",
}
# Areas whose art stays baked into the floor; only their outline goes to the game code.
ZONES = {"sand_pit.tscn": "sand", "water_death.tscn": "water"}
# Objects drawn at runtime from their own baked picture (<id>_d.t3x)
PICTURED = {"dblock", "keydoor", "key", "skull"}
DYN_PX = 1.0  # pixels per world unit in the object pictures
DYN_SKIP = {"ScorePopup", "ColorblindLabel", "editor_sprite", "lock"}
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


def _kind_params(kind, n):
    out = []
    if kind in ("dblock", "keydoor"):
        w, h = S.vec(n.get("size"), (32.0, 32.0))
        out += ["size_x=%s" % _fmt(w), "size_y=%s" % _fmt(h),
                "score_value=%s" % _fmt(int(n.get("score_value", 0) or 0)),
                "dblock_score=%d" % (1 if n.get("dblock_score") else 0)]
    if kind == "keydoor":
        col = S.color(n.get("color"))
        out += ["color_r=%s" % _fmt(col[0]), "color_g=%s" % _fmt(col[1]), "color_b=%s" % _fmt(col[2])]
    if kind == "key":
        col = S.color(n.get("modulate"))
        out += ["color_r=%s" % _fmt(col[0]), "color_g=%s" % _fmt(col[1]), "color_b=%s" % _fmt(col[2]),
                "score_value=%s" % _fmt(int(n.get("score_value", 0) or 0))]
    if kind == "skull":
        d = S.vec(n.get("direction"))
        out += ["speed=%s" % _fmt(float(n.get("speed", 0) or 0)), "dir_x=%s" % _fmt(d[0]), "dir_y=%s" % _fmt(d[1]),
                "score_value=%s" % _fmt(int(n.get("score_value", 0) or 0))]
        pr = S.vec(n.get("death_sound_pitch"), (1.0, 1.0))
        out += ["pitch_lo=%s" % _fmt(pr[0]), "pitch_hi=%s" % _fmt(pr[1])]
    return out


def _render_object(project, tc, root, kind, level_shade):
    """Picture of a dynamic object around its origin (in its own rotation/scale)."""
    ext = 128
    cv = B.Canvas(int(2 * ext * DYN_PX), -ext, -ext, DYN_PX)
    gx = S.global_xform(root)
    to_local = S.invert((1.0, 0.0, gx[2], 0.0, 1.0, gx[5]))
    items = []
    for n in root.walk():
        if n is root:
            continue
        x = n
        skip = False
        while x is not None and x is not root:
            if x.name in DYN_SKIP:
                skip = True
            x = x.parent
        if skip or not S.visible(n):
            continue
        if n.type in ("Sprite2D", "AnimatedSprite2D", "Polygon2D", "NinePatchRect"):
            items.append(n)
    items.sort(key=lambda n: (S.global_z(n), n.index))
    for n in items:
        xf = S.mul(to_local, S.global_xform(n))
        mod = S.global_modulate(n)
        if kind == "skull" and n.type == "AnimatedSprite2D":
            mod = tuple(a * b for a, b in zip(mod, level_shade))  # graphic.modulate = level_shade
        if n.type == "Sprite2D":
            B.draw_sprite(cv, tc, n, xf, n.get("texture"), mod)
        elif n.type == "AnimatedSprite2D":
            B.draw_sprite(cv, tc, n, xf, B.frame_texture(n), mod)
        elif n.type == "Polygon2D":
            B.draw_polygon(cv, tc, n, xf, mod)
        else:
            fn = None
            mat = root.get("shader")
            if kind == "keydoor":
                col = S.color(root.get("color"))
                shades = [S.color(mat.get("shader_parameter/" + k), None) if isinstance(mat, Sub) else None
                          for k in ("HighShade", "MidShade", "LowShade", "ExtraShade")]
                shades[1] = shades[3] = col
                fn = B.colorswap_fn(shades)
            elif isinstance(mat, Sub):
                fn = B.material_fn(mat, tc.unsupported)
            w, h = S.vec(root.get("size"), (32.0, 32.0))
            pxf = S.mul(to_local, S.global_xform(n.parent))
            B.draw_ninepatch(cv, tc, n, pxf, mod, rect=(-w / 2, -h / 2, w, h), colorfn=fn)
    img = cv.image()
    a = np.asarray(img)[..., 3]
    ys, xs = np.nonzero(a)
    if len(xs) == 0:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return img.crop((x0, y0, x1, y1)), (ext * DYN_PX - x0, ext * DYN_PX - y0)


def _render_lock(project, tc, root):
    lock = root.child("lock")
    if lock is None:
        return None
    cv = B.Canvas(128, -64, -64, DYN_PX)
    B.draw_sprite(cv, tc, lock, S.local_xform(lock), lock.get("texture"), (1, 1, 1, 1))
    img = cv.image()
    a = np.asarray(img)[..., 3]
    ys, xs = np.nonzero(a)
    if len(xs) == 0:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return img.crop((x0, y0, x1, y1)), (64 - x0, 64 - y0)


def _zone_polys(n):
    """World-space outline(s) of an Area2D zone."""
    out = []
    for c in n.children:
        if c.type in ("CollisionPolygon2D", "CollisionShape2D"):
            shape_polys(c, S.global_xform(c), 1, out)
    polys = [d for k, _, d in out if k == "poly"]
    for k, _, d in out:
        if k == "circ":
            cx, cy, r = d
            polys.append([(cx + r * math.cos(i * math.pi / 8), cy + r * math.sin(i * math.pi / 8)) for i in range(16)])
    if not polys:
        for c in n.children:
            if c.type == "Polygon2D" and len(c.get("polygon") or []) >= 3:
                xf = S.global_xform(c)
                polys.append([S.apply(xf, p) for p in c.get("polygon")])
    return polys


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
    zones = []
    dyn = []  # (image, anchor) pictures of dynamic objects
    dynamic_roots = set()
    player_index = None
    win_conf = p.get("win_confetti")
    for n in root.walk():
        if n.scene:
            base = os.path.basename(n.scene)
            if base in SKIP_SCENES:
                dynamic_roots.add(id(n))
                continue
            zk = ZONES.get(base)
            if zk:
                for poly in _zone_polys(n):
                    zones.append((zk, poly))
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
                params = _obj_params(n) if kind not in PICTURED else _kind_params(kind, n)
                if kind in PICTURED:
                    pic = _render_object(project, tc, n, kind, S.color(p.get("level_shade")))
                    if pic is not None:
                        params.append("spr=%d" % _dyn_sprite(dyn, pic))
                    if kind == "keydoor":
                        lk = _render_lock(project, tc, n)
                        if lk is not None:
                            params.append("spr2=%d" % _dyn_sprite(dyn, lk))
                objects.append((kind, pos, rot, sx, sy, params, n.get("visible", True) is not False))
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
        if n.type in ("Sprite2D", "AnimatedSprite2D", "Polygon2D", "TileMapLayer", "PointLight2D", "NinePatchRect"):
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
        elif n.type == "NinePatchRect":
            B.draw_ninepatch(target, tc, n, S.global_xform(n.parent), mod)
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
    for zk, poly in zones:
        lines.append("zone %s %d %s" % (zk, len(poly), " ".join("%s %s" % (_fmt(x), _fmt(y)) for x, y in poly)))
    dyn_files = []
    for i, (img, anchor) in enumerate(dyn):
        lines.append("dspr %d %d %d %s %s" % (i, img.size[0], img.size[1], _fmt(float(anchor[0])), _fmt(float(anchor[1]))))
        fp = os.path.join(out_dir, "%s_d%02d.png" % (out_name, i))
        img.save(fp)
        dyn_files.append(fp)
    for kind, pos, rot, sx, sy, params, vis in objects:
        lines.append("obj %s %s %s %s %s %s%s%s" % (kind, _fmt(pos[0]), _fmt(pos[1]), _fmt(rot), _fmt(sx), _fmt(sy),
                                                   "" if vis else " hidden=1",
                                                   (" " + " ".join(params)) if params else ""))
    lines.append("end")
    with open(os.path.join(out_dir, out_name + ".txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return {"id": lid, "bounds": bounds, "shapes": len(shapes), "objects": len(objects), "over": has_over,
            "dyn": dyn_files}


def _dyn_sprite(dyn, pic):
    img, anchor = pic
    key = (img.size, img.tobytes(), round(anchor[0], 2), round(anchor[1], 2))
    for i, (im2, an2) in enumerate(dyn):
        if (im2.size, im2.tobytes(), round(an2[0], 2), round(an2[1], 2)) == key:
            return i
    dyn.append(pic)
    return len(dyn) - 1


report_scripts = set()
