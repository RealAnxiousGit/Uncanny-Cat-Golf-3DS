"""Bakes a level's static visuals into flat images, the way Godot would draw them.

Covers what the levels use for scenery: Sprite2D, AnimatedSprite2D (first
frame), Polygon2D, TileMapLayer, CanvasModulate, PointLight2D (added on top),
and the colour-swap shader. Anything else is skipped and reported.
"""
import math
import os

import numpy as np
from PIL import Image

import scene as S
from scene import Ext, Sub

Image.MAX_IMAGE_PIXELS = None


class TexCache:
    def __init__(self, project):
        self.project = project
        self.cache = {}
        self.unsupported = set()

    def image(self, path):
        """Straight-alpha float32 RGBA array (h, w, 4) in 0..1 for a res:// image."""
        key = ("img", path)
        if key not in self.cache:
            fs = self.project.fs(path)
            im = Image.open(fs).convert("RGBA")
            self.cache[key] = np.asarray(im, dtype=np.float32) / 255.0
        return self.cache[key]

    def texture(self, tex):
        """Returns (array, region (x, y, w, h)) for a Texture2D value, or None."""
        if tex is None:
            return None
        if isinstance(tex, Ext):
            if tex.path.lower().endswith((".png", ".jpg", ".jpeg", ".webp", ".svg")) and not tex.path.endswith(".svg"):
                arr = self.image(tex.path)
                return arr, (0, 0, arr.shape[1], arr.shape[0])
            if tex.path.endswith(".tres"):
                r = self.project.load(tex.path)
                sec = r.resource
                fake = Sub(self.project, r, sec)
                return self.texture(fake)
            self.unsupported.add("texture file " + os.path.splitext(tex.path)[1])
            return None
        if isinstance(tex, Sub):
            t = tex.type
            if t == "AtlasTexture":
                base = self.texture(tex.get("atlas"))
                if base is None:
                    return None
                arr, (bx, by, bw, bh) = base
                reg = tex.get("region")
                if reg is None:
                    return base
                x, y, w, h = [float(v) for v in reg.args]
                return arr, (bx + x, by + y, w, h)
            if t == "NoiseTexture2D":
                key = ("noise", id(tex.section))
                if key not in self.cache:
                    self.cache[key] = noise_texture(tex)
                arr = self.cache[key]
                return arr, (0, 0, arr.shape[1], arr.shape[0])
            if t == "CanvasTexture":
                return self.texture(tex.get("diffuse_texture"))
            if t in ("GradientTexture2D", "GradientTexture1D"):
                key = ("grad", id(tex.section))
                if key not in self.cache:
                    self.cache[key] = gradient_texture(tex)
                arr = self.cache[key]
                return arr, (0, 0, arr.shape[1], arr.shape[0])
            self.unsupported.add("texture " + str(t))
        return None


# --- procedural textures ----------------------------------------------------

def _gradient_colors(grad, n):
    if grad is None:
        cols, offs = [(0, 0, 0, 1), (1, 1, 1, 1)], [0.0, 1.0]
    else:
        cols = grad.get("colors") or [(0, 0, 0, 1), (1, 1, 1, 1)]
        offs = grad.get("offsets") or [i / max(1, len(cols) - 1) for i in range(len(cols))]
    t = np.linspace(0, 1, n)
    out = np.zeros((n, 4), np.float32)
    for ch in range(4):
        out[:, ch] = np.interp(t, offs, [c[ch] for c in cols])
    return out


def gradient_texture(tex):
    w = int(tex.get("width", 64))
    h = int(tex.get("height", 64)) if tex.type == "GradientTexture2D" else 1
    lut = _gradient_colors(tex.get("gradient"), 256)
    if tex.type == "GradientTexture1D":
        return lut[np.linspace(0, 255, w).astype(int)][None, :, :]
    fx, fy = S.vec(tex.get("fill_from"), (0.0, 0.0))
    tx, ty = S.vec(tex.get("fill_to"), (1.0, 0.0))
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    u, v = (xs + 0.5) / w, (ys + 0.5) / h
    if int(tex.get("fill", 0)) == 1:  # radial
        t = np.hypot(u - fx, v - fy) / max(1e-6, math.hypot(tx - fx, ty - fy))
    else:
        dx, dy = tx - fx, ty - fy
        t = ((u - fx) * dx + (v - fy) * dy) / max(1e-6, dx * dx + dy * dy)
    t = np.clip(t, 0, 1)
    return lut[(t * 255).astype(int)]


