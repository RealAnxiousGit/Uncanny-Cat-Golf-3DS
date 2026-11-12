"""Builds node trees from Godot scenes, with instanced scenes merged in.

Every property value is resolved against the file it was written in, so an
ExtResource inside an instanced sub-scene points at that sub-scene's files.
"""
import math

from godot import ExtRef, SubRef, GType, NodePathV


class Ext:
    """An external resource: a file path plus its declared type."""
    __slots__ = ("path", "type")

    def __init__(self, path, rtype):
        self.path = path
        self.type = rtype

    def __repr__(self):
        return "Ext(%s)" % self.path


class Sub:
    """A sub_resource with its properties resolved lazily."""
    __slots__ = ("project", "res", "section", "_props")

    def __init__(self, project, res, section):
        self.project = project
        self.res = res
        self.section = section
        self._props = None

    @property
    def type(self):
        return self.section.attrs.get("type")

    @property
    def props(self):
        if self._props is None:
            self._props = {k: resolve(self.project, self.res, v) for k, v in self.section.props.items()}
        return self._props

    def get(self, k, d=None):
        return self.props.get(k, d)

    def __repr__(self):
        return "Sub(%s %s)" % (self.type, self.section.attrs.get("id"))


def resolve(project, res, v):
    if isinstance(v, ExtRef):
        if v.id not in res.ext:
            return None
        a = res.ext[v.id]
        return Ext(project.resolve_ext(res, v.id), a.get("type"))
    if isinstance(v, SubRef):
        s = res.sub.get(v.id)
        return Sub(project, res, s) if s else None
    if isinstance(v, list):
        return [resolve(project, res, x) for x in v]
    if isinstance(v, dict):
        return {k: resolve(project, res, x) for k, x in v.items()}
    return v


class Node:
    __slots__ = ("name", "type", "props", "children", "parent", "scene", "index", "groups")

    def __init__(self, name, ntype):
        self.name = name
        self.type = ntype
        self.props = {}
        self.children = []
        self.parent = None
        self.scene = None  # res path of the scene this node is the root of (instances)
        self.index = 0
        self.groups = []

    def get(self, k, d=None):
        return self.props.get(k, d)

    def child(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def find(self, path):
        n = self
        for part in path.split("/"):
            if part in ("", "."):
                continue
            if part == "..":
                n = n.parent
                continue
            n = n.child(part)
            if n is None:
                return None
        return n

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()

    def path(self):
        parts = []
        n = self
        while n.parent is not None:
            parts.append(n.name)
            n = n.parent
        return "/".join(reversed(parts))

    @property
    def script(self):
        s = self.props.get("script")
        return s.path if isinstance(s, Ext) else None

    def __repr__(self):
        return "<%s %s%s>" % (self.type, self.name, " @" + self.scene if self.scene else "")


BINARY_SKIPPED = set()  # binary sub-scenes the text reader cannot open (left out of the level)


def instantiate(project, res_path, _depth=0):
    if _depth > 12:
        raise RuntimeError("scene nesting too deep at " + res_path)
    res = project.load(res_path)
    root = None
    for sec in res.nodes:
        a = sec.attrs
        name = a["name"]
        inst = a.get("instance")
        if inst is not None:
            sub_path = project.resolve_ext(res, inst.id)
            try:
                node = instantiate(project, sub_path, _depth + 1)
            except UnicodeDecodeError:  # a binary scene (.scn / imported .glb): an empty stand-in
                node = Node(name, "Node2D")
                BINARY_SKIPPED.add(sub_path)
            node.name = name
            node.scene = sub_path
        elif "parent" in a and root is not None and root.find(a["parent"]) is not None \
                and root.find(a["parent"]).child(name) is not None and "type" not in a:
            # override of a node inside an instanced sub-scene (editable children)
            node = root.find(a["parent"]).child(name)
        else:
            node = Node(name, a.get("type", "Node"))
        if a.get("groups"):
            node.groups = list(a["groups"])
        for k, v in sec.props.items():
            node.props[k] = resolve(project, res, v)
        if "parent" not in a:
            if root is None:
                root = node
                if inst is None:
                    root.scene = None
            continue
        parent = root.find(a["parent"])
        if parent is None:
            continue  # parent removed / outside this scene
        if node.parent is None and node is not root:
            node.parent = parent
            parent.children.append(node)
    # tree order index
    for i, n in enumerate(root.walk()):
        n.index = i
    return root


# --- transforms -------------------------------------------------------------

def vec(v, d=(0.0, 0.0)):
    if isinstance(v, GType) and v.name in ("Vector2", "Vector2i"):
        return (float(v.args[0]), float(v.args[1]))
    return d


def color(v, d=(1.0, 1.0, 1.0, 1.0)):
    if isinstance(v, GType) and v.name == "Color":
        a = [float(x) for x in v.args]
        if len(a) == 3:
            a.append(1.0)
        return tuple(a)
    return d


def local_xform(n):
    """2x3 affine [a b c; d e f] of a CanvasItem (Node2D) relative to its parent."""
    px, py = vec(n.get("position"))
    rot = float(n.get("rotation", 0.0) or 0.0)
    sx, sy = vec(n.get("scale"), (1.0, 1.0))
    skew = float(n.get("skew", 0.0) or 0.0)
    cr, sr = math.cos(rot), math.sin(rot)
    # Godot: x axis = (cos r, sin r) * sx ; y axis = (-sin(r+skew), cos(r+skew)) * sy
    cs, ss = math.cos(rot + skew), math.sin(rot + skew)
    return (cr * sx, -ss * sy, px, sr * sx, cs * sy, py)


def mul(a, b):
    return (a[0] * b[0] + a[1] * b[3], a[0] * b[1] + a[1] * b[4], a[0] * b[2] + a[1] * b[5] + a[2],
            a[3] * b[0] + a[4] * b[3], a[3] * b[1] + a[4] * b[4], a[3] * b[2] + a[4] * b[5] + a[5])


IDENT = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0)


