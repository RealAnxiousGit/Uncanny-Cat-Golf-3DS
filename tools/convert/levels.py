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
    # World 2
    "speed_token.tscn": "token",
    "stop_token.tscn": "token",
    "jump_pad.tscn": "jumppad",
    "baddie_spawner.tscn": "spawner",
    "car.tscn": "car",
    # World 3
    "chuckle.tscn": "skull",  # baddie.gd too, as a chaser
    "switch_block.tscn": "switch",
    "dog_golf.tscn": "dog",
    "dog_goal.tscn": "doggoal",
    "smiley_ring.tscn": "ring",
    "smiley_coin.tscn": "scoin",
    # World 4
    "portal.tscn": "portal",
    "nuke.tscn": "nuke",
    "fake_goal.tscn": "fakegoal",
    "final_ghost.tscn": "fghost",
    "prism_gate.tscn": "prismgate",
    # World 5
    "horse_boss.tscn": "horse",
    "horse_goal.tscn": "hgoal",
    "land_mine.tscn": "mine",
    "wizard_ghost.tscn": "wizard",
}
CAR_SCENE = "res://assetsNEW/scenes/objects/baddies/car.tscn"
SPAWNER_SCENE = "res://assetsNEW/scenes/objects/baddies/baddie_spawner.tscn"
SKULL_SCENE = "res://assetsNEW/scenes/objects/baddies/skull.tscn"
CHUCKLE_SCENE = "res://assetsNEW/scenes/objects/baddies/chuckle.tscn"
# Areas whose art stays baked into the floor; only their outline goes to the game code.
ZONES = {"sand_pit.tscn": "sand", "water_death.tscn": "water"}
# Objects drawn at runtime from their own baked picture (<id>_d.t3x)
PICTURED = {"dblock", "keydoor", "key", "skull", "token", "jumppad", "spawner", "car",
            "switch", "dog", "doggoal", "ring", "scoin", "portal", "nuke", "fakegoal", "prismgate",
            "horse", "hgoal", "mine", "wizard"}
# the node that gets graphic.modulate = level.level_shade in each object's _ready()
SHADED = {"skull": "AnimatedSprite2D", "car": "Sprite2D", "token": "AnimatedSprite2D", "spawner": "Door",
          "jumppad": "Sprite2D", "dog": "Sprite2D", "doggoal": "AnimatedSprite2D", "ring": "AnimatedSprite2D",
          "horse": "Sprite2D"}
DYN_PX = 1.0  # pixels per world unit in the object pictures
DYN_SKIP = {"ScorePopup", "ColorblindLabel", "editor_sprite", "lock"}
# Secret keys (secret_key_trigger.gd / secret_key_hint.gd) free themselves until the key quest reaches step 2
# (Master.save_data.QUEST_PROGRESS); the quest is not ported yet, so they are left out like in a fresh save.
SKIP_SCENES = {"level_hud.tscn", "secret_key_trigger.tscn", "secret_key_hint.tscn"}
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


