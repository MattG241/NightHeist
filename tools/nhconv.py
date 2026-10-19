#!/usr/bin/env python3
"""Night Heist asset converter.

  glTF 2.0 (.gltf/.glb) -> .nmd model (+ .nan animation clips)
  PNG                   -> .ntx texture in a GX format with mipmaps

Formats are documented in src/core/nh_assets.h. Everything is big-endian.

Usage:
  nhconv.py manifest assets/manifest.json OUTDIR     convert everything listed
  nhconv.py texture in.png out.ntx --fmt CMPR --size 256 [--clamp] [--nearest] [--nomips]
  nhconv.py model in.gltf out.nmd [--anims OUTDIR]

Models: each node's mesh is transformed into model space. If any node has
extras {"bone": true}, those nodes form the skeleton (parents before
children) and every mesh is rigidly attached to its nearest bone ancestor;
parts are split so each draw uses at most 10 bones (GX matrix slots).
"""
import base64
import json
import math
import os
import struct
import sys

import numpy as np

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None

GX_FMT = {"I8": 0x1, "IA8": 0x3, "RGB565": 0x4, "RGB5A3": 0x5, "RGBA8": 0x6, "CMPR": 0xE}
BLOCK = {"I8": (8, 4, 32), "IA8": (4, 4, 32), "RGB565": (4, 4, 32), "RGB5A3": (4, 4, 32),
         "RGBA8": (4, 4, 64), "CMPR": (8, 8, 32)}
MAX_SLOTS = 10
POS_SCALE, NRM_SCALE, UV_SCALE = 128.0, 64.0, 256.0


# --------------------------------------------------------------------------- textures

