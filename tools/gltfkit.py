"""Tiny glTF 2.0 writer for procedurally authored Night Heist art.

Only what the art scripts need: meshes made of triangle primitives with
POSITION/NORMAL/TEXCOORD_0/COLOR_0, named materials, a node hierarchy with
extras, and keyframed rotation/translation animations. The output is a
standard .gltf with an embedded base64 buffer, so it opens in Blender.
"""
import base64
import json
import math
import struct

import numpy as np


class Geo:
    """Accumulates triangles grouped by material for one mesh."""

    def __init__(self):
        self.prims = {}  # material -> dict of lists

    def _p(self, mat):
        return self.prims.setdefault(mat, {"pos": [], "nrm": [], "uv": [], "col": [], "idx": []})

    def quad(self, mat, p0, p1, p2, p3, uv=((0, 0), (1, 0), (1, 1), (0, 1)), col=(1, 1, 1, 1)):
        p = self._p(mat)
        a, b, c = np.array(p0, float), np.array(p1, float), np.array(p2, float)
        n = np.cross(b - a, c - a)
        n = n / max(np.linalg.norm(n), 1e-9)
        base = len(p["pos"])
        for v, t in zip((p0, p1, p2, p3), uv):
            p["pos"].append(tuple(map(float, v)))
            p["nrm"].append(tuple(n))
            p["uv"].append(tuple(map(float, t)))
            p["col"].append(tuple(col))
        p["idx"] += [base, base + 1, base + 2, base, base + 2, base + 3]

    def tri(self, mat, p0, p1, p2, uv=((0, 0), (1, 0), (0.5, 1)), col=(1, 1, 1, 1), normals=None):
        p = self._p(mat)
        a, b, c = np.array(p0, float), np.array(p1, float), np.array(p2, float)
        n = np.cross(b - a, c - a)
        n = n / max(np.linalg.norm(n), 1e-9)
        base = len(p["pos"])
        for k, (v, t) in enumerate(zip((p0, p1, p2), uv)):
            p["pos"].append(tuple(map(float, v)))
            p["nrm"].append(tuple(normals[k]) if normals else tuple(n))
            p["uv"].append(tuple(map(float, t)))
            p["col"].append(tuple(col))
        p["idx"] += [base, base + 1, base + 2]

    def box(self, mat, lo, hi, col=(1, 1, 1, 1), uvs=1.0, faces="xXyYzZ"):
        """Axis-aligned box, outward faces, world-metre UVs scaled by uvs."""
        (x0, y0, z0), (x1, y1, z1) = lo, hi
        s = uvs
        if "X" in faces:
            self.quad(mat, (x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1),
                      ((z1 * s, y0 * s), (z0 * s, y0 * s), (z0 * s, y1 * s), (z1 * s, y1 * s)), col)
        if "x" in faces:
            self.quad(mat, (x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0),
                      ((z0 * s, y0 * s), (z1 * s, y0 * s), (z1 * s, y1 * s), (z0 * s, y1 * s)), col)
        if "Y" in faces:
            self.quad(mat, (x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0),
                      ((x0 * s, z1 * s), (x1 * s, z1 * s), (x1 * s, z0 * s), (x0 * s, z0 * s)), col)
        if "y" in faces:
            self.quad(mat, (x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1),
                      ((x0 * s, z0 * s), (x1 * s, z0 * s), (x1 * s, z1 * s), (x0 * s, z1 * s)), col)
        if "Z" in faces:
            self.quad(mat, (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1),
                      ((x0 * s, y0 * s), (x1 * s, y0 * s), (x1 * s, y1 * s), (x0 * s, y1 * s)), col)
        if "z" in faces:
            self.quad(mat, (x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0),
                      ((x1 * s, y0 * s), (x0 * s, y0 * s), (x0 * s, y1 * s), (x1 * s, y1 * s)), col)

    def cyl(self, mat, c, r, y0, y1, sides=8, col=(1, 1, 1, 1), caps=True, axis="y", r1=None):
        """Smooth-shaded cylinder (or cone with r1) along y (or x/z) centred at c."""
        r1 = r if r1 is None else r1
        p = self._p(mat)

        def place(u, v, h):
            if axis == "y":
                return (c[0] + u, h, c[2] + v)
            if axis == "x":
                return (h, c[1] + u, c[2] + v)
            return (c[0] + u, c[1] + v, h)

        def nrm(u, v):
            if axis == "y":
                return (u, 0, v)
            if axis == "x":
                return (0, u, v)
            return (u, v, 0)

        base = len(p["pos"])
        for k in range(sides + 1):
            a = 2 * math.pi * k / sides
            cu, sv = math.cos(a), math.sin(a)
            for rr, h, t in ((r, y0, 0.0), (r1, y1, 1.0)):
                p["pos"].append(place(cu * rr, sv * rr, h))
                p["nrm"].append(nrm(cu, sv))
                p["uv"].append((k / sides, t))
                p["col"].append(tuple(col))
        for k in range(sides):
            i0 = base + k * 2
            # CCW seen from outside for axis y (u=x, v=z): order depends on handedness.
            if axis == "y":
                p["idx"] += [i0, i0 + 1, i0 + 3, i0, i0 + 3, i0 + 2]
            else:
                p["idx"] += [i0, i0 + 2, i0 + 3, i0, i0 + 3, i0 + 1]
        if caps:
            for rr, h, up in ((r, y0, -1), (r1, y1, 1)):
                ctr = place(0, 0, h)
                ring = [place(math.cos(2 * math.pi * k / sides) * rr, math.sin(2 * math.pi * k / sides) * rr, h)
                        for k in range(sides)]
                for k in range(sides):
                    a, b = ring[k], ring[(k + 1) % sides]
                    tri = (ctr, b, a) if (up > 0) == (axis == "y") else (ctr, a, b)
                    self.tri(mat, *tri, col=col)