def _enemy_kind(ets):
    """What a spawner's enemy_to_spawn scene is: 0 car, 1 skull, 2 chuckle (baddie_spawner.gd editorEnemies)."""
    s = str(getattr(ets, "path", ets)) if ets is not None else ""
    return 2 if "chuckle" in s else (1 if "skull" in s else 0)


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
    if kind == "token":  # collectible.gd with does_speed
        out += ["score_value=%s" % _fmt(int(n.get("score_value", 0) or 0)),
                "does_speed=%d" % (1 if n.get("does_speed") else 0),
                "set_speed=%s" % _fmt(float(n.get("set_speed", 0) or 0)),
                "coin_multiply=%d" % (1 if n.get("coin_multiply") else 0),
                "coin_multiply_value=%s" % _fmt(int(n.get("coin_multiply_value", 0) or 0)),
                "radius=%s" % _fmt(_circle_radius(n, 16.0))]
    if kind == "jumppad":  # jump_pad.gd
        out += ["direction=%s" % _fmt(float(n.get("direction", 0) or 0)),
                "speed_granted=%s" % _fmt(float(n.get("speed_granted", 0) or 0)),
                "radius=%s" % _fmt(_circle_radius(n, 16.0))]
    if kind == "spawner":  # baddie_spawner.gd
        d = S.vec(n.get("b_direction"))
        out += ["enemy=%d" % _enemy_kind(n.get("enemy_to_spawn")),
                "spawn_rate=%d" % int(n.get("spawn_rate", 0) or 0),
                "spawn_countdown=%d" % int(n.get("spawn_countdown", 0) or 0),
                "max_baddies=%d" % int(n.get("max_baddies", 0) or 0),
                "dir_x=%s" % _fmt(d[0]), "dir_y=%s" % _fmt(d[1]),
                "speed=%s" % _fmt(float(n.get("b_speed", 0) or 0)),
                "score_value=%s" % _fmt(int(n.get("b_score_value", 0) or 0)),
                "rotate=%s" % _fmt(float(n.get("b_rotate", 0) or 0)),
                "ignorey=%d" % (1 if n.get("b_ignorey_caronly") else 0)]
    if kind == "car":  # baddie.gd
        d = S.vec(n.get("direction"))
        out += ["speed=%s" % _fmt(float(n.get("speed", 0) or 0)), "dir_x=%s" % _fmt(d[0]), "dir_y=%s" % _fmt(d[1]),
                "score_value=%s" % _fmt(int(n.get("score_value", 0) or 0))]
    if kind == "skull":
        d = S.vec(n.get("direction"))
        out += ["speed=%s" % _fmt(float(n.get("speed", 0) or 0)), "dir_x=%s" % _fmt(d[0]), "dir_y=%s" % _fmt(d[1]),
                "score_value=%s" % _fmt(int(n.get("score_value", 0) or 0))]
        pr = S.vec(n.get("death_sound_pitch"), (1.0, 1.0))
        out += ["pitch_lo=%s" % _fmt(pr[0]), "pitch_hi=%s" % _fmt(pr[1])]
        if n.get("chaser"):  # chuckle.tscn: chases the cat
            out += ["chaser=1", "chase_accel=%s" % _fmt(float(n.get("chase_accel", 4.0) or 0)),
                    "chase_limit=%s" % _fmt(float(n.get("chase_limit", 200.0) or 0))]
        if n.get("do_thought") == "Chuckle":
            out.append("chuckle=1")
    if kind == "switch":  # switch_block.gd (block.gd size)
        w, h = S.vec(n.get("size"), (32.0, 32.0))
        out += ["size_x=%s" % _fmt(w), "size_y=%s" % _fmt(h), "keep_face=%d" % (1 if n.get("keep_face") else 0)]
    if kind == "dog":  # dog_golf.gd
        d = S.vec(n.get("direction"))
        out += ["speed=%s" % _fmt(float(n.get("speed", 0) or 0)), "dir_x=%s" % _fmt(d[0]), "dir_y=%s" % _fmt(d[1]),
                "bounce=%d" % (0 if n.get("bounce_off_player") is False else 1),
                "radius=%s" % _fmt(_circle_radius(n, 15.8))]
    if kind == "doggoal":
        out.append("radius=%s" % _fmt(_circle_radius(n, 16.0)))
    if kind == "ring":  # smiley_ring.gd
        col = S.color(n.get("color"))
        out += ["time_given=%s" % _fmt(float(n.get("time_given", 0) or 0)),
                "score_value=%s" % _fmt(int(n.get("score_value", 0) or 0)),
                "ring_bell=%d" % (0 if n.get("ring_bell") is False else 1),
                "redoable=%d" % (1 if n.get("redoable") else 0),
                "radius=%s" % _fmt(_circle_radius(n, 24.0)),
                "color_r=%s" % _fmt(col[0]), "color_g=%s" % _fmt(col[1]), "color_b=%s" % _fmt(col[2])]
    if kind == "scoin":
        out.append("radius=%s" % _fmt(_circle_radius(n, 10.0)))
    if kind == "portal":  # portal.tscn: a 16x16 RectangleShape2D
        w, h = _rect_size(n, (16.0, 16.0))
        out += ["size_x=%s" % _fmt(w), "size_y=%s" % _fmt(h)]
    if kind == "nuke":  # nuke.gd (a collectible)
        out += ["score_value=%s" % _fmt(int(n.get("score_value", 0) or 0)),
                "radius=%s" % _fmt(_circle_radius(n, 16.0)),
                "spawners=%d" % (1 if n.get("spawner_folder") else 0)]
    if kind == "fakegoal":  # fake_goal.gd
        sv = n.get("score_value")
        out += ["score_value=%s" % _fmt(int(500 if sv is None else sv)),
                "radius=%s" % _fmt(_circle_radius(n, 16.0)),
                "ghost=%d" % (1 if n.get("final_ghost") else 0),
                "alt=%d" % (1 if (n.get("objects_to_show") or n.get("canvas")) else 0)]
    if kind == "horse":  # horse_boss.gd
        d = S.vec(n.get("direction"))
        out += ["health=%d" % int(n.get("health", 10) or 0),
                "base_speed=%s" % _fmt(float(n.get("BASE_SPEED", 0) or 0)),
                "attack_time=%d" % int(n.get("attack_time", 0) or 0),
                "target_delay=%d" % int(n.get("target_delay", 0) or 0),
                "dir_x=%s" % _fmt(d[0]), "dir_y=%s" % _fmt(d[1]),
                "end_of_life=%d" % (1 if n.get("end_of_life") else 0)]
    if kind == "hgoal":  # horse_goal.gd
        out += ["active=%d" % (1 if n.get("active") else 0)]
    if kind == "mine":  # land_mine.gd: its Detector
        r = 16.0
        det = n.child("Detector")
        if det is not None:
            r = _circle_radius(det, 16.0)
        out.append("radius=%s" % _fmt(r))
    if kind == "prismgate":  # prism_gate.gd (block.gd size)
        w, h = S.vec(n.get("size"), (64.0, 64.0))
        out += ["size_x=%s" % _fmt(w), "size_y=%s" % _fmt(h),
                "prisms=%d" % int(n.get("prism_number", 0) or 0)]
    return out


def _rect_size(n, default):
    """Size of an Area2D's RectangleShape2D, in the object's own units."""
    for c in n.children:
        if c.type == "CollisionShape2D":
            sh = c.get("shape")
            if isinstance(sh, Sub) and sh.type == "RectangleShape2D":
                w, h = S.vec(sh.get("size"), default)
                sc = S.vec(c.get("scale"), (1.0, 1.0))
                return w * abs(sc[0]), h * abs(sc[1])
    return default


def _circle_radius(n, default):
    """Radius of an Area2D's CircleShape2D, in the object's own units (the root's scale is applied later)."""
    for c in n.children:
        if c.type == "CollisionShape2D":
            sh = c.get("shape")
            if isinstance(sh, Sub) and sh.type == "CircleShape2D":
                return float(sh.get("radius", default)) * abs(S.vec(c.get("scale"), (1.0, 1.0))[0])
    return default