def level_size(fmt, w, h):
    bw, bh, bb = BLOCK[fmt]
    return ((w + bw - 1) // bw) * ((h + bh - 1) // bh) * bb


def pad_image(a, bw, bh):
    """Edge-clamp an (h, w, c) array up to whole blocks."""
    h, w = a.shape[:2]
    ph, pw = (-h) % bh, (-w) % bw
    if ph or pw:
        a = np.pad(a, ((0, ph), (0, pw), (0, 0)), mode="edge")
    return a


def blocks(a, bw, bh):
    """(H, W, C) -> (nby, nbx, bh, bw, C) in raster block order."""
    h, w, c = a.shape
    return a.reshape(h // bh, bh, w // bw, bw, c).swapaxes(1, 2)


def lum(rgb):
    return (rgb[..., 0] * 77 + rgb[..., 1] * 150 + rgb[..., 2] * 29 + 128) >> 8


def to565(rgb):
    r = (rgb[..., 0].astype(np.int32) * 31 + 127) // 255
    g = (rgb[..., 1].astype(np.int32) * 63 + 127) // 255
    b = (rgb[..., 2].astype(np.int32) * 31 + 127) // 255
    return (r << 11) | (g << 5) | b


def from565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=-1).astype(np.int32)


def encode_cmpr(rgba):
    """GX CMPR: 8x8 tiles, each four DXT1 4x4 blocks (TL, TR, BL, BR), big-endian."""
    a = pad_image(rgba, 8, 8).astype(np.int32)
    h, w = a.shape[:2]
    # All 4x4 blocks: (nby4, nbx4, 16, 4)
    b4 = blocks(a, 4, 4).reshape(h // 4, w // 4, 16, 4)
    rgb = b4[..., :3].astype(np.float64)
    mean = rgb.mean(axis=2, keepdims=True)
    d = rgb - mean
    # Principal axis by power iteration on the 3x3 covariance.
    cov = np.einsum("...ki,...kj->...ij", d, d)
    v = np.ones(cov.shape[:-1])
    for _ in range(6):
        v = np.einsum("...ij,...j->...i", cov, v)
        n = np.linalg.norm(v, axis=-1, keepdims=True)
        v = np.where(n > 1e-9, v / np.maximum(n, 1e-9), np.array([0.577, 0.577, 0.577]))
    proj = np.einsum("...ki,...i->...k", d, v)
    lo = mean[..., 0, :] + v * proj.min(axis=-1, keepdims=True)
    hi = mean[..., 0, :] + v * proj.max(axis=-1, keepdims=True)
    c0 = to565(np.clip(np.rint(hi), 0, 255).astype(np.int32))
    c1 = to565(np.clip(np.rint(lo), 0, 255).astype(np.int32))
    # Four-colour mode needs c0 > c1.
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    eq = c0 == c1
    c1 = np.where(eq & (c0 > 0), c0 - 1, c1)
    c0 = np.where(eq & (c0 == 0), 1, c0)
    p0, p1 = from565(c0), from565(c1)
    pal = np.stack([p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3], axis=-2)  # (..., 4, 3)
    dist = ((b4[..., None, :3] - pal[..., None, :, :]) ** 2).sum(-1)  # (..., 16, 4)
    idx = dist.argmin(-1)  # (..., 16)
    nby, nbx = h // 4, w // 4
    out = bytearray()
    for ty in range(0, nby, 2):
        for tx in range(0, nbx, 2):
            for sy, sx in ((0, 0), (0, 1), (1, 0), (1, 1)):
                by, bx = ty + sy, tx + sx
                out += struct.pack(">HH", int(c0[by, bx]), int(c1[by, bx]))
                ix = idx[by, bx].reshape(4, 4)
                for row in ix:
                    out.append(int((row[0] << 6) | (row[1] << 4) | (row[2] << 2) | row[3]))
    return bytes(out)


def encode_level(fmt, rgba):
    if fmt == "CMPR":
        return encode_cmpr(rgba)
    bw, bh, _ = BLOCK[fmt]
    a = pad_image(rgba, bw, bh).astype(np.int32)
    bl = blocks(a, bw, bh)  # (nby, nbx, bh, bw, 4)
    if fmt == "I8":
        return lum(bl[..., :3]).astype(np.uint8).tobytes()
    if fmt == "IA8":
        out = np.stack([bl[..., 3], lum(bl[..., :3])], axis=-1)
        return out.astype(np.uint8).tobytes()
    if fmt == "RGB565":
        return to565(bl[..., :3]).astype(">u2").tobytes()
    if fmt == "RGB5A3":
        r, g, b, al = bl[..., 0], bl[..., 1], bl[..., 2], bl[..., 3]
        opaque = 0x8000 | (((r * 31 + 127) // 255) << 10) | (((g * 31 + 127) // 255) << 5) | ((b * 31 + 127) // 255)
        trans = (((al * 7 + 127) // 255) << 12) | (((r * 15 + 127) // 255) << 8) | (((g * 15 + 127) // 255) << 4) | ((b * 15 + 127) // 255)
        return np.where(al >= 224, opaque, trans).astype(">u2").tobytes()
    if fmt == "RGBA8":
        out = bytearray()
        flat = bl.reshape(bl.shape[0], bl.shape[1], 16, 4)
        for by in range(flat.shape[0]):
            for bx in range(flat.shape[1]):
                px = flat[by, bx]
                out += bytes(np.stack([px[:, 3], px[:, 0]], -1).astype(np.uint8).ravel())
                out += bytes(np.stack([px[:, 1], px[:, 2]], -1).astype(np.uint8).ravel())
        return bytes(out)
    raise ValueError(fmt)


def convert_texture(src, dst, fmt="CMPR", size=None, wrap=True, filt=True, mips=True):
    img = Image.open(src).convert("RGBA")
    if size:
        w, h = (size, size) if isinstance(size, int) else size
        img = img.resize((w, h), Image.LANCZOS)
    w, h = img.size
    assert w & (w - 1) == 0 and h & (h - 1) == 0, "%s: power-of-two sizes only" % src
    levels = []
    cur = img
    while True:
        levels.append(np.asarray(cur, dtype=np.uint8))
        if not mips or (cur.size[0] == 1 and cur.size[1] == 1) or len(levels) >= 11:
            break
        # Stop at 4x4 for block formats: smaller levels just waste padded blocks.
        if min(cur.size) <= 4:
            break
        cur = cur.resize((max(cur.size[0] // 2, 1), max(cur.size[1] // 2, 1)), Image.BOX)
    data = b"".join(encode_level(fmt, lv) for lv in levels)
    expect = 0
    lw, lh = w, h
    for _ in levels:
        expect += level_size(fmt, lw, lh)
        lw, lh = max(lw // 2, 1), max(lh // 2, 1)
    assert len(data) == expect, (src, len(data), expect)
    hdr = b"NTX1" + struct.pack(">HHBBBBI", w, h, GX_FMT[fmt], len(levels), 1 if wrap else 0, 1 if filt else 0,
                                len(data))
    hdr += b"\0" * (32 - len(hdr))
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with open(dst, "wb") as f:
        f.write(hdr + data)
    return len(hdr) + len(data)


# --------------------------------------------------------------------------- glTF

COMP = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


class Gltf:
    def __init__(self, path):
        self.dir = os.path.dirname(path)
        raw = open(path, "rb").read()
        bin_chunk = None
        if raw[:4] == b"glTF":
            jlen = struct.unpack_from("<I", raw, 12)[0]
            self.j = json.loads(raw[20:20 + jlen])
            off = 20 + jlen
            if off < len(raw):
                blen = struct.unpack_from("<I", raw, off)[0]
                bin_chunk = raw[off + 8:off + 8 + blen]
        else:
            self.j = json.loads(raw)
        self.buffers = []
        for b in self.j.get("buffers", []):
            uri = b.get("uri")
            if uri is None:
                self.buffers.append(bin_chunk)
            elif uri.startswith("data:"):
                self.buffers.append(base64.b64decode(uri.split(",", 1)[1]))
            else:
                self.buffers.append(open(os.path.join(self.dir, uri), "rb").read())

    def accessor(self, i):
        a = self.j["accessors"][i]
        bv = self.j["bufferViews"][a["bufferView"]]
        dt = np.dtype(COMP[a["componentType"]]).newbyteorder("<")
        n = NCOMP[a["type"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = bv.get("byteStride", 0) or dt.itemsize * n
        buf = self.buffers[bv["buffer"]]
        cnt = a["count"]
        arr = np.ndarray((cnt, n), dtype=dt, buffer=buf, offset=off, strides=(stride, dt.itemsize))
        arr = arr.astype(np.float64 if dt.kind == "f" else np.int64)
        if a.get("normalized"):
            arr = arr / float(np.iinfo(COMP[a["componentType"]]).max)
        return arr

    def node_local(self, i):
        n = self.j["nodes"][i]
        if "matrix" in n:
            return np.array(n["matrix"], dtype=np.float64).reshape(4, 4).T
        t = n.get("translation", [0, 0, 0])
        r = n.get("rotation", [0, 0, 0, 1])
        s = n.get("scale", [1, 1, 1])
        return trs(t, r, s)


def quat_to_mat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
                     [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
                     [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])


def trs(t, r, s):
    m = np.eye(4)
    m[:3, :3] = quat_to_mat(r) * np.array(s)[None, :]
    m[:3, 3] = t
    return m


def slerp(a, b, t):
    a, b = np.array(a, dtype=np.float64), np.array(b, dtype=np.float64)
    d = float(np.dot(a, b))
    if d < 0:
        b, d = -b, -d
    if d > 0.9995:
        r = a + (b - a) * t
    else:
        th = math.acos(d)
        r = (math.sin((1 - t) * th) * a + math.sin(t * th) * b) / math.sin(th)
    return r / np.linalg.norm(r)


def convert_model(src, dst, anim_dir=None, merge_parts=True):
    g = Gltf(src)
    nodes = g.j["nodes"]
    scene = g.j["scenes"][g.j.get("scene", 0)]
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    order = []

    def walk(i):
        order.append(i)
        for c in nodes[i].get("children", []):
            walk(c)

    for r in scene["nodes"]:
        walk(r)
    glob = {}
    for i in order:
        loc = g.node_local(i)
        glob[i] = glob[parent[i]] @ loc if i in parent else loc

    bones = [i for i in order if nodes[i].get("extras", {}).get("bone")]
    bone_index = {n: k for k, n in enumerate(bones)}
    assert len(bones) <= 24, "too many bones"

    def owner_bone(i):
        while i is not None:
            if i in bone_index:
                return bone_index[i]
            i = parent.get(i)
        return 0

    # Gather triangles grouped by material, each vertex tagged with its bone.
    groups = {}
    for i in order:
        n = nodes[i]
        if "mesh" not in n:
            continue
        M = glob[i]
        Nm = np.linalg.inv(M[:3, :3]).T
        b = owner_bone(i) if bones else 0
        for prim in g.j["meshes"][n["mesh"]]["primitives"]:
            if prim.get("mode", 4) != 4:
                continue
            at = prim["attributes"]
            pos = g.accessor(at["POSITION"])
            nrm = g.accessor(at["NORMAL"]) if "NORMAL" in at else np.tile([0.0, 1.0, 0.0], (len(pos), 1))
            uv = g.accessor(at["TEXCOORD_0"]) if "TEXCOORD_0" in at else np.zeros((len(pos), 2))
            col = g.accessor(at["COLOR_0"]) if "COLOR_0" in at else None
            idx = g.accessor(prim["indices"])[:, 0] if "indices" in prim else np.arange(len(pos))
            mat = "default"
            if "material" in prim:
                mat = g.j["materials"][prim["material"]].get("name", "default")
            wp = (M[:3, :3] @ pos.T).T + M[:3, 3]
            wn = (Nm @ nrm.T).T
            wn /= np.maximum(np.linalg.norm(wn, axis=1, keepdims=True), 1e-9)
            if col is not None and col.shape[1] == 3:
                col = np.hstack([col, np.ones((len(col), 1))])
            grp = groups.setdefault(mat, {"tris": []})
            for t in range(0, len(idx), 3):
                tri = []
                for k in range(3):
                    v = idx[t + k]
                    c = tuple(col[v]) if col is not None else (1, 1, 1, 1)
                    tri.append((tuple(wp[v]), tuple(wn[v]), tuple(uv[v]), c, b))
                grp["tris"].append(tri)

    has_colors = any(any(v[3] != (1, 1, 1, 1) for tri in grp["tris"] for v in tri) for grp in groups.values())
    parts = []
    for mat, grp in sorted(groups.items()):
        # Split into batches of at most MAX_SLOTS bones.
        by_bone = {}
        for tri in grp["tris"]:
            by_bone.setdefault(tri[0][4], []).append(tri)
        batch_bones, batch_tris = [], []
        for b in sorted(by_bone):
            if len(batch_bones) == MAX_SLOTS:
                parts.append((mat, batch_bones, batch_tris))
                batch_bones, batch_tris = [], []
            batch_bones.append(b)
            batch_tris += by_bone[b]
        if batch_tris:
            parts.append((mat, batch_bones, batch_tris))

    out_parts = []
    allp = []
    for mat, bbones, tris in parts:
        slot = {b: k for k, b in enumerate(bbones)}
        verts, cols, index, lookup = [], [], [], {}
        for tri in tris:
            for p, nm, uv, c, b in tri:
                q = (tuple(int(round(x * POS_SCALE)) for x in p), tuple(int(round(x * NRM_SCALE)) for x in nm),
                     slot[b] if bones else 0, tuple(int(round(x * UV_SCALE)) for x in uv),
                     tuple(int(round(x * 255)) for x in c))
                if q not in lookup:
                    if len(verts) >= 65535:
                        raise ValueError("part too large")
                    lookup[q] = len(verts)
                    verts.append(q)
                    allp.append(p)
                index.append(lookup[q])
        out_parts.append((mat, [bones_ for bones_ in bbones] if bones else [], verts, index))

    allp = np.array(allp) if allp else np.zeros((1, 3))
    bmin, bmax = allp.min(axis=0), allp.max(axis=0)

    def clamp16(v):
        return max(-32768, min(32767, v))

    def clamp8(v):
        return max(-128, min(127, v))

    body = bytearray()
    body += b"NMD1" + struct.pack(">HHHH", 1 if has_colors else 0, len(bones), len(out_parts), 0)
    body += struct.pack(">6f", *bmin, *bmax)
    body += b"\0" * (48 - len(body))
    for i in bones:
        n = nodes[i]
        name = n.get("name", "bone%d" % i).encode()[:31]
        par = bone_index.get(parent.get(i), -1) if parent.get(i) in bone_index else -1
        t = n.get("translation", [0, 0, 0])
        r = n.get("rotation", [0, 0, 0, 1])
        body += name.ljust(32, b"\0") + struct.pack(">hH3f4f", par, 0, *t, *r)
    for mat, sl, verts, index in out_parts:
        body += mat.encode()[:31].ljust(32, b"\0") + struct.pack(">II", len(verts), len(index))
        slots = list(sl) + [0] * (MAX_SLOTS - len(sl))
        body += struct.pack(">B10B", len(sl), *slots)
        body += b"\0" * (64 - 32 - 8 - 11)
    for mat, sl, verts, index in out_parts:
        vb = bytearray()
        for p, nm, b, uv, c in verts:
            vb += struct.pack(">3h3bB2h", *(clamp16(x) for x in p), *(clamp8(x) for x in nm), b,
                              *(clamp16(x) for x in uv))
        vb += b"\0" * ((-len(vb)) % 4)
        body += vb
        if has_colors:
            for v in verts:
                body += bytes(max(0, min(255, x)) for x in v[4])
        ib = struct.pack(">%dH" % len(index), *index)
        ib += b"\0" * ((-len(ib)) % 4)
        body += ib
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with open(dst, "wb") as f:
        f.write(body)

    written = [dst]
    if anim_dir and bones:
        for anim in g.j.get("animations", []):
            written.append(convert_anim(g, anim, bones, os.path.join(anim_dir, anim["name"] + ".nan")))
    return written


def convert_anim(g, anim, bones, dst, fps=30.0):
    nodes = g.j["nodes"]
    tracks = {}
    tmax = 0.0
    for ch in anim["channels"]:
        tgt = ch["target"]
        if tgt.get("node") not in bones or tgt["path"] not in ("rotation", "translation"):
            continue
        smp = anim["samplers"][ch["sampler"]]
        times = g.accessor(smp["input"])[:, 0]
        vals = g.accessor(smp["output"])
        tracks[(tgt["node"], tgt["path"])] = (times, vals)
        tmax = max(tmax, float(times[-1]))
    loop = bool(anim.get("extras", {}).get("loop", True))
    nframes = max(1, int(round(tmax * fps)) + (0 if loop else 1))

    def sample(node, path, t, rest):
        if (node, path) not in tracks:
            return rest
        times, vals = tracks[(node, path)]
        if t <= times[0]:
            return vals[0]
        if t >= times[-1]:
            return vals[-1]
        k = int(np.searchsorted(times, t) - 1)
        a = (t - times[k]) / (times[k + 1] - times[k])
        if path == "rotation":
            return slerp(vals[k], vals[k + 1], a)
        return vals[k] + (vals[k + 1] - vals[k]) * a

    out = bytearray(b"NAN1" + struct.pack(">HHfB3x", len(bones), nframes, fps, 1 if loop else 0))
    out += anim["name"].encode()[:31].ljust(32, b"\0")
    for f in range(nframes):
        t = f / fps
        for b in bones:
            n = nodes[b]
            q = sample(b, "rotation", t, n.get("rotation", [0, 0, 0, 1]))
            p = sample(b, "translation", t, n.get("translation", [0, 0, 0]))
            q = np.array(q) / max(np.linalg.norm(q), 1e-9)
            out += struct.pack(">4h3h", *(int(round(x * 32767)) for x in q),
                               *(max(-32768, min(32767, int(round(x * 256)))) for x in p))
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with open(dst, "wb") as fh:
        fh.write(out)
    return dst


# --------------------------------------------------------------------------- manifest

def run_manifest(path, outdir):
    man = json.load(open(path))
    base = os.path.dirname(path)
    total = 0
    for name, t in sorted(man.get("textures", {}).items()):
        dst = os.path.join(outdir, "tex", name + ".ntx")
        total += convert_texture(os.path.join(base, t["src"]), dst, t.get("fmt", "CMPR"), t.get("size"),
                                 t.get("wrap", True), t.get("filter", True), t.get("mips", True))
    for name, m in sorted(man.get("models", {}).items()):
        files = convert_model(os.path.join(base, m["src"]), os.path.join(outdir, "mdl", name + ".nmd"),
                              os.path.join(outdir, "anim") if m.get("anims") else None)
        total += sum(os.path.getsize(f) for f in files)
    print("converted %d textures, %d models -> %s (%.1f KB)" % (len(man.get("textures", {})),
                                                               len(man.get("models", {})), outdir, total / 1024))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "manifest":
        run_manifest(argv[2], argv[3])
    elif cmd == "texture":
        args = argv[4:]
        fmt = args[args.index("--fmt") + 1] if "--fmt" in args else "CMPR"
        size = int(args[args.index("--size") + 1]) if "--size" in args else None
        convert_texture(argv[2], argv[3], fmt, size, "--clamp" not in args, "--nearest" not in args,
                        "--nomips" not in args)
    elif cmd == "model":
        args = argv[4:]
        convert_model(argv[2], argv[3], args[args.index("--anims") + 1] if "--anims" in args else None)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
