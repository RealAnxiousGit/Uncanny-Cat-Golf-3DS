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
}
CAR_SCENE = "res://assetsNEW/scenes/objects/baddies/car.tscn"
SPAWNER_SCENE = "res://assetsNEW/scenes/objects/baddies/baddie_spawner.tscn"
SKULL_SCENE = "res://assetsNEW/scenes/objects/baddies/skull.tscn"
CHUCKLE_SCENE = "res://assetsNEW/scenes/objects/baddies/chuckle.tscn"
# Areas whose art stays baked into the floor; only their outline goes to the game code.
ZONES = {"sand_pit.tscn": "sand", "water_death.tscn": "water"}
# Objects drawn at runtime from their own baked picture (<id>_d.t3x)
PICTURED = {"dblock", "keydoor", "key", "skull", "token", "jumppad", "spawner", "car",
            "switch", "dog", "doggoal", "ring", "scoin"}
# the node that gets graphic.modulate = level.level_shade in each object's _ready()
SHADED = {"skull": "AnimatedSprite2D", "car": "Sprite2D", "token": "AnimatedSprite2D", "spawner": "Door",
          "jumppad": "Sprite2D", "dog": "Sprite2D", "doggoal": "AnimatedSprite2D", "ring": "AnimatedSprite2D"}
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
    return out


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
    node_of = []  # the scene node behind each object, to link slots to their tokens
    slot_links = []  # (object index, token node)
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
        if kind in ("coin", "token"):
            coin_index[id(node_of[i])] = len(coin_index)
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
        if kind == "gpoly":
            gid, pts = data
            lines.append("gpoly %d %d %d %s" % (gid, bits, len(pts), " ".join("%s %s" % (_fmt(x), _fmt(y)) for x, y in pts)))
        elif kind == "poly":
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
            "dyn": dyn_files, "tiles": tile_files}


def _dyn_sprite(dyn, pic):
    img, anchor = pic
    key = (img.size, img.tobytes(), round(anchor[0], 2), round(anchor[1], 2))
    for i, (im2, an2) in enumerate(dyn):
        if (im2.size, im2.tobytes(), round(an2[0], 2), round(an2[1], 2)) == key:
            return i
    dyn.append(pic)
    return len(dyn) - 1


report_scripts = set()
