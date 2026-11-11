"""Reader for Godot 4 text resources (.tscn, .tres, project.godot).

Only reads; never writes Godot files. Values come back as plain Python:
numbers, strings, lists, dicts, and small classes for Godot types.
"""
import base64
import os
import re
import struct


class GType:
    """A Godot constructor value such as Vector2(1, 2) or Color(...)."""
    __slots__ = ("name", "args")

    def __init__(self, name, args):
        self.name = name
        self.args = args

    def __repr__(self):
        return "%s(%s)" % (self.name, ", ".join(repr(a) for a in self.args))

    def __eq__(self, other):
        return isinstance(other, GType) and other.name == self.name and other.args == self.args

    def __hash__(self):
        return hash((self.name, tuple(map(repr, self.args))))


class ExtRef:
    __slots__ = ("id",)

    def __init__(self, rid):
        self.id = rid

    def __repr__(self):
        return "ExtResource(%r)" % self.id


class SubRef:
    __slots__ = ("id",)

    def __init__(self, rid):
        self.id = rid

    def __repr__(self):
        return "SubResource(%r)" % self.id


class NodePathV(str):
    pass


class StringNameV(str):
    pass


_num = re.compile(r"[-+]?(?:\d+\.?\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|inf|nan)")
_ident = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


class _P:
    def __init__(self, text):
        self.s = text
        self.i = 0

    def ws(self):
        s, n = self.s, len(self.s)
        while self.i < n:
            c = s[self.i]
            if c in " \t\r\n,":
                # commas are separators inside lists; harmless elsewhere
                self.i += 1
            elif c == ";":
                while self.i < n and s[self.i] != "\n":
                    self.i += 1
            else:
                break

    def peek(self):
        self.ws()
        return self.s[self.i] if self.i < len(self.s) else ""

    def string(self):
        assert self.s[self.i] == '"'
        self.i += 1
        out = []
        s = self.s
        while True:
            c = s[self.i]
            if c == "\\":
                nxt = s[self.i + 1]
                out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(nxt, nxt))
                self.i += 2
            elif c == '"':
                self.i += 1
                return "".join(out)
            else:
                out.append(c)
                self.i += 1

    def value(self):
        c = self.peek()
        s = self.s
        if c == '"':
            return self.string()
        if c == "&" and s[self.i + 1] == '"':
            self.i += 1
            return StringNameV(self.string())
        if c == "^" and s[self.i + 1] == '"':
            self.i += 1
            return NodePathV(self.string())
        if c == "[":
            self.i += 1
            out = []
            while self.peek() != "]":
                out.append(self.value())
            self.i += 1
            return out
        if c == "{":
            self.i += 1
            out = {}
            while self.peek() != "}":
                k = self.value()
                self.ws()
                assert s[self.i] == ":", s[self.i - 20:self.i + 20]
                self.i += 1
                out[k] = self.value()
            self.i += 1
            return out
        m = _num.match(s, self.i)
        if m and (c.isdigit() or c in "-+."):
            self.i = m.end()
            t = m.group(0)
            if re.fullmatch(r"[-+]?\d+", t):
                return int(t)
            return float(t)
        m = _ident.match(s, self.i)
        if not m:
            raise ValueError("bad value at %d: %r" % (self.i, s[self.i:self.i + 40]))
        name = m.group(0)
        self.i = m.end()
        if name == "true":
            return True
        if name == "false":
            return False
        if name == "null":
            return None
        if name in ("inf", "nan"):
            return float(name)
        # typed containers: Array[int]([...]) / Dictionary[A, B]({...})
        if self.i < len(s) and s[self.i] == "[":
            depth = 0
            while True:
                ch = s[self.i]
                if ch == "[":
                    depth += 1
                elif ch == "]":
                    depth -= 1
                    if depth == 0:
                        self.i += 1
                        break
                self.i += 1
        if self.i < len(s) and s[self.i] == "(":
            self.i += 1
            if name == "Object":
                cls = _ident.match(s, self.i)
                self.i = cls.end()
                props = {}
                while self.peek() != ")":
                    k = self.value()
                    self.ws()
                    self.i += 1  # ':'
                    props[k] = self.value()
                self.i += 1
                return GType("Object", [cls.group(0), props])
            args = []
            while self.peek() != ")":
                args.append(self.value())
            self.i += 1
            if name == "ExtResource":
                return ExtRef(args[0])
            if name == "SubResource":
                return SubRef(args[0])
            if name == "NodePath":
                return NodePathV(args[0] if args else "")
            if name == "StringName":
                return StringNameV(args[0])
            if name in ("Array", "Dictionary"):
                return args[0] if args else ([] if name == "Array" else {})
            if name == "PackedByteArray" and len(args) == 1 and isinstance(args[0], str):
                return base64.b64decode(args[0])
            if name.startswith("Packed") and name.endswith("Array"):
                return _pack(name, args)
            return GType(name, args)
        return GType(name, [])  # bare identifier (rare)


def _pack(name, args):
    if name in ("PackedVector2Array",):
        return [(args[i], args[i + 1]) for i in range(0, len(args), 2)]
    if name in ("PackedVector3Array",):
        return [tuple(args[i:i + 3]) for i in range(0, len(args), 3)]
    if name == "PackedColorArray":
        return [tuple(args[i:i + 4]) for i in range(0, len(args), 4)]
    return list(args)