def _prepare_object(kind, root, rotate_deg=None):
    """What each object's _ready() changes on its looks before the first frame."""
    if kind == "jumppad":  # jump_pad.gd: the arrow points along direction; frame 1 when it sets a speed
        arrow = root.child("arrow")
        if arrow is not None:
            arrow.props["rotation"] = float(root.get("direction", 0) or 0) + 0.5 * math.pi
            if float(root.get("speed_granted", 0) or 0) > 20.0:  # Player.STOP_SPEED
                arrow.props["frame"] = 1
    if kind == "hgoal":  # horse_goal.gd activate(): the sprite shows once the goal is active
        spr = root.child("AnimatedSprite2D")
        if spr is not None:
            spr.props["visible"] = True
    if kind == "switch" and not root.get("keep_face"):  # switch_block.gd change_face(): frame = switchState (0)
        spr = root.child("Sprite2D")
        if spr is not None:
            spr.props["frame"] = 0
    if kind in ("car", "skull"):  # baddie.gd: graphic.rotation_degrees = rotate
        deg = rotate_deg if rotate_deg is not None else float(root.get("rotate", 0) or 0)
        g = root.child("Sprite2D") or root.child("AnimatedSprite2D")
        if g is not None and deg:
            g.props["rotation"] = math.radians(deg)


def _resolve(n, path):
    """A NodePath relative to node n ("../../Collectibles/StopToken")."""
    x = n
    for part in str(path).split("/"):
        if not part or part == ".":
            continue
        x = x.parent if part == ".." else (x.child(part) if x is not None else None)
        if x is None:
            return None
    return x


def _curve_points(curve):
    """A Curve2D baked into a polyline with the distance along it (Godot's PathFollow2D progress)."""
    data = curve.get("_data") if isinstance(curve, Sub) else None
    raw = (data or {}).get("points") or []
    vals = []
    for v in raw:
        if isinstance(v, (int, float)):
            vals.append(float(v))
        elif isinstance(v, (tuple, list)):
            vals.extend(float(c) for c in v[:2])
        else:
            vals.extend(S.vec(v))
    pts = [(vals[i], vals[i + 1], vals[i + 2], vals[i + 3], vals[i + 4], vals[i + 5]) for i in range(0, len(vals) - 5, 6)]
    poly = []
    for i, (ix, iy, ox, oy, px, py) in enumerate(pts):
        if i == 0:
            poly.append((px, py))
            continue
        _ix0, _iy0, ox0, oy0, px0, py0 = pts[i - 1]
        a, b = (px0, py0), (px0 + ox0, py0 + oy0)
        c, d = (px + ix, py + iy), (px, py)
        for k in range(1, 33):
            t = k / 32.0
            u = 1 - t
            poly.append((u * u * u * a[0] + 3 * u * u * t * b[0] + 3 * u * t * t * c[0] + t * t * t * d[0],
                         u * u * u * a[1] + 3 * u * u * t * b[1] + 3 * u * t * t * c[1] + t * t * t * d[1]))
    dist = [0.0]
    for i in range(1, len(poly)):
        dist.append(dist[-1] + math.hypot(poly[i][0] - poly[i - 1][0], poly[i][1] - poly[i - 1][1]))
    return poly, dist


def _curve_at(poly, dist, s):
    if not poly:
        return (0.0, 0.0)
    s = max(0.0, min(s, dist[-1]))
    for i in range(1, len(poly)):
        if dist[i] >= s:
            f = (s - dist[i - 1]) / max(1e-6, dist[i] - dist[i - 1])
            return (poly[i - 1][0] + (poly[i][0] - poly[i - 1][0]) * f, poly[i - 1][1] + (poly[i][1] - poly[i - 1][1]) * f)
    return poly[-1]


def _key_value(keys, t):
    """An animation value track at time t (linear between keys, or the last key for update = 1)."""
    times = list(keys.get("times") or [])
    values = list(keys.get("values") or [])
    if not times:
        return None
    if t <= times[0]:
        return values[0]
    for i in range(1, len(times)):
        if t < times[i]:
            if keys.get("update") == 1:
                return values[i - 1]
            f = (t - times[i - 1]) / max(1e-6, times[i] - times[i - 1])
            a, b = values[i - 1], values[i]
            if isinstance(a, (int, float)):
                return a + (b - a) * f
            av, bv = S.vec(a), S.vec(b)
            return (av[0] + (bv[0] - av[0]) * f, av[1] + (bv[1] - av[1]) * f)
    return values[-1]