def apply(m, p):
    return (m[0] * p[0] + m[1] * p[1] + m[2], m[3] * p[0] + m[4] * p[1] + m[5])


def invert(m):
    det = m[0] * m[4] - m[1] * m[3]
    if abs(det) < 1e-12:
        return None
    ia, ib, id_, ie = m[4] / det, -m[1] / det, -m[3] / det, m[0] / det
    return (ia, ib, -(ia * m[2] + ib * m[5]), id_, ie, -(id_ * m[2] + ie * m[5]))


NON_2D = {"Node", "CanvasLayer", "AudioStreamPlayer", "AnimationPlayer", "Timer", "Node3D",
          "SubViewport", "HTTPRequest", "ResourcePreloader"}


def global_xform(n, stop=None):
    """Transform from node space to the level root's space."""
    chain = []
    x = n
    while x is not None and x is not stop:
        chain.append(x)
        x = x.parent
    m = IDENT
    for x in reversed(chain):
        if x.type == "CanvasLayer":
            m = IDENT  # canvas layers are screen space; treated as their own root
            continue
        if x.type in NON_2D and x.type != "CanvasLayer":
            continue
        if x.parent is None:
            continue  # level root sits at the origin
        m = mul(m, local_xform(x))
    return m


def in_canvas_layer(n):
    x = n.parent
    while x is not None:
        if x.type == "CanvasLayer":
            return True
        x = x.parent
    return False


def visible(n):
    x = n
    while x is not None:
        if x.get("visible") is False:
            return False
        x = x.parent
    return True


def global_modulate(n):
    r = g = b = a = 1.0
    x = n
    first = True
    while x is not None:
        c = color(x.get("modulate"))
        r, g, b, a = r * c[0], g * c[1], b * c[2], a * c[3]
        if first:
            c = color(x.get("self_modulate"))
            r, g, b, a = r * c[0], g * c[1], b * c[2], a * c[3]
            first = False
        x = x.parent
    return (r, g, b, a)


def global_z(n):
    z = 0
    x = n
    while x is not None:
        z += int(x.get("z_index", 0) or 0)
        if x.get("z_as_relative") is False:
            break
        x = x.parent
    return z