class Gltf:
    def __init__(self):
        self.nodes, self.meshes, self.materials, self.anims = [], [], [], []
        self.mat_index = {}
        self.blob = bytearray()
        self.views, self.accessors = [], []
        self.roots = []

    def material(self, name):
        if name not in self.mat_index:
            self.mat_index[name] = len(self.materials)
            self.materials.append({"name": name, "pbrMetallicRoughness": {"metallicFactor": 0.0}})
        return self.mat_index[name]

    def _acc(self, arr, ctype, typ, target=None, minmax=False):
        arr = np.ascontiguousarray(arr)
        while len(self.blob) % 4:
            self.blob.append(0)
        off = len(self.blob)
        self.blob += arr.tobytes()
        view = {"buffer": 0, "byteOffset": off, "byteLength": arr.nbytes}
        if target:
            view["target"] = target
        self.views.append(view)
        acc = {"bufferView": len(self.views) - 1, "componentType": ctype, "count": int(arr.shape[0]), "type": typ}
        if minmax:
            acc["min"] = [float(x) for x in arr.min(axis=0)]
            acc["max"] = [float(x) for x in arr.max(axis=0)]
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def mesh(self, name, geo):
        prims = []
        for mat, p in sorted(geo.prims.items()):
            if not p["idx"]:
                continue
            pos = np.array(p["pos"], np.float32)
            prim = {"attributes": {
                "POSITION": self._acc(pos, 5126, "VEC3", 34962, True),
                "NORMAL": self._acc(np.array(p["nrm"], np.float32), 5126, "VEC3", 34962),
                "TEXCOORD_0": self._acc(np.array(p["uv"], np.float32), 5126, "VEC2", 34962),
            }, "indices": self._acc(np.array(p["idx"], np.uint16), 5123, "SCALAR", 34963),
                "material": self.material(mat)}
            if any(c != (1, 1, 1, 1) for c in p["col"]):
                prim["attributes"]["COLOR_0"] = self._acc(np.array(p["col"], np.float32), 5126, "VEC4", 34962)
            prims.append(prim)
        self.meshes.append({"name": name, "primitives": prims})
        return len(self.meshes) - 1

    def node(self, name, mesh=None, t=(0, 0, 0), r=(0, 0, 0, 1), children=(), extras=None, root=False):
        n = {"name": name}
        if mesh is not None:
            n["mesh"] = mesh
        if any(t):
            n["translation"] = [float(x) for x in t]
        if tuple(r) != (0, 0, 0, 1):
            n["rotation"] = [float(x) for x in r]
        if children:
            n["children"] = list(children)
        if extras:
            n["extras"] = extras
        self.nodes.append(n)
        idx = len(self.nodes) - 1
        if root:
            self.roots.append(idx)
        return idx

    def animation(self, name, tracks, loop=True):
        """tracks: list of (node, 'rotation'|'translation', times, values)."""
        samplers, channels = [], []
        for node, path, times, values in tracks:
            ti = self._acc(np.array(times, np.float32).reshape(-1, 1), 5126, "SCALAR", minmax=True)
            vals = np.array(values, np.float32)
            vi = self._acc(vals, 5126, "VEC4" if path == "rotation" else "VEC3")
            samplers.append({"input": ti, "output": vi, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path}})
        self.anims.append({"name": name, "samplers": samplers, "channels": channels, "extras": {"loop": loop}})

    def save(self, path):
        doc = {
            "asset": {"version": "2.0", "generator": "Night Heist make_art.py"},
            "scene": 0,
            "scenes": [{"nodes": self.roots}],
            "nodes": self.nodes,
            "meshes": self.meshes,
            "materials": self.materials,
            "buffers": [{"byteLength": len(self.blob),
                         "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(self.blob)).decode()}],
            "bufferViews": self.views,
            "accessors": self.accessors,
        }
        if self.anims:
            doc["animations"] = self.anims
        with open(path, "w") as f:
            json.dump(doc, f, separators=(",", ":"))


def quat_axis(axis, ang):
    ax = np.array(axis, float)
    ax /= np.linalg.norm(ax)
    s = math.sin(ang / 2)
    return (ax[0] * s, ax[1] * s, ax[2] * s, math.cos(ang / 2))


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def euler(x=0.0, y=0.0, z=0.0):
    """Rotation applied Y, then X, then Z (radians), as a quaternion."""
    return quat_mul(quat_mul(quat_axis((0, 1, 0), y), quat_axis((1, 0, 0), x)), quat_axis((0, 0, 1), z))