def level_tracks(root, goal_node, player_node):
    """Autoplaying animations that move the game: wind (the player's additional_velocity) and a goal
    carried along a path (PathFollow2D progress). Returns ["track <kind> <len> <loop> <n> t x y ..."]."""
    out = []
    for n in root.walk():
        if n.type != "AnimationPlayer" or not n.get("autoplay"):
            continue
        name = str(n.get("autoplay"))
        anim = None
        for lib in (n.get("libraries") or {}).values():
            data = lib.get("_data") if isinstance(lib, Sub) else None
            if data and name in data:
                anim = data[name]
        if not isinstance(anim, Sub):
            continue
        length = float(anim.get("length", 1.0) or 1.0)
        loop = 1 if anim.get("loop_mode") else 0
        base = n.parent if n.get("root_node") is None else _resolve(n, n.get("root_node"))
        i = 0
        while anim.get("tracks/%d/path" % i) is not None:
            path = str(anim.get("tracks/%d/path" % i))
            keys = anim.get("tracks/%d/keys" % i) or {}
            i += 1
            if anim.get("tracks/%d/type" % (i - 1)) not in (None, "value") or ":" not in path:
                continue
            node_path, prop = path.rsplit(":", 1)
            target = _resolve(base, node_path) if base is not None else None
            if target is None:
                continue
            if prop == "additional_velocity" and target is player_node:
                ks = []
                for t, v in zip(keys.get("times") or [], keys.get("values") or []):
                    x, y = S.vec(v)
                    ks.append("%s %s %s" % (_fmt(float(t)), _fmt(x), _fmt(y)))
                out.append("track wind %s %d 1 %d %s" % (_fmt(length), loop, len(ks), " ".join(ks)))
            if prop == "progress" and target.type == "PathFollow2D" and goal_node is not None:
                x = goal_node
                while x is not None and x is not target:
                    x = x.parent
                if x is not target:
                    continue
                poly, dist = _curve_points(target.parent.get("curve") if target.parent is not None else None)
                if not poly:
                    continue
                gx = S.global_xform(target.parent)
                off = S.vec(goal_node.get("position"), (0.0, 0.0))
                ks = []
                steps = max(2, int(length * 10))
                for k in range(steps + 1):
                    t = length * k / steps
                    s = _key_value(keys, t)
                    if s is None:
                        continue
                    px, py = _curve_at(poly, dist, float(s))
                    wx, wy = S.apply(gx, (px + off[0], py + off[1]))
                    ks.append("%s %s %s" % (_fmt(t), _fmt(wx), _fmt(wy)))
                out.append("track goal %s %d 0 %d %s" % (_fmt(length), loop, len(ks), " ".join(ks)))
    return out


def render_slot_frames(project, tc, root):
    """slot.tscn: the reel picture (slots.png, 10 frames) with its screen overlay, one picture per frame."""
    pics = []
    gx = S.global_xform(root)
    to_local = S.invert((1.0, 0.0, gx[2], 0.0, 1.0, gx[5]))
    for fr in range(int(root.get("hframes", 10) or 10)):
        root.props["frame"] = fr
        cv = B.Canvas(256, -128, -128, DYN_PX)
        B.draw_sprite(cv, tc, root, S.mul(to_local, gx), root.get("texture"), S.global_modulate(root))
        for c in root.walk():
            if c is not root and c.type == "Polygon2D" and S.visible(c):
                B.draw_polygon(cv, tc, c, S.mul(to_local, S.global_xform(c)), S.global_modulate(c))
        img = cv.image()
        a = np.asarray(img)[..., 3]
        ys, xs = np.nonzero(a)
        if len(xs) == 0:
            return []
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        pics.append((img.crop((x0, y0, x1, y1)), (128 - x0, 128 - y0)))
    root.props["frame"] = 0
    return pics