def _value_noise(w, h, freq, seed, seamless):
    rng = np.random.default_rng(seed & 0xFFFFFFFF)
    # smooth value noise with fractal octaves; not Godot's exact noise, same look
    total = np.zeros((h, w), np.float32)
    amp, f, norm = 1.0, freq, 0.0
    for _ in range(5):
        gw = max(2, int(round(w * f)))
        gh = max(2, int(round(h * f)))
        grid = rng.random((gh + 1, gw + 1)).astype(np.float32)
        if seamless:
            grid[-1, :] = grid[0, :]
            grid[:, -1] = grid[:, 0]
        ys = np.linspace(0, gh, h, endpoint=not seamless)
        xs = np.linspace(0, gw, w, endpoint=not seamless)
        y0 = np.clip(ys.astype(int), 0, gh - 1)
        x0 = np.clip(xs.astype(int), 0, gw - 1)
        fy = ys - y0
        fx = xs - x0
        fy = fy * fy * (3 - 2 * fy)
        fx = fx * fx * (3 - 2 * fx)
        a = grid[y0][:, x0]
        b = grid[y0][:, x0 + 1]
        c = grid[y0 + 1][:, x0]
        d = grid[y0 + 1][:, x0 + 1]
        top = a + (b - a) * fx[None, :]
        bot = c + (d - c) * fx[None, :]
        total += amp * (top + (bot - top) * fy[:, None])
        norm += amp
        amp *= 0.5
        f *= 2.0
    total /= norm
    lo, hi = total.min(), total.max()
    return (total - lo) / max(1e-6, hi - lo)


def noise_texture(tex):
    w = int(tex.get("width", 512))
    h = int(tex.get("height", 512))
    noise = tex.get("noise")
    freq = float(noise.get("frequency", 0.01)) if isinstance(noise, Sub) else 0.01
    seed = int(noise.get("seed", 0)) if isinstance(noise, Sub) else 0
    v = _value_noise(w, h, freq, seed, bool(tex.get("seamless", False)))
    if tex.get("invert"):
        v = 1 - v
    ramp = tex.get("color_ramp")
    if ramp is not None:
        lut = _gradient_colors(ramp, 256)
        return lut[(v * 255).astype(int)]
    out = np.ones((h, w, 4), np.float32)
    out[..., 0] = out[..., 1] = out[..., 2] = v
    return out


# --- shaders ----------------------------------------------------------------

def material_fn(mat, unsupported):
    """Returns a function that recolours a straight-alpha RGBA array, or None."""
    if not isinstance(mat, Sub) or mat.type != "ShaderMaterial":
        return None
    sh = mat.get("shader")
    name = os.path.basename(sh.path) if isinstance(sh, Ext) else str(sh)
    if name == "colorswap.gdshader" or name == "colorswapShiny.gdshader":
        shades = [S.color(mat.get("shader_parameter/" + k), None)
                  for k in ("HighShade", "MidShade", "LowShade", "ExtraShade")]
        alpha = float(mat.get("shader_parameter/alpha", 1.0))
        return colorswap_fn(shades, alpha)
    unsupported.add("shader " + name)
    return None


def colorswap_fn(shades, alpha=1.0):
    """colorswap.gdshader: white/red/black/blue pixels become High/Mid/Low/Extra shades."""
    keys = [(255, 255, 255), (255, 0, 0), (0, 0, 0), (0, 0, 255)]

    def fn(arr):
        out = arr.copy()
        c8 = np.floor(arr * 255.0 + 1e-3)
        opaque = c8[..., 3] >= 255
        for key, shade in zip(keys, shades):
            if shade is None:
                shade = (0.0, 0.0, 0.0, 0.0)
            m = opaque & (c8[..., 0] == key[0]) & (c8[..., 1] == key[1]) & (c8[..., 2] == key[2])
            out[m] = shade
        nz = out[..., 3] > 0
        out[..., 3] = np.where(nz, alpha * arr[..., 3], 0)
        return out
    return fn