class Section:
    __slots__ = ("kind", "attrs", "props", "order")

    def __init__(self, kind, attrs):
        self.kind = kind
        self.attrs = attrs
        self.props = {}
        self.order = []

    def get(self, k, d=None):
        return self.props.get(k, d)

    def __repr__(self):
        return "[%s %s]" % (self.kind, self.attrs)


def parse_text(text):
    """Split a Godot text file into sections with parsed properties."""
    p = _P(text)
    sections = []
    cur = None
    n = len(text)
    while True:
        # skip blank lines / comments
        while p.i < n and text[p.i] in " \t\r\n":
            p.i += 1
        if p.i >= n:
            break
        if text[p.i] == ";":
            while p.i < n and text[p.i] != "\n":
                p.i += 1
            continue
        if text[p.i] == "[":
            end = text.find("]\n", p.i)
            if end < 0:
                end = text.rindex("]")
            # header: [kind key=value key=value]
            hdr = text[p.i + 1:end]
            p.i = end + 1
            kind, _, rest = hdr.partition(" ")
            attrs = {}
            hp = _P(rest)
            while True:
                hp.ws()
                if hp.i >= len(rest):
                    break
                m = re.compile(r"[A-Za-z_][A-Za-z_0-9]*").match(rest, hp.i)
                key = m.group(0)
                hp.i = m.end()
                assert rest[hp.i] == "="
                hp.i += 1
                attrs[key] = hp.value()
            cur = Section(kind, attrs)
            sections.append(cur)
            continue
        # property line: key = value
        eq = text.index("=", p.i)
        key = text[p.i:eq].strip()
        p.i = eq + 1
        val = p.value()
        if cur is None:
            cur = Section("_top", {})
            sections.append(cur)
        cur.props[key] = val
        cur.order.append(key)
    return sections


class Resource:
    """A parsed .tscn or .tres file."""

    def __init__(self, path, res_path):
        self.path = path
        self.res_path = res_path
        with open(path, encoding="utf-8") as f:
            self.sections = parse_text(f.read())
        self.header = self.sections[0]
        self.ext = {}
        self.sub = {}
        self.nodes = []
        self.resource = None
        for s in self.sections:
            if s.kind == "ext_resource":
                self.ext[s.attrs["id"]] = s.attrs
            elif s.kind == "sub_resource":
                self.sub[s.attrs["id"]] = s
            elif s.kind == "node":
                self.nodes.append(s)
            elif s.kind == "resource":
                self.resource = s


class Project:
    """The whole Godot project: resolves res:// paths and caches files."""

    def __init__(self, root):
        self.root = os.path.abspath(root)
        if not os.path.exists(os.path.join(self.root, "project.godot")):
            raise SystemExit("Not a Godot project (no project.godot): %s" % self.root)
        self._cache = {}
        self.settings = {}
        cur = ""
        for s in parse_text(open(os.path.join(self.root, "project.godot"), encoding="utf-8").read()):
            for k, v in s.props.items():
                self.settings[(s.kind if s.kind != "_top" else "") + "/" + k] = v
        self._uid = None

    def fs(self, res_path):
        assert res_path.startswith("res://"), res_path
        return os.path.join(self.root, res_path[6:].replace("/", os.sep))

    def load(self, res_path):
        if res_path not in self._cache:
            self._cache[res_path] = Resource(self.fs(res_path), res_path)
        return self._cache[res_path]

    def uid_map(self):
        """uid:// -> res:// from .uid files, .import files and resource headers."""
        if self._uid is not None:
            return self._uid
        m = {}
        for dirpath, dirs, files in os.walk(self.root):
            dirs[:] = [d for d in dirs if d not in (".godot", ".git")]
            for fn in files:
                full = os.path.join(dirpath, fn)
                rel = "res://" + os.path.relpath(full, self.root).replace(os.sep, "/")
                try:
                    if fn.endswith(".uid"):
                        m[open(full, encoding="utf-8").read().strip()] = rel[:-4]
                    elif fn.endswith(".import"):
                        for line in open(full, encoding="utf-8"):
                            if line.startswith("uid="):
                                m[line.split("=", 1)[1].strip().strip('"')] = rel[:-7]
                                break
                    elif fn.endswith((".tscn", ".tres")):
                        with open(full, encoding="utf-8") as f:
                            first = f.readline()
                        mm = re.search(r'uid="(uid://[^"]+)"', first)
                        if mm:
                            m[mm.group(1)] = rel
                except (UnicodeDecodeError, OSError):
                    pass
        self._uid = m
        return m

    def resolve_ext(self, res, ext_id):
        """Path of an ext_resource, preferring the uid (paths can be stale)."""
        a = res.ext[ext_id]
        uid = a.get("uid")
        if uid and uid in self.uid_map():
            return self.uid_map()[uid]
        return a["path"]


def tile_map_cells(data):
    """Decode TileMapLayer.tile_map_data: list of (x, y, source, ax, ay, alt)."""
    if not data:
        return []
    fmt = struct.unpack_from("<H", data, 0)[0]
    if fmt != 0:
        raise ValueError("unknown tile_map_data format %d" % fmt)
    out = []
    for off in range(2, len(data) - 11, 12):
        x, y, src, ax, ay, alt = struct.unpack_from("<hhHHHH", data, off)
        out.append((x, y, src, ax, ay, alt))
    return out