def render_spawned(project, tc, kind, rotate_deg, level_shade):
    """Picture of the car or skull a spawner makes (its scene, turned by b_rotate)."""
    root = S.instantiate(project, {"skull": SKULL_SCENE, "chuckle": CHUCKLE_SCENE}.get(kind, CAR_SCENE))
    kind = "skull" if kind == "chuckle" else kind
    _prepare_object(kind, root, rotate_deg)
    return _render_object(project, tc, root, kind, level_shade)


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
            if x.name in DYN_SKIP and not (kind == "prismgate" and x.name == "lock"):
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
        if SHADED.get(kind) == n.name and n.parent is root:
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
    for b in sorted(S.BINARY_SKIPPED):
        report.append("note: binary scene left out: " + b)
    S.BINARY_SKIPPED.clear()
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
    # tile layers the game turns on and off: level.gd switch_tiles (switch blocks) and dog_goal.gd dog_tiles
    tile_groups = {}  # id(layer) -> (group, role 0 switch / 1 dog, switch index, layer)
    goal_tiles = {}   # id(dog goal) -> id(its layer)
    for i, path in enumerate(p.get("switch_tiles") or []):
        t = root.find(str(path))
        if t is not None and id(t) not in tile_groups:
            tile_groups[id(t)] = (len(tile_groups) + 1, 0, i, t)
    rings = []
    for n in root.walk():
        base = os.path.basename(n.scene) if n.scene else ""
        if base == "dog_goal.tscn":
            t = _resolve(n, n.get("dog_tiles")) if n.get("dog_tiles") else root.child("dogtiles")
            if t is not None:
                if id(t) not in tile_groups:
                    tile_groups[id(t)] = (len(tile_groups) + 1, 1, 0, t)
                goal_tiles[id(n)] = id(t)
        if base == "smiley_ring.tscn":
            rings.append(n)
    # smiley_ring.gd _get_coins(): the SmileyCoins under c_parent, or same-coloured siblings
    coin_ring = {}
    for ri, r in enumerate(rings):
        cp = _resolve(r, r.get("c_parent")) if r.get("c_parent") else None
        if cp is not None:
            members = [c for c in cp.children if c.scene and os.path.basename(c.scene) == "smiley_coin.tscn"]
        else:
            col = S.color(r.get("color"))
            members = [c for c in (r.parent.children if r.parent else [])
                       if c.scene and os.path.basename(c.scene) == "smiley_coin.tscn"
                       and all(abs(a - b) < 1e-3 for a, b in zip(S.color(c.get("modulate")), col))]
        for c in members:
            coin_ring.setdefault(id(c), ri)
    # fake_goal.gd: tiles_to_remove goes off (role 2), objects_to_show appear and the CanvasModulate takes
    # new_color: the course is baked again that way (<id>_u2 / _o2), shown once the fake goal is hit
    alt_shown = []
    alt_canvas = None
    easy_only = []  # easy_mode_ending.gd objects_to_add: freed outside easy mode
    metronomes = []  # (node, [entries]) in scene order
    extra_flags = []
    for n in root.walk():
        base = os.path.basename(n.scene) if n.scene else ""
        if n.script is not None and str(getattr(n.script, "path", n.script)).endswith("santa.gd"):
            if "santa_scroll" not in extra_flags:
                extra_flags.append("santa_scroll")
        if base == "golden_tee.tscn":
            if "golden_tee" not in extra_flags:
                extra_flags.append("golden_tee")
            dynamic_roots.add(id(n))
        if base == "metronome.tscn":
            m = len(metronomes)
            entries = [_resolve(n, path) for path in (n.get("TriggerList") or [])]
            for i, t in enumerate(entries):
                if n.get("tile") and t is not None and t.type == "TileMapLayer" and id(t) not in tile_groups:
                    tile_groups[id(t)] = (len(tile_groups) + 1, 3, m * 16 + i, t)
            metronomes.append((n, entries))
        if base == "fake_goal.tscn":
            if n.get("tiles_to_remove"):
                t = _resolve(n, n.get("tiles_to_remove"))
                if t is not None and id(t) not in tile_groups:
                    tile_groups[id(t)] = (len(tile_groups) + 1, 2, 0, t)
            for path in n.get("objects_to_show") or []:
                t = _resolve(n, path)
                if t is not None:
                    alt_shown.append(t)
            if n.get("canvas"):
                alt_canvas = S.color(n.get("new_color"))
        if n.script is not None and str(getattr(n.script, "path", n.script)).endswith("easy_mode_ending.gd"):
            for path in n.get("objects_to_add") or []:
                t = _resolve(n, path)
                if t is not None:
                    easy_only.append(t)
    goal_node = player_node = None
    for n in root.walk():
        base = os.path.basename(n.scene) if n.scene else ""
        if base == "goal.tscn" and goal_node is None:
            goal_node = n
        if base == "player.tscn" and player_node is None:
            player_node = n
    tracks = level_tracks(root, goal_node, player_node)
    lights_hidden = set()  # lights_out_event.gd: what goes dark (baked again without it: <id>_u2 / _o2)
    node_of = []  # the scene node behind each object, to link slots to their tokens
    slot_links = []  # (object index, token node)
    portal_links = []  # (object index, target portal node)
    hgoal_links = []  # (object index, next_goal node)
    for t in easy_only:
        dynamic_roots.add(id(t))
    for n in root.walk():
        if n.scene:
            base = os.path.basename(n.scene)
            if base == "slot.tscn":  # slot.gd: spins until its collect_connect token is taken
                dynamic_roots.add(id(n))
                frames = render_slot_frames(project, tc, n)
                first = -1
                for pic in frames:  # kept in a row, so frame f is spr + f
                    dyn.append(pic)
                    if first < 0:
                        first = len(dyn) - 1
                pos = S.apply(S.global_xform(n), (0, 0))
                slot_links.append((len(objects), _resolve(n, n.get("collect_connect", ""))))
                objects.append(("slot", pos, 0.0, 1.0, 1.0,
                                ["rate=%d" % int(n.get("rate", 10) or 10), "spr=%d" % first,
                                 "frames=%d" % len(frames)], True))
                node_of.append(n)
                continue
            if base == "slot_machine.tscn":  # slot_machine.gd: baked as it stands; the game code scores the reels
                pos = S.apply(S.global_xform(n), (0, 0))
                objects.append(("slotmachine", pos, 0.0, 1.0, 1.0,
                                ["skull_num=%d" % int(n.get("skull_num", 30) or 30)], True))
                node_of.append(n)
            if base in SKIP_SCENES:
                dynamic_roots.add(id(n))
                continue
            if base == "police_event.tscn":  # police_event.gd: entering the area starts a spawner (or one car)
                dynamic_roots.add(id(n))
                for poly in _zone_polys(n):
                    zones.append(("police", poly))
                d = S.vec(n.get("b_direction"))
                ep = S.vec(n.get("e_position"))
                shade = S.color(p.get("level_shade"))
                params = ["has_spawner=%d" % (1 if n.get("spawner") is not None else 0),
                          "score_value=%s" % _fmt(int(n.get("score_value", 0) or 0)),
                          "spawner_value=%s" % _fmt(int(n.get("spawner_value", 0) or 0)),
                          "spawn_rate=%d" % int(n.get("spawn_rate", 0) or 0),
                          "spawn_countdown=%d" % int(n.get("spawn_countdown", 0) or 0),
                          "max_baddies=%d" % int(n.get("max_baddies", 0) or 0),
                          "dir_x=%s" % _fmt(d[0]), "dir_y=%s" % _fmt(d[1]),
                          "speed=%s" % _fmt(float(n.get("b_speed", 0) or 0)),
                          "b_score_value=%s" % _fmt(int(n.get("b_score_value", 0) or 0)),
                          "rotate=%s" % _fmt(float(n.get("b_rotate", 0) or 0)),
                          "e_x=%s" % _fmt(ep[0]), "e_y=%s" % _fmt(ep[1])]
                ei = _enemy_kind(n.get("enemy_to_spawn"))
                ek = ("car", "skull", "chuckle")[ei]
                params.append("enemy=%d" % ei)
                cp = render_spawned(project, tc, ek, float(n.get("b_rotate", 0) or 0), shade)
                if cp is not None:
                    params.append("spr2=%d" % _dyn_sprite(dyn, cp))
                door = _render_object(project, tc, S.instantiate(project, SPAWNER_SCENE), "spawner", shade)
                if door is not None:
                    params.append("spr=%d" % _dyn_sprite(dyn, door))
                objects.append(("police", (0.0, 0.0), 0.0, 1.0, 1.0, params, True))
                node_of.append(n)
                continue
            if base == "metronome.tscn":  # metronome.gd: beats from the music; its look is a screen overlay
                dynamic_roots.add(id(n))
                m = [mm for mm, (mn, _e) in enumerate(metronomes) if mn is n][0]
                snd = n.get("playTriggerSound")
                sp = str(getattr(snd, "path", "") or "")
                sk = {"rhythm_block.ogg": 1, "polyrhythm_block.ogg": 2}.get(os.path.basename(sp), 0)
                vis = n.get("Visual")
                objects.append(("metronome", (0.0, 0.0), 0.0, 1.0, 1.0,
                                ["m=%d" % m, "every=%d" % int(n.get("TriggerEvery", 2) or 2),
                                 "visual=%d" % (0 if vis is False else 1),
                                 "n=%d" % len(metronomes[m][1]), "tile=%d" % (1 if n.get("tile") else 0),
                                 "snd=%d" % sk,
                                 "vol=%s" % _fmt(10 ** (float(n.get("triggerSoundVolume", 0) or 0) / 20.0)),
                                 "pitch=%s" % _fmt(float(n.get("triggerSoundPitch", 1.0) or 1.0))], True))
                node_of.append(n)
                continue
            if base == "boss_appear_event.tscn":
                dynamic_roots.add(id(n))
                for poly in _zone_polys(n):
                    zones.append(("bossappear", poly))
                objects.append(("bossappear", S.apply(S.global_xform(n), (0, 0)), 0.0, 1.0, 1.0, [], True))
                node_of.append(n)
                continue
            if base == "coaster_starter.tscn":  # 3-18 coaster_starter.gd + coaster_oncoming.gd
                dynamic_roots.add(id(n))
                for poly in _zone_polys(n):
                    zones.append(("coaster", poly))
                onc = root.find("Gimmicks/Oncoming")
                oy = S.apply(S.global_xform(onc), (0, 0))[1] if onc is not None else 80.0
                sy_list = [S.apply(S.global_xform(c), (0, 0))[1] for c in (onc.children if onc else [])]
                while len(sy_list) < 4:
                    sy_list.append(oy)
                cparams = [
                    "coins_to_exit=%d" % int(n.get("coins_to_exit", 25) or 25),
                    "speed=%s" % _fmt(float(onc.get("speed", -800.0) if onc else -800.0)),
                    "spawn_offset=%s" % _fmt(float(onc.get("spawn_offset", 1200.0) if onc else 1200.0)),
                    "screen_edge=%s" % _fmt(float(onc.get("screen_edge", 320.0) if onc else 320.0)),
                    "kill_edge=%s" % _fmt(float(onc.get("kill_edge", -400.0) if onc else -400.0)),
                    "slot0=%s" % _fmt(sy_list[0]), "slot1=%s" % _fmt(sy_list[1]),
                    "slot2=%s" % _fmt(sy_list[2]), "slot3=%s" % _fmt(sy_list[3]),
                ]
                objects.append(("coaster", S.apply(S.global_xform(n), (0, 0)), 0.0, 1.0, 1.0, cparams, True))
                node_of.append(n)
                continue
            if base in ("lights_out_event.tscn", "ash_baby_event.tscn"):  # CustomEvent areas, once each
                dynamic_roots.add(id(n))
                ek = "lights" if base.startswith("lights") else "ashbaby"
                for poly in _zone_polys(n):
                    zones.append((ek, poly))
                params = ["score_value=%d" % int(n.get("score_value", 0) or 0)]
                if ek == "lights":
                    for path in [n.get("affected_tiles")] + list(n.get("affected_deco") or []):
                        t = _resolve(n, path) if path else None
                        if t is not None:
                            lights_hidden.add(id(t))
                    params.append("goal=%d" % (1 if n.get("affected_goal") else 0))
                objects.append((ek, (0.0, 0.0), 0.0, 1.0, 1.0, params, True))
                node_of.append(n)
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
                    _prepare_object(kind, n)
                    pic = _render_object(project, tc, n, kind, S.color(p.get("level_shade")))
                    if pic is not None:
                        params.append("spr=%d" % _dyn_sprite(dyn, pic))
                    if kind == "spawner":  # what it spawns, turned the way it drives
                        ek = ("car", "skull", "chuckle")[_enemy_kind(n.get("enemy_to_spawn"))]
                        cp = render_spawned(project, tc, ek, float(n.get("b_rotate", 0) or 0),
                                            S.color(p.get("level_shade")))
                        if cp is not None:
                            params.append("spr2=%d" % _dyn_sprite(dyn, cp))
                    if kind == "switch" and not n.get("keep_face"):  # the face for switchState 1
                        sp = n.child("Sprite2D")
                        if sp is not None:
                            sp.props["frame"] = 1
                            pic2 = _render_object(project, tc, n, kind, S.color(p.get("level_shade")))
                            sp.props["frame"] = 0
                            if pic2 is not None:
                                params.append("spr2=%d" % _dyn_sprite(dyn, pic2))
                    if kind == "scoin":
                        params.append("ring=%d" % coin_ring.get(id(n), -1))
                    if kind == "hgoal":
                        hgoal_links.append((len(objects), _resolve(n, n.get("next_goal")) if n.get("next_goal") else None))
                    if kind == "portal":
                        portal_links.append((len(objects), _resolve(n, n.get("portal_target"))
                                             if n.get("portal_target") else None))
                    if kind == "fakegoal":  # after it is hit: the hole without its flag
                        fl = n.child("Flag")
                        if fl is not None:
                            keep = fl.props.get("visible")
                            fl.props["visible"] = False
                            pic2 = _render_object(project, tc, n, kind, S.color(p.get("level_shade")))
                            if keep is None:
                                del fl.props["visible"]
                            else:
                                fl.props["visible"] = keep
                            if pic2 is not None:
                                params.append("spr2=%d" % _dyn_sprite(dyn, pic2))
                        tg = None
                        if n.get("tiles_to_remove"):
                            t = _resolve(n, n.get("tiles_to_remove"))
                            tg = tile_groups.get(id(t)) if t is not None else None
                        params.append("group=%d" % (tg[0] if tg else 0))
                    if kind == "doggoal":
                        tg = tile_groups.get(goal_tiles.get(id(n)))
                        params.append("group=%d" % (tg[0] if tg else 0))
                    if kind == "keydoor":
                        lk = _render_lock(project, tc, n)
                        if lk is not None:
                            params.append("spr2=%d" % _dyn_sprite(dyn, lk))
                objects.append((kind, pos, rot, sx, sy, params, n.get("visible", True) is not False))
                node_of.append(n)
                continue
    # slot -> the index of its token among the level's coins and tokens (the game's coin list order)
    coin_index = {}
    for i, (kind, *_rest) in enumerate(objects):
        if kind in ("coin", "token", "nuke"):
            coin_index[id(node_of[i])] = len(coin_index)
    # portal -> its target's index among the level's portals
    portal_index = {}
    for i, (kind, *_rest) in enumerate(objects):
        if kind == "portal":
            portal_index[id(node_of[i])] = len(portal_index)
    for m, (mn, entries) in enumerate(metronomes):
        if mn.get("tile"):
            continue
        for i, t in enumerate(entries):
            if t is None:
                continue
            for oi, (kind, *_rest) in enumerate(objects):
                if kind != "ghost":
                    continue
                x = node_of[oi]
                while x is not None and x is not t:
                    x = x.parent
                if x is t:
                    objects[oi][5].extend(["met=%d" % m, "mslot=%d" % i])
    hgoal_index = {}
    for i, (kind, *_rest) in enumerate(objects):
        if kind == "hgoal":
            hgoal_index[id(node_of[i])] = len(hgoal_index)
    for oi, tgt in hgoal_links:
        objects[oi][5].append("next=%d" % hgoal_index.get(id(tgt), -1) if tgt is not None else "next=-1")
    for oi, tgt in portal_links:
        objects[oi][5].append("target=%d" % portal_index.get(id(tgt), -1) if tgt is not None else "target=-1")
    for oi, tok in slot_links:
        objects[oi][5].append("coin=%d" % coin_index.get(id(tok), -1) if tok is not None else "coin=-1")
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
    modulate_node = None
    shapes = []
    draw_items = []
    modulate_canvas = None
    for n in root.walk():
        if n is root or in_dynamic(n):
            continue
        if n.type == "CanvasLayer" or S.in_canvas_layer(n):
            continue
        if n.type == "TileMapLayer" and id(n) in tile_groups:
            polys = []
            B.draw_tilemap(None, tc, project, n, S.global_xform(n), (1, 1, 1, 1), collect=polys)
            for bits, pts in polys:
                shapes.append(("gpoly", bits, (tile_groups[id(n)][0], pts)))
            continue  # drawn on its own below
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
            modulate_node = n
        if n.type in SKIP_TYPES:
            continue
        if not S.visible(n):
            continue
        if n.type in ("Sprite2D", "AnimatedSprite2D", "Polygon2D", "TileMapLayer", "PointLight2D", "NinePatchRect") \
                and id(n) not in tile_groups:
            draw_items.append(n)
        elif n.scene and not in_dynamic(n):
            pass  # instanced deco: its children are walked and drawn individually
        if n.scene and os.path.basename(n.scene) not in DYNAMIC and n.script is not None and n.scene not in report_scripts:
            report_scripts.add(n.scene)
            report.append("note: scripted scene drawn as static: " + n.scene)

    size = 512 if bounds <= 256 else 1024
    scale = size / (2.0 * bounds)
    draw_items.sort(key=lambda n: (S.global_z(n), n.index))

    def hidden_by(n, hidden):
        x = n
        while x is not None:
            if id(x) in hidden:
                return True
            x = x.parent
        return False

    under, over = _bake(project, tc, draw_items, size, bounds, scale, player_index, modulate_canvas, set(), hidden_by)
    if lights_hidden:  # the lights-out look
        cm = None if modulate_node is not None and id(modulate_node) in lights_hidden else modulate_canvas
        under2, over2 = _bake(project, tc, draw_items, size, bounds, scale, player_index, cm, lights_hidden, hidden_by)
    elif alt_shown or alt_canvas is not None:  # the fake goal's look: its objects shown, the new colour
        saved = [(t, t.props.get("visible")) for t in alt_shown]
        for t in alt_shown:
            t.props["visible"] = True
        items2 = []
        for n in root.walk():
            if n is root or in_dynamic(n) or n.type == "CanvasLayer" or S.in_canvas_layer(n):
                continue
            if n.type in SKIP_TYPES or not S.visible(n):
                continue
            if n.type in ("Sprite2D", "AnimatedSprite2D", "Polygon2D", "TileMapLayer", "PointLight2D",
                          "NinePatchRect") and id(n) not in tile_groups:
                items2.append(n)
        for t, v in saved:
            if v is None:
                del t.props["visible"]
            else:
                t.props["visible"] = v
        items2.sort(key=lambda n: (S.global_z(n), n.index))
        cm = alt_canvas if alt_canvas is not None else modulate_canvas
        under2, over2 = _bake(project, tc, items2, size, bounds, scale, player_index, cm, set(), hidden_by)
    else:
        under2 = over2 = None
    # each switchable layer: its own picture file (<id>_t<group>.t3x; too big for the object atlas),
    # drawn while the layer is on
    os.makedirs(out_dir, exist_ok=True)
    tile_files = []
    for gid, role, idx, t in sorted(tile_groups.values(), key=lambda v: v[0]):
        pic = []
        if S.visible(t):
            cv = B.Canvas(int(2 * bounds * DYN_PX), -bounds, -bounds, DYN_PX)
            B.draw_tilemap(cv, tc, project, t, S.global_xform(t), S.global_modulate(t))
            if modulate_canvas is not None:
                cv.multiply(modulate_canvas)
            img = cv.image()
            a = np.asarray(img)[..., 3]
            ys, xs = np.nonzero(a)
            if len(xs):
                x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
                crop = img.crop((x0, y0, x1, y1))
                px = DYN_PX
                while max(crop.size) > 1024:  # tex3ds tops out at 1024: halve it
                    crop = crop.resize(((crop.size[0] + 1) // 2, (crop.size[1] + 1) // 2))
                    px *= 0.5
                fp = os.path.join(out_dir, "%s_t%d.png" % (out_name, gid))
                crop.save(fp)
                tile_files.append((gid, fp))
                f = px / DYN_PX
                pic = ["tex=1", "ax=%s" % _fmt(float(bounds * DYN_PX - x0) * f),
                       "ay=%s" % _fmt(float(bounds * DYN_PX - y0) * f), "px=%s" % _fmt(px)]
        z = S.global_z(t)
        layer_over = not (z < 0 or (z == 0 and t.index < player_index))
        on = (idx == 0) if role == 0 else (t.get("enabled", True) is not False)  # level.gd switch(): state 0
        objects.append(("tiles", (0.0, 0.0), 0.0, 1.0, 1.0,
                        ["group=%d" % gid, "role=%d" % role, "idx=%d" % idx, "on=%d" % (1 if on else 0),
                         "over=%d" % (1 if layer_over else 0)] + pic, True))

    os.makedirs(out_dir, exist_ok=True)
    img_u = under.image().convert("RGB")
    img_u.save(os.path.join(out_dir, out_name + "_u.png"))
    has_over = bool((over.px[..., 3] > 0.004).any())
    if has_over:
        over.image().save(os.path.join(out_dir, out_name + "_o.png"))
    if under2 is not None:
        under2.image().convert("RGB").save(os.path.join(out_dir, out_name + "_u2.png"))
        if has_over:
            over2.image().save(os.path.join(out_dir, out_name + "_o2.png"))

    shade = S.color(p.get("level_shade"))
    flags_list = [f for f in LEVEL_FLAGS if p.get(f)] + extra_flags
    lines = ["ucgl 1",
             "id %s" % lid,
             "author %s" % p.get("lvl_author", ""),
             "bounds %d" % bounds,
             "par %d" % int(p.get("par", 0)),
             "ontime %s" % _fmt(float(p.get("on_time", 0.0))),
             "ranks %s" % " ".join(str(int(x)) for x in (p.get("rank_reqs") or [0, 1, 4500, 5500, 6000, 7500])),
             "music %d" % int(p.get("music", -1)),
             "shade %s" % " ".join(_fmt(c) for c in shade),
             "flags %s" % " ".join(flags_list),
             "image %d %s" % (size, "uo" if has_over else "u")]
    for kind, bits, data in shapes:
        if kind == "gpoly":
            gid, pts = data
            lines.append("gpoly %d %d %d %s" % (gid, bits, len(pts), " ".join("%s %s" % (_fmt(x), _fmt(y)) for x, y in pts)))
        elif kind == "poly":
            lines.append("poly %d %d %s" % (bits, len(data), " ".join("%s %s" % (_fmt(x), _fmt(y)) for x, y in data)))
        else:
            lines.append("circ %d %s %s %s" % (bits, _fmt(data[0]), _fmt(data[1]), _fmt(data[2])))
    lines.extend(tracks)
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
            "dyn": dyn_files, "tiles": tile_files}


def _bake(project, tc, draw_items, size, bounds, scale, player_index, modulate_canvas, hidden, hidden_by):
    """The course's flat pictures: under the cat and over it, leaving out what is hidden."""
    under = B.Canvas(size, -bounds, -bounds, scale)
    over = B.Canvas(size, -bounds, -bounds, scale)
    lights = []
    for n in draw_items:
        if hidden and hidden_by(n, hidden):
            continue
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
    return under, over


def _dyn_sprite(dyn, pic):
    img, anchor = pic
    key = (img.size, img.tobytes(), round(anchor[0], 2), round(anchor[1], 2))
    for i, (im2, an2) in enumerate(dyn):
        if (im2.size, im2.tobytes(), round(an2[0], 2), round(an2[1], 2)) == key:
            return i
    dyn.append(pic)
    return len(dyn) - 1


report_scripts = set()