# --- the canvas -------------------------------------------------------------

class Canvas:
    """Premultiplied RGBA float canvas covering world rect [x0, x0+size/scale)."""

    def __init__(self, size, x0, y0, scale):
        self.size = size
        self.x0, self.y0, self.scale = x0, y0, scale
        self.px = np.zeros((size, size, 4), np.float32)
        # world -> pixel transform
        self.w2p = (scale, 0.0, -x0 * scale, 0.0, scale, -y0 * scale)

    def draw_textured(self, xf, tex_arr, region, quad_local, uv_of_local, modulate, wrap=False,
                      colorfn=None, add=False, poly=None):
        """Draw a textured shape.

        xf: local->world affine. quad_local: polygon (local coords) to fill.
        uv_of_local: affine local -> texel (in the source array, absolute pixels).
        """
        m = S.mul(self.w2p, xf)  # local -> pixel
        pts = [S.apply(m, p) for p in quad_local]
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        ix0 = max(0, int(math.floor(min(xs))))
        iy0 = max(0, int(math.floor(min(ys))))
        ix1 = min(self.size, int(math.ceil(max(xs))) + 1)
        iy1 = min(self.size, int(math.ceil(max(ys))) + 1)
        if ix0 >= ix1 or iy0 >= iy1:
            return
        inv = S.invert(m)
        if inv is None:
            return
        gy, gx = np.mgrid[iy0:iy1, ix0:ix1].astype(np.float32)
        gx += 0.5
        gy += 0.5
        lx = inv[0] * gx + inv[1] * gy + inv[2]
        ly = inv[3] * gx + inv[4] * gy + inv[5]
        mask = _point_in_poly(lx, ly, poly if poly is not None else quad_local)
        if not mask.any():
            return
        if tex_arr is None:
            col = np.ones(lx.shape + (4,), np.float32)
        else:
            tu = uv_of_local[0] * lx + uv_of_local[1] * ly + uv_of_local[2]
            tv = uv_of_local[3] * lx + uv_of_local[4] * ly + uv_of_local[5]
            # texel per pixel, to pick a pre-shrunk copy and avoid shimmering
            sx = math.hypot(uv_of_local[0] * inv[0] + uv_of_local[1] * inv[3],
                            uv_of_local[3] * inv[0] + uv_of_local[4] * inv[3])
            col = _sample(tex_arr, region, tu, tv, wrap, max(1.0, sx))
            if colorfn is not None:
                col = colorfn(col)
        col = col * np.array(modulate, np.float32)
        a = col[..., 3:4] * mask[..., None]
        pre = col[..., :3] * a
        dst = self.px[iy0:iy1, ix0:ix1]
        if add:
            dst[..., :3] += pre
        else:
            dst[..., :3] = pre + dst[..., :3] * (1 - a)
            dst[..., 3:4] = a + dst[..., 3:4] * (1 - a)

    def multiply(self, c):
        self.px[..., 0] *= c[0]
        self.px[..., 1] *= c[1]
        self.px[..., 2] *= c[2]

    def image(self):
        a = np.clip(self.px[..., 3:4], 0, 1)
        rgb = np.where(a > 1e-6, self.px[..., :3] / np.maximum(a, 1e-6), 0)
        out = np.concatenate([np.clip(rgb, 0, 1), a], axis=-1)
        return Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA")


_shrink_cache = {}


def _sample(arr, region, u, v, wrap, shrink):
    rx, ry, rw, rh = region
    level = 1
    while shrink >= 2.0 * level and min(rw, rh) / (level * 2) >= 2:
        level *= 2
    if level > 1:
        key = (id(arr), level)
        small = _shrink_cache.get(key)
        if small is None:
            im = arr
            pre = im[..., :3] * im[..., 3:4]
            pim = np.concatenate([pre, im[..., 3:4]], -1)
            h, w = pim.shape[:2]
            img = Image.fromarray((np.clip(pim, 0, 1) * 255).astype(np.uint8), "RGBA")
            img = img.resize((max(1, w // level), max(1, h // level)), Image.BOX)
            s = np.asarray(img, np.float32) / 255.0
            a = s[..., 3:4]
            s = np.concatenate([np.where(a > 0, s[..., :3] / np.maximum(a, 1e-6), 0), a], -1)
            small = _shrink_cache[key] = s
        arr = small
        u = u / level
        v = v / level
        rx, ry, rw, rh = rx / level, ry / level, rw / level, rh / level
    h, w = arr.shape[:2]
    if wrap:
        uu = rx + np.mod(u - rx, rw)
        vv = ry + np.mod(v - ry, rh)
    else:
        uu = np.clip(u, rx, rx + rw - 1e-3)
        vv = np.clip(v, ry, ry + rh - 1e-3)
    # bilinear in premultiplied space
    fx = uu - 0.5
    fy = vv - 0.5
    x0 = np.floor(fx).astype(np.int64)
    y0 = np.floor(fy).astype(np.int64)
    ax = (fx - x0)[..., None]
    ay = (fy - y0)[..., None]
    lo_x, hi_x = int(math.floor(rx)), int(math.ceil(rx + rw)) - 1
    lo_y, hi_y = int(math.floor(ry)), int(math.ceil(ry + rh)) - 1

    def at(xx, yy):
        if wrap:
            xx = lo_x + np.mod(xx - lo_x, max(1, hi_x - lo_x + 1))
            yy = lo_y + np.mod(yy - lo_y, max(1, hi_y - lo_y + 1))
        xx = np.clip(xx, max(0, lo_x), min(w - 1, hi_x))
        yy = np.clip(yy, max(0, lo_y), min(h - 1, hi_y))
        p = arr[yy, xx]
        return np.concatenate([p[..., :3] * p[..., 3:4], p[..., 3:4]], -1)

    c = (at(x0, y0) * (1 - ax) * (1 - ay) + at(x0 + 1, y0) * ax * (1 - ay) +
         at(x0, y0 + 1) * (1 - ax) * ay + at(x0 + 1, y0 + 1) * ax * ay)
    a = c[..., 3:4]
    return np.concatenate([np.where(a > 1e-6, c[..., :3] / np.maximum(a, 1e-6), 0), a], -1)


def _point_in_poly(x, y, poly):
    n = len(poly)
    if n == 4 and _is_axis_rect(poly):
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        return (x >= min(xs)) & (x < max(xs)) & (y >= min(ys)) & (y < max(ys))
    inside = np.zeros(x.shape, bool)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if yi != yj:
            cond = ((yi > y) != (yj > y)) & (x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            inside ^= cond
        j = i
    return inside


def _is_axis_rect(p):
    xs = sorted(set(round(q[0], 6) for q in p))
    ys = sorted(set(round(q[1], 6) for q in p))
    return len(xs) == 2 and len(ys) == 2


# --- drawing nodes ----------------------------------------------------------

def frame_texture(node):
    """Texture of the first frame an AnimatedSprite2D shows."""
    sf = node.get("sprite_frames")
    if not isinstance(sf, Sub):
        return None
    anims = sf.get("animations") or []
    want = node.get("animation", "default")
    pick = None
    for a in anims:
        if a.get("name") == want:
            pick = a
    if pick is None and anims:
        pick = anims[0]
    if not pick or not pick.get("frames"):
        return None
    fr = int(node.get("frame", 0) or 0)
    frames = pick["frames"]
    return frames[min(fr, len(frames) - 1)].get("texture")


def draw_sprite(cv, tc, node, xf, tex, modulate):
    got = tc.texture(tex)
    if got is None:
        return
    arr, (rx, ry, rw, rh) = got
    hf = int(node.get("hframes", 1) or 1)
    vf = int(node.get("vframes", 1) or 1)
    if node.get("region_enabled"):
        r = node.get("region_rect")
        if r is not None:
            x, y, w, h = [float(v) for v in r.args]
            rx, ry, rw, rh = rx + x, ry + y, w, h
    fw, fh = rw / hf, rh / vf
    fr = int(node.get("frame", 0) or 0)
    if node.get("frame_coords") is not None:
        cx, cy = S.vec(node.get("frame_coords"))
        fr = int(cy) * hf + int(cx)
    fx, fy = fr % hf, fr // hf
    rx, ry = rx + fx * fw, ry + fy * fh
    ox, oy = S.vec(node.get("offset"))
    if node.get("centered", True) is not False:
        ox -= fw / 2
        oy -= fh / 2
    quad = [(ox, oy), (ox + fw, oy), (ox + fw, oy + fh), (ox, oy + fh)]
    flip_h = bool(node.get("flip_h"))
    flip_v = bool(node.get("flip_v"))
    # local -> texel
    a = -1.0 if flip_h else 1.0
    e = -1.0 if flip_v else 1.0
    tx0 = rx + (fw if flip_h else 0) - a * ox
    ty0 = ry + (fh if flip_v else 0) - e * oy
    uv = (a, 0.0, tx0, 0.0, e, ty0)
    fn = material_fn(effective_material(node), tc.unsupported)
    cv.draw_textured(xf, arr, (rx, ry, fw, fh), quad, uv, modulate, False, fn)


def effective_material(node):
    x = node
    while x is not None:
        m = x.get("material")
        if m is not None:
            return m
        if not x.get("use_parent_material"):
            return None
        x = x.parent
    return None


def draw_polygon(cv, tc, node, xf, modulate):
    pts = node.get("polygon") or []
    if len(pts) < 3:
        return
    off = S.vec(node.get("offset"))
    pts = [(p[0] + off[0], p[1] + off[1]) for p in pts]
    col = S.color(node.get("color"))
    mod = tuple(m * c for m, c in zip(modulate, col))
    tex = node.get("texture")
    got = tc.texture(tex) if tex is not None else None
    if got is None:
        cv.draw_textured(xf, None, None, pts, None, mod, poly=pts)
        return
    arr, (rx, ry, rw, rh) = got
    to = S.vec(node.get("texture_offset"))
    ts = S.vec(node.get("texture_scale"), (1.0, 1.0))
    tr = float(node.get("texture_rotation", 0.0) or 0.0)
    cr, sr = math.cos(tr), math.sin(tr)
    # texel = scale * (R * (p - offset_node) + tex_offset); uv defaults to the (un-offset) points
    m = (ts[0] * cr, -ts[0] * sr, ts[0] * to[0], ts[1] * sr, ts[1] * cr, ts[1] * to[1])
    m = S.mul(m, (1, 0, -off[0], 0, 1, -off[1]))
    m = (m[0], m[1], m[2] + rx, m[3], m[4], m[5] + ry)
    wrap = int(node.get("texture_repeat", 0) or 0) in (2, 3)
    if node.get("uv"):
        wrap = wrap  # explicit UVs: rare in levels; fall back to point mapping
    fn = material_fn(effective_material(node), tc.unsupported)
    cv.draw_textured(xf, arr, (rx, ry, rw, rh), pts, m, mod, wrap, fn, poly=pts)


class TileSetInfo:
    def __init__(self, project, tc, tset):
        self.tile_size = (16, 16)
        self.sources = {}
        self.physics_layers = {}
        res = None
        if isinstance(tset, Ext):
            res = project.load(tset.path)
            root = Sub(project, res, res.resource)
        else:
            root = tset
        ts = root.get("tile_size")
        if ts is not None:
            self.tile_size = S.vec(ts)
        for k, v in root.props.items():
            if k.startswith("sources/"):
                self.sources[int(k.split("/")[1])] = v
            if k.startswith("physics_layer_") and k.endswith("/collision_layer"):
                self.physics_layers[int(k.split("_")[2].split("/")[0])] = int(v)
        self.root = root

    def tile(self, src, ax, ay, alt):
        s = self.sources.get(src)
        if not isinstance(s, Sub) or s.type != "TileSetAtlasSource":
            return None
        rs = S.vec(s.get("texture_region_size"), self.tile_size)
        margins = S.vec(s.get("margins"))
        sep = S.vec(s.get("separation"))
        key = "%d:%d/%d" % (ax, ay, alt)
        base = "%d:%d/0" % (ax, ay)
        size = S.vec(s.get("%d:%d/size_in_atlas" % (ax, ay)), (1.0, 1.0))
        info = {
            "texture": s.get("texture"),
            "region": (margins[0] + ax * (rs[0] + sep[0]), margins[1] + ay * (rs[1] + sep[1]),
                       rs[0] * size[0], rs[1] * size[1]),
            "flip_h": bool(s.get(key + "/flip_h", False)),
            "flip_v": bool(s.get(key + "/flip_v", False)),
            "transpose": bool(s.get(key + "/transpose", False)),
            "origin": S.vec(s.get(key + "/texture_origin", s.get(base + "/texture_origin"))),
            "modulate": S.color(s.get(key + "/modulate")),
            "polys": [],
        }
        for layer in range(4):
            for pi in range(8):
                pts = s.get("%s/physics_layer_%d/polygon_%d/points" % (key, layer, pi))
                if pts is None and alt != 0:
                    pts = s.get("%s/physics_layer_%d/polygon_%d/points" % (base, layer, pi))
                if pts is None:
                    break
                info["polys"].append((self.physics_layers.get(layer, 1), pts))
        return info


def draw_tilemap(cv, tc, project, node, xf, modulate, collect=None):
    from godot import tile_map_cells
    tset = node.get("tile_set")
    if tset is None:
        return
    key = ("tileset", tset.path if isinstance(tset, Ext) else id(tset))
    if key not in tc.cache:
        tc.cache[key] = TileSetInfo(project, tc, tset)
    tsi = tc.cache[key]
    tw, th = tsi.tile_size
    fn = material_fn(effective_material(node), tc.unsupported)
    for (x, y, src, ax, ay, alt) in tile_map_cells(node.get("tile_map_data")):
        t = tsi.tile(src, ax, ay, alt)
        if t is None:
            continue
        cx, cy = x * tw + tw / 2, y * th + th / 2
        if collect is not None and node.get("collision_enabled", True) is not False:
            for layer_bits, pts in t["polys"]:
                collect.append((layer_bits, [S.apply(xf, (cx + px, cy + py)) for (px, py) in _tile_xform_pts(t, pts)]))
        if cv is None:
            continue
        got = tc.texture(t["texture"])
        if got is None:
            continue
        arr, _ = got
        rx, ry, rw, rh = t["region"]
        ox, oy = t["origin"]
        hx, hy = rw / 2, rh / 2
        quad = [(cx - hx - ox, cy - hy - oy), (cx + hx - ox, cy - hy - oy),
                (cx + hx - ox, cy + hy - oy), (cx - hx - ox, cy + hy - oy)]
        lx0, ly0 = quad[0]
        # (u, v) = (lx - lx0, ly - ly0), optionally swapped, then flipped
        U = (1.0, 0.0, -lx0)
        V = (0.0, 1.0, -ly0)
        if t["transpose"]:
            U, V = V, U
        if t["flip_h"]:
            U = (-U[0], -U[1], rw - U[2])
        if t["flip_v"]:
            V = (-V[0], -V[1], rh - V[2])
        uv = (U[0], U[1], U[2] + rx, V[0], V[1], V[2] + ry)
        mod = tuple(m * c for m, c in zip(modulate, t["modulate"]))
        cv.draw_textured(xf, arr, (rx, ry, rw, rh), quad, uv, mod, False, fn)


def _tile_xform_pts(t, pts):
    out = []
    for (px, py) in pts:
        if t["transpose"]:
            px, py = py, px
        if t["flip_h"]:
            px = -px
        if t["flip_v"]:
            py = -py
        out.append((px, py))
    return out


def draw_light(cv, tc, node, xf):
    tex = node.get("texture")
    got = tc.texture(tex)
    if got is None:
        return
    arr, (rx, ry, rw, rh) = got
    sc = float(node.get("texture_scale", 1.0) or 1.0)
    energy = float(node.get("energy", 1.0) or 1.0)
    col = S.color(node.get("color"))
    ox, oy = S.vec(node.get("offset"))
    w, h = rw * sc, rh * sc
    quad = [(ox - w / 2, oy - h / 2), (ox + w / 2, oy - h / 2), (ox + w / 2, oy + h / 2), (ox - w / 2, oy + h / 2)]
    uv = (1 / sc, 0.0, rx - (ox - w / 2) / sc, 0.0, 1 / sc, ry - (oy - h / 2) / sc)
    blend = int(node.get("blend_mode", 0) or 0)
    if blend != 0:
        return  # subtract / mix lights: skipped for now
    mod = (col[0] * energy * col[3], col[1] * energy * col[3], col[2] * energy * col[3], 1.0)
    cv.draw_textured(xf, arr, (rx, ry, rw, rh), quad, uv, mod, False, None, add=True)

def draw_ninepatch(cv, tc, node, xf, modulate, rect=None, colorfn=None):
    """NinePatchRect. rect = (x, y, w, h) in xf's space (defaults to the node's offsets)."""
    got = tc.texture(node.get("texture"))
    if got is None:
        return
    arr, (rx, ry, rw, rh) = got
    rr = node.get("region_rect")
    if rr is not None:
        x, y, w, h = [float(v) for v in rr.args]
        if w > 0 and h > 0:
            rx, ry, rw, rh = rx + x, ry + y, w, h
    if rect is None:
        l, t = float(node.get("offset_left", 0) or 0), float(node.get("offset_top", 0) or 0)
        r, b = float(node.get("offset_right", 0) or 0), float(node.get("offset_bottom", 0) or 0)
        rect = (l, t, r - l, b - t)
    X, Y, W, H = rect
    ml, mt = float(node.get("patch_margin_left", 0) or 0), float(node.get("patch_margin_top", 0) or 0)
    mr, mb = float(node.get("patch_margin_right", 0) or 0), float(node.get("patch_margin_bottom", 0) or 0)
    tile_h = int(node.get("axis_stretch_horizontal", 0) or 0) != 0
    tile_v = int(node.get("axis_stretch_vertical", 0) or 0) != 0
    xs_d = [X, X + ml, X + W - mr, X + W]
    ys_d = [Y, Y + mt, Y + H - mb, Y + H]
    xs_s = [rx, rx + ml, rx + rw - mr, rx + rw]
    ys_s = [ry, ry + mt, ry + rh - mb, ry + rh]
    fn = colorfn if colorfn is not None else material_fn(effective_material(node), tc.unsupported)
    for j in range(3):
        for i in range(3):
            dx0, dx1, dy0, dy1 = xs_d[i], xs_d[i + 1], ys_d[j], ys_d[j + 1]
            sx0, sx1, sy0, sy1 = xs_s[i], xs_s[i + 1], ys_s[j], ys_s[j + 1]
            if dx1 <= dx0 or dy1 <= dy0 or sx1 <= sx0 or sy1 <= sy0:
                continue
            kx = 1.0 if (i == 1 and tile_h) else (sx1 - sx0) / (dx1 - dx0)
            ky = 1.0 if (j == 1 and tile_v) else (sy1 - sy0) / (dy1 - dy0)
            quad = [(dx0, dy0), (dx1, dy0), (dx1, dy1), (dx0, dy1)]
            uv = (kx, 0.0, sx0 - kx * dx0, 0.0, ky, sy0 - ky * dy0)
            cv.draw_textured(xf, arr, (sx0, sy0, sx1 - sx0, sy1 - sy0), quad, uv, modulate, True, fn)
