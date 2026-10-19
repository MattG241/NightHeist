#!/usr/bin/env python3
"""Generate Night Heist's procedural art sources.

Writes, under assets/src/:
  props/*.gltf   furniture, fixtures, loot and doors, built from primitives
  chars/*.gltf   guard and dog with rigid skeletons and keyframed clips
  tex/*.png      procedural textures (glass, screens, paintings, cookie, decals)
and assets/manifest.json (input to nhconv.py), plus the generated C table
src/core/nh_proptab.c/.h so prop collision sizes and gameplay flags come from
the same source as the meshes.

All output is original work for Night Heist (CC0). Surface textures from
Poly Haven come from tools/fetch_polyhaven.py.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gltfkit import Geo, Gltf, euler  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "src")
RNG = np.random.default_rng(1234)

# Prop flags (must match LV_P_* in src/core/nh_level.h)
SOLID, OCCL, HIDE, INTER = 1, 2, 4, 8

# --------------------------------------------------------------------------- textures


def save_png(name, arr):
    os.makedirs(os.path.join(SRC, "tex"), exist_ok=True)
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).save(os.path.join(SRC, "tex", name + ".png"))


def noise(size, scale, octaves=4):
    out = np.zeros((size, size))
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = max(2, int(size / scale * 2 ** o))
        g = RNG.random((n + 1, n + 1))
        g[-1, :] = g[0, :]
        g[:, -1] = g[:, 0]
        img = Image.fromarray((g * 255).astype(np.uint8)).resize((size, size), Image.BICUBIC)
        out += np.asarray(img, float) / 255 * amp
        tot += amp
        amp *= 0.5
    return out / tot


def make_textures():
    s = 128
    yy, xx = np.mgrid[0:s, 0:s] / (s - 1)
    # Glass: faint blue, alpha 70 with streaks.
    g = np.zeros((s, s, 4))
    streak = noise(s, 64, 2)
    g[..., 0], g[..., 1], g[..., 2] = 150 + streak * 40, 180 + streak * 40, 200 + streak * 40
    g[..., 3] = 60 + streak * 50
    save_png("glass", g)
    # Screen: dark with glowing UI rows.
    sc = np.zeros((s, s, 3))
    sc[...] = (10, 18, 26)
    for r in range(10, s - 8, 9):
        w = int(RNG.integers(30, 100))
        sc[r:r + 4, 8:8 + w] = (40, 140, 170) if r % 2 else (60, 170, 90)
    sc[:6, :] = (30, 60, 110)
    save_png("screen", sc)
    # Plastic, paper, skin, gold, cash: subtle variation.
    for name, base, var in (("plastic", (205, 205, 200), 10), ("paper", (235, 232, 222), 8),
                            ("skin", (196, 150, 120), 14), ("gold", (215, 170, 60), 40),
                            ("cloth", (200, 200, 200), 30)):
        n = noise(s, 32)[..., None]
        save_png(name, np.array(base)[None, None, :] + (n - 0.5) * var * 2)
    cash = np.zeros((s, s, 3))
    cash[...] = (95, 140, 95)
    cash[::16, :] = (60, 100, 60)
    cash[:, ::32] = (60, 100, 60)
    save_png("cash", cash + (noise(s, 16)[..., None] - 0.5) * 30)
    # Paintings: six abstract canvases with frames.
    for k in range(6):
        p = np.zeros((s, s, 3))
        c1, c2 = RNG.integers(30, 220, 3), RNG.integers(30, 220, 3)
        t = noise(s, 48, 3)
        p[...] = c1 * (1 - t[..., None]) + c2 * t[..., None]
        img = Image.fromarray(p.astype(np.uint8))
        d = ImageDraw.Draw(img)
        for _ in range(int(RNG.integers(3, 8))):
            x0, y0 = RNG.integers(10, 100, 2)
            r = int(RNG.integers(8, 35))
            col = tuple(int(v) for v in RNG.integers(20, 240, 3))
            if RNG.random() < 0.5:
                d.ellipse([x0, y0, x0 + r, y0 + r], fill=col)
            else:
                d.rectangle([x0, y0, x0 + r * 2, y0 + r // 2], fill=col)
        img = img.filter(ImageFilter.GaussianBlur(1.2))
        a = np.asarray(img, float)
        a[:7, :], a[-7:, :], a[:, :7], a[:, -7:] = (120, 90, 40), (120, 90, 40), (120, 90, 40), (120, 90, 40)
        save_png("painting%d" % k, a)
    # Exit sign, keypad, fuse panel labels.
    ex = np.zeros((64, 128, 3))
    ex[...] = (20, 150, 60)
    img = Image.fromarray(ex.astype(np.uint8))
    ImageDraw.Draw(img).text((38, 22), "EXIT", fill=(230, 255, 230))
    save_png("sign_exit", np.asarray(img.resize((128, 64))))

    # Torch cookie (I8): soft disc, bright hotspot, faint ring and lens dirt.
    s = 128
    yy, xx = np.mgrid[0:s, 0:s]
    r = np.hypot(xx - (s - 1) / 2, yy - (s - 1) / 2) / (s / 2)
    hot = np.exp(-(r / 0.28) ** 2) * 0.55
    body = np.clip(1 - (r / 0.92) ** 4, 0, 1) * 0.55
    ring = np.exp(-((r - 0.62) / 0.06) ** 2) * 0.12
    dirt = (noise(s, 24, 3) - 0.5) * 0.12
    c = np.clip(hot + body + ring + dirt * body, 0, 1)
    c[r > 0.97] = 0
    c[0, :], c[-1, :], c[:, 0], c[:, -1] = 0, 0, 0, 0
    save_png("cookie", np.stack([c * 255] * 3, -1))
    # Soft glow sprite (IA8 white with alpha) and blob shadow.
    g = np.clip(1 - r, 0, 1) ** 2
    save_png("glow", np.dstack([np.full((s, s), 255)] * 3 + [g * 255]))
    b = np.clip(1 - r, 0, 1) ** 1.5
    save_png("blob", np.dstack([np.zeros((s, s))] * 3 + [b * 200]))
    d = np.exp(-(r / 0.35) ** 2)
    save_png("dust", np.dstack([np.full((s, s), 255)] * 3 + [d * 255]))
    # Laser beam: horizontal soft line.
    lb = np.exp(-((yy - s / 2) / 6.0) ** 2)
    save_png("beam", np.dstack([np.full((s, s), 255)] * 3 + [lb * 255]))
    # Vision cone wedge for debug views and the minimap.
    save_png("white", np.full((8, 8, 3), 255))
    save_png("glow_panel", np.full((16, 16, 3), (255, 246, 225)))


# --------------------------------------------------------------------------- props

PROPS = []  # (name, half_extents, flags, builder)


def prop(name, size, flags=0):
    """Decorator: size is the collision box (w, h, d) sitting on the floor."""
    def deco(fn):
        PROPS.append((name, size, flags, fn))
        return fn
    return deco


def col(r, g, b):
    return (r / 255, g / 255, b / 255, 1.0)


DARKWOOD, LIGHTWOOD, GREY, BLACK = col(110, 80, 60), col(190, 160, 120), col(150, 150, 155), col(40, 40, 45)


@prop("desk", (1.6, 0.76, 0.8), SOLID | HIDE)
def _(g):
    g.box("wood", (-0.8, 0.72, -0.4), (0.8, 0.76, 0.4), LIGHTWOOD)
    for x in (-0.76, 0.72):
        g.box("wood", (x, 0, -0.38), (x + 0.04, 0.72, 0.38), LIGHTWOOD)
    g.box("wood", (-0.72, 0.3, 0.32), (0.72, 0.72, 0.36), LIGHTWOOD)
    g.box("metal", (0.35, 0.45, -0.36), (0.7, 0.7, 0.3), GREY)


@prop("office_chair", (0.6, 1.0, 0.6), SOLID)
def _(g):
    g.box("fabric", (-0.24, 0.42, -0.24), (0.24, 0.5, 0.24), col(60, 60, 70))
    g.box("fabric", (-0.24, 0.5, 0.18), (0.24, 1.0, 0.25), col(60, 60, 70))
    g.cyl("metal", (0, 0, 0), 0.03, 0.08, 0.42, 6, BLACK)
    g.box("metal", (-0.28, 0.0, -0.03), (0.28, 0.08, 0.03), BLACK)
    g.box("metal", (-0.03, 0.0, -0.28), (0.03, 0.08, 0.28), BLACK)


@prop("filing_cabinet", (0.5, 1.3, 0.6), SOLID)
def _(g):
    g.box("metal", (-0.25, 0, -0.3), (0.25, 1.3, 0.3), GREY)
    for k in range(4):
        y = 0.08 + k * 0.31
        g.box("metal", (-0.06, y + 0.2, -0.32), (0.06, y + 0.23, -0.3), BLACK)


@prop("bookshelf", (1.0, 2.0, 0.35), SOLID | OCCL)
def _(g):
    g.box("wood", (-0.5, 0, -0.17), (0.5, 2.0, 0.17), DARKWOOD, faces="xXyYZ")
    for k in range(5):
        y = 0.05 + k * 0.4
        g.box("wood", (-0.47, y, -0.17), (0.47, y + 0.03, 0.14), DARKWOOD)
        x = -0.45
        while x < 0.4:
            w = 0.04 + RNG.random() * 0.05
            h = 0.22 + RNG.random() * 0.12
            g.box("paper", (x, y + 0.03, -0.12), (x + w, y + 0.03 + h, 0.12),
                  col(*(int(v) for v in RNG.integers(60, 200, 3))))
            x += w + 0.005
    g.box("wood", (-0.5, 0, 0.14), (0.5, 2.0, 0.17), DARKWOOD, faces="z")


@prop("sofa", (2.0, 0.85, 0.9), SOLID)
def _(g):
    c = col(110, 70, 55)
    g.box("leather", (-1.0, 0.1, -0.45), (1.0, 0.45, 0.45), c)
    g.box("leather", (-1.0, 0.45, 0.2), (1.0, 0.85, 0.45), c)
    g.box("leather", (-1.0, 0.45, -0.45), (-0.82, 0.65, 0.2), c)
    g.box("leather", (0.82, 0.45, -0.45), (1.0, 0.65, 0.2), c)


@prop("armchair", (0.9, 0.9, 0.9), SOLID)
def _(g):
    c = col(120, 60, 50)
    g.box("leather", (-0.45, 0.1, -0.45), (0.45, 0.45, 0.45), c)
    g.box("leather", (-0.45, 0.45, 0.25), (0.45, 0.9, 0.45), c)
    g.box("leather", (-0.45, 0.45, -0.45), (-0.33, 0.68, 0.25), c)
    g.box("leather", (0.33, 0.45, -0.45), (0.45, 0.68, 0.25), c)


@prop("coffee_table", (1.2, 0.45, 0.6), SOLID)
def _(g):
    g.box("veneer", (-0.6, 0.4, -0.3), (0.6, 0.45, 0.3))
    for x in (-0.55, 0.5):
        for z in (-0.25, 0.2):
            g.box("veneer", (x, 0, z), (x + 0.05, 0.4, z + 0.05))


@prop("bed", (1.6, 0.6, 2.1), SOLID)
def _(g):
    g.box("wood", (-0.8, 0, -1.05), (0.8, 0.35, 1.05), DARKWOOD)
    g.box("cloth", (-0.78, 0.35, -1.0), (0.78, 0.55, 1.0), col(220, 215, 205))
    g.box("cloth", (-0.6, 0.55, 0.65), (0.6, 0.65, 0.95), col(240, 240, 240))
    g.box("wood", (-0.8, 0, 1.0), (0.8, 1.1, 1.05), DARKWOOD)


@prop("wardrobe", (1.2, 2.1, 0.6), SOLID | OCCL | HIDE | INTER)
def _(g):
    g.box("veneer", (-0.6, 0, -0.3), (0.6, 2.1, 0.3))
    g.box("metal", (-0.04, 0.95, -0.32), (-0.01, 1.15, -0.3), GREY)
    g.box("metal", (0.01, 0.95, -0.32), (0.04, 1.15, -0.3), GREY)


@prop("locker", (0.5, 1.9, 0.5), SOLID | OCCL | HIDE | INTER)
def _(g):
    g.box("metal", (-0.25, 0, -0.25), (0.25, 1.9, 0.25), col(90, 110, 130))
    for y in (1.6, 1.65, 1.7):
        g.box("metal", (-0.15, y, -0.27), (0.15, y + 0.02, -0.25), BLACK)


@prop("plant", (0.5, 1.3, 0.5), SOLID)
def _(g):
    g.cyl("plastic", (0, 0, 0), 0.2, 0, 0.4, 8, col(80, 60, 50), r1=0.24)
    for k in range(7):
        a = k * 2.4
        x, z = math.cos(a) * 0.12, math.sin(a) * 0.12
        g.box("cloth", (x - 0.12, 0.4 + k * 0.1, z - 0.04), (x + 0.12, 0.45 + k * 0.1 + 0.3, z + 0.04),
              col(50, 110 + k * 8, 50))


@prop("crate", (1.0, 1.0, 1.0), SOLID | OCCL)
def _(g):
    g.box("slats", (-0.5, 0, -0.5), (0.5, 1.0, 0.5), uvs=0.8)


@prop("crate_stack", (1.2, 2.2, 1.2), SOLID | OCCL)
def _(g):
    g.box("slats", (-0.6, 0, -0.6), (0.6, 1.2, 0.6), uvs=0.8)
    g.box("slats", (-0.45, 1.2, -0.5), (0.5, 2.2, 0.45), uvs=0.8)


@prop("pallet_rack", (3.6, 3.0, 1.0), SOLID | OCCL)
def _(g):
    org = col(220, 110, 30)
    for x in (-1.8, -0.05, 1.7):
        for z in (-0.5, 0.42):
            g.box("metal", (x, 0, z), (x + 0.08, 3.0, z + 0.08), col(40, 70, 140))
    for y in (0.1, 1.1, 2.1):
        g.box("metal", (-1.8, y, -0.5), (1.8, y + 0.1, 0.5), org)
        for k in range(3):
            x = -1.6 + k * 1.15
            g.box("slats", (x, y + 0.1, -0.4), (x + 0.9, y + 0.1 + 0.7, 0.4), uvs=0.8)


@prop("computer", (0.5, 0.45, 0.3), 0)
def _(g):
    g.box("plastic", (-0.25, 0.08, -0.03), (0.25, 0.4, 0.03), BLACK)
    g.quad("screen", (-0.23, 0.1, -0.031), (0.23, 0.1, -0.031), (0.23, 0.38, -0.031), (-0.23, 0.38, -0.031), facing=(0, 0, -1))
    g.box("plastic", (-0.03, 0, -0.03), (0.03, 0.1, 0.03), BLACK)
    g.box("plastic", (-0.12, 0, -0.08), (0.12, 0.01, 0.08), BLACK)
    g.box("plastic", (-0.22, 0, -0.3), (0.22, 0.02, -0.15), col(50, 50, 55))


@prop("server_rack", (0.7, 2.0, 1.0), SOLID | OCCL)
def _(g):
    g.box("metal", (-0.35, 0, -0.5), (0.35, 2.0, 0.5), BLACK)
    for k in range(14):
        y = 0.15 + k * 0.12
        g.quad("screen", (-0.3, y, -0.51), (0.3, y, -0.51), (0.3, y + 0.08, -0.51), (-0.3, y + 0.08, -0.51), facing=(0, 0, -1))


@prop("safe", (0.7, 1.0, 0.7), SOLID | INTER)
def _(g):
    g.box("metal", (-0.35, 0, -0.35), (0.35, 1.0, 0.35), col(60, 65, 70))
    g.cyl("metal", (0.0, 0.62, -0.36), 0.09, -0.4, -0.36, 12, col(170, 170, 160), axis="z")
    g.box("metal", (0.18, 0.4, -0.38), (0.24, 0.6, -0.35), col(170, 170, 160))


@prop("kitchen_counter", (2.0, 0.92, 0.65), SOLID)
def _(g):
    g.box("wood", (-1.0, 0, -0.3), (1.0, 0.88, 0.32), col(230, 230, 225))
    g.box("granite", (-1.0, 0.88, -0.33), (1.0, 0.92, 0.33))
    g.box("metal", (-0.3, 0.92, -0.2), (0.2, 0.93, 0.15), GREY)


@prop("fridge", (0.8, 1.9, 0.75), SOLID | OCCL)
def _(g):
    g.box("plastic", (-0.4, 0, -0.37), (0.4, 1.9, 0.37), col(230, 230, 230))
    g.box("metal", (0.3, 0.9, -0.4), (0.33, 1.5, -0.37), GREY)


@prop("coffee_machine", (0.35, 0.45, 0.35), 0)
def _(g):
    g.box("plastic", (-0.17, 0, -0.17), (0.17, 0.45, 0.17), BLACK)
    g.box("metal", (-0.08, 0.1, -0.19), (0.08, 0.2, -0.17), GREY)


@prop("water_cooler", (0.35, 1.25, 0.35), SOLID)
def _(g):
    g.box("plastic", (-0.17, 0, -0.17), (0.17, 0.95, 0.17), col(220, 220, 220))
    g.cyl("glass", (0, 0, 0), 0.13, 0.95, 1.25, 10, col(150, 200, 255))


@prop("display_case", (1.0, 1.1, 1.0), SOLID | INTER)
def _(g):
    g.box("veneer", (-0.5, 0, -0.5), (0.5, 0.8, 0.5))
    g.box("glass", (-0.48, 0.8, -0.48), (0.48, 1.1, 0.48))


@prop("pedestal", (0.6, 1.1, 0.6), SOLID | INTER)
def _(g):
    g.box("marble", (-0.3, 0, -0.3), (0.3, 1.0, 0.3))
    g.box("marble", (-0.33, 1.0, -0.33), (0.33, 1.05, 0.33))


@prop("painting", (1.2, 0.9, 0.06), INTER)
def _(g):
    g.box("wood", (-0.6, 0, -0.03), (0.6, 0.9, 0.03), col(130, 95, 45))
    g.quad("painting0", (-0.55, 0.05, -0.031), (0.55, 0.05, -0.031), (0.55, 0.85, -0.031), (-0.55, 0.85, -0.031), facing=(0, 0, -1))


@prop("conference_table", (3.0, 0.76, 1.2), SOLID)
def _(g):
    g.box("veneer", (-1.5, 0.71, -0.6), (1.5, 0.76, 0.6))
    g.box("veneer", (-1.1, 0, -0.3), (-0.9, 0.71, 0.3))
    g.box("veneer", (0.9, 0, -0.3), (1.1, 0.71, 0.3))


@prop("reception_desk", (2.4, 1.1, 0.8), SOLID)
def _(g):
    g.box("veneer", (-1.2, 0, -0.4), (1.2, 1.1, -0.3))
    g.box("veneer", (-1.2, 0.72, -0.3), (1.2, 0.76, 0.4))
    g.box("veneer", (-1.2, 0, -0.3), (-1.1, 0.72, 0.4))
    g.box("veneer", (1.1, 0, -0.3), (1.2, 0.72, 0.4))


@prop("bank_counter", (2.0, 1.1, 0.6), SOLID)
def _(g):
    g.box("marble", (-1.0, 0, -0.3), (1.0, 1.05, 0.3))
    g.box("granite", (-1.0, 1.05, -0.32), (1.0, 1.1, 0.32))
    g.box("glass", (-1.0, 1.1, 0.0), (1.0, 1.9, 0.02))


@prop("toilet_stall", (1.0, 2.0, 1.5), SOLID | OCCL | HIDE | INTER)
def _(g):
    c = col(170, 180, 175)
    g.box("plastic", (-0.5, 0.1, -0.75), (-0.47, 2.0, 0.75), c)
    g.box("plastic", (0.47, 0.1, -0.75), (0.5, 2.0, 0.75), c)
    g.box("plastic", (-0.47, 0.1, -0.75), (0.47, 2.0, -0.72), c)
    g.box("plastic", (-0.2, 0, 0.3), (0.2, 0.45, 0.7), col(240, 240, 240))


@prop("sink", (0.6, 0.9, 0.5), SOLID)
def _(g):
    g.box("plastic", (-0.3, 0.75, -0.25), (0.3, 0.9, 0.25), col(240, 240, 240))
    g.box("plastic", (-0.08, 0, -0.05), (0.08, 0.75, 0.15), col(240, 240, 240))
    g.box("metal", (-0.02, 0.9, 0.12), (0.02, 1.05, 0.16), GREY)


@prop("wine_rack", (1.5, 2.0, 0.4), SOLID | OCCL)
def _(g):
    g.box("wood", (-0.75, 0, -0.2), (0.75, 2.0, 0.2), DARKWOOD, faces="xXyYZ")
    for r in range(6):
        for c in range(7):
            g.cyl("glass", (-0.6 + c * 0.2, 0.2 + r * 0.3, 0), 0.04, -0.2, 0.15, 6, col(40, 70, 40), axis="z")


@prop("barrel", (0.6, 0.9, 0.6), SOLID)
def _(g):
    g.cyl("wood", (0, 0, 0), 0.3, 0, 0.9, 10, DARKWOOD)


@prop("trash_bin", (0.4, 0.6, 0.4), SOLID)
def _(g):
    g.cyl("metal", (0, 0, 0), 0.18, 0, 0.6, 8, GREY, r1=0.2)


@prop("lamp_floor", (0.4, 1.7, 0.4), SOLID)
def _(g):
    g.cyl("metal", (0, 0, 0), 0.15, 0, 0.03, 8, BLACK)
    g.cyl("metal", (0, 0, 0), 0.02, 0.03, 1.4, 6, BLACK)
    g.cyl("cloth", (0, 0, 0), 0.2, 1.4, 1.7, 10, col(240, 220, 180), r1=0.13)


@prop("ceiling_lamp", (0.6, 0.1, 0.6), 0)
def _(g):
    g.box("metal", (-0.3, -0.06, -0.3), (0.3, 0.0, 0.3), col(220, 220, 220))
    g.quad("glow_panel", (-0.27, -0.061, 0.27), (0.27, -0.061, 0.27), (0.27, -0.061, -0.27), (-0.27, -0.061, -0.27), facing=(0, -1, 0))


@prop("car", (1.9, 1.4, 4.4), SOLID | OCCL)
def _(g):
    c = col(*(int(v) for v in RNG.integers(40, 200, 3)))
    g.box("metal", (-0.95, 0.3, -2.2), (0.95, 0.85, 2.2), c)
    g.box("metal", (-0.85, 0.85, -1.0), (0.85, 1.4, 1.1), c)
    g.box("glass", (-0.86, 0.9, -0.95), (0.86, 1.35, 1.05))
    for x in (-0.95, 0.75):
        for z in (-1.5, 1.2):
            g.cyl("plastic", (x, 0.32, z + 0.15), 0.32, x, x + 0.2, 10, BLACK, axis="x")


@prop("dumpster", (1.8, 1.3, 1.0), SOLID | OCCL | HIDE | INTER)
def _(g):
    g.box("metal", (-0.9, 0, -0.5), (0.9, 1.2, 0.5), col(40, 90, 50))
    g.box("metal", (-0.92, 1.2, -0.52), (0.92, 1.3, 0.52), col(30, 70, 40))


@prop("piano", (1.5, 1.0, 1.6), SOLID)
def _(g):
    g.box("plastic", (-0.75, 0.6, -0.8), (0.75, 1.0, 0.8), BLACK)
    g.box("plastic", (-0.75, 0.55, -0.85), (0.75, 0.65, -0.6), col(240, 240, 240))
    for x in (-0.7, 0.62):
        g.box("plastic", (x, 0, -0.6), (x + 0.08, 0.6, -0.52), BLACK)
    g.box("plastic", (-0.05, 0, 0.6), (0.05, 0.6, 0.7), BLACK)


@prop("tv", (1.2, 1.1, 0.4), SOLID)
def _(g):
    g.box("veneer", (-0.6, 0, -0.2), (0.6, 0.45, 0.2))
    g.box("plastic", (-0.55, 0.5, -0.03), (0.55, 1.1, 0.03), BLACK)
    g.quad("screen", (-0.52, 0.53, -0.031), (0.52, 0.53, -0.031), (0.52, 1.07, -0.031), (-0.52, 1.07, -0.031), facing=(0, 0, -1))


@prop("bench", (1.6, 0.45, 0.45), SOLID)
def _(g):
    g.box("wood", (-0.8, 0.4, -0.22), (0.8, 0.45, 0.22), DARKWOOD)
    for x in (-0.7, 0.62):
        g.box("metal", (x, 0, -0.2), (x + 0.08, 0.4, 0.2), BLACK)


@prop("streetlight", (0.3, 5.0, 0.3), SOLID)
def _(g):
    g.cyl("metal", (0, 0, 0), 0.08, 0, 5.0, 8, col(60, 60, 60), r1=0.06)
    g.box("metal", (-0.1, 4.9, -0.8), (0.1, 5.0, 0.0), col(60, 60, 60))
    g.box("glow_panel", (-0.12, 4.86, -0.8), (0.12, 4.9, -0.5))


@prop("vent_grate", (1.0, 0.04, 1.0), INTER)
def _(g):
    g.box("metal", (-0.5, 0, -0.5), (0.5, 0.04, 0.5), GREY, faces="yY")
    for k in range(8):
        z = -0.42 + k * 0.11
        g.box("metal", (-0.45, -0.03, z), (0.45, 0.0, z + 0.05), BLACK)


@prop("fuse_box", (0.5, 0.7, 0.15), INTER)
def _(g):
    g.box("metal", (-0.25, 0, -0.07), (0.25, 0.7, 0.08), col(150, 150, 140))
    g.box("plastic", (-0.2, 0.5, -0.08), (0.2, 0.6, -0.07), col(220, 180, 30))


@prop("keypad", (0.15, 0.22, 0.05), INTER)
def _(g):
    g.box("metal", (-0.075, 0, -0.025), (0.075, 0.22, 0.025), col(50, 50, 55))
    g.quad("screen", (-0.06, 0.15, -0.026), (0.06, 0.15, -0.026), (0.06, 0.2, -0.026), (-0.06, 0.2, -0.026), facing=(0, 0, -1))


@prop("security_camera", (0.2, 0.2, 0.4), 0)
def _(g):
    g.box("plastic", (-0.08, -0.08, -0.3), (0.08, 0.06, 0.05), col(225, 225, 220))
    g.box("glass", (-0.05, -0.05, -0.31), (0.05, 0.03, -0.3), col(20, 20, 30))
    g.box("metal", (-0.02, 0.06, -0.02), (0.02, 0.2, 0.02), GREY)


@prop("laser_emitter", (0.12, 0.12, 0.12), 0)
def _(g):
    g.box("metal", (-0.06, -0.06, -0.06), (0.06, 0.06, 0.06), BLACK)


@prop("alarm_panel", (0.3, 0.4, 0.08), INTER)
def _(g):
    g.box("plastic", (-0.15, 0, -0.04), (0.15, 0.4, 0.04), col(230, 230, 225))
    g.quad("screen", (-0.11, 0.22, -0.041), (0.11, 0.22, -0.041), (0.11, 0.34, -0.041), (-0.11, 0.34, -0.041), facing=(0, 0, -1))


@prop("terminal", (0.5, 1.2, 0.4), SOLID | INTER)
def _(g):
    g.box("metal", (-0.25, 0, -0.2), (0.25, 1.0, 0.2), col(70, 75, 80))
    g.box("plastic", (-0.24, 1.0, -0.05), (0.24, 1.35, 0.0), BLACK)
    g.quad("screen", (-0.22, 1.02, -0.051), (0.22, 1.02, -0.051), (0.22, 1.33, -0.051), (-0.22, 1.33, -0.051), facing=(0, 0, -1))


@prop("statue", (0.4, 0.6, 0.4), INTER)
def _(g):
    g.cyl("gold", (0, 0, 0), 0.12, 0, 0.1, 8)
    g.cyl("gold", (0, 0, 0), 0.06, 0.1, 0.45, 8)
    g.box("gold", (-0.1, 0.45, -0.1), (0.1, 0.6, 0.1))


# Loot (small, interactive, drawn individually so they can disappear).
@prop("cash_stack", (0.3, 0.15, 0.2), INTER)
def _(g):
    for k in range(3):
        g.box("cash", (-0.15 + k * 0.01, k * 0.05, -0.1), (0.15, k * 0.05 + 0.05, 0.1))


@prop("gold_bars", (0.35, 0.12, 0.2), INTER)
def _(g):
    for k in range(3):
        g.box("gold", (-0.17 + k * 0.12, 0, -0.1), (-0.07 + k * 0.12, 0.06, 0.1))
    g.box("gold", (-0.1, 0.06, -0.1), (0.0, 0.12, 0.1))


@prop("jewelry_box", (0.3, 0.15, 0.2), INTER)
def _(g):
    g.box("leather", (-0.15, 0, -0.1), (0.15, 0.12, 0.1), col(130, 20, 40))
    g.box("gold", (-0.05, 0.12, -0.02), (0.05, 0.15, 0.02))


@prop("documents", (0.32, 0.06, 0.24), INTER)
def _(g):
    g.box("paper", (-0.16, 0, -0.12), (0.16, 0.05, 0.12))
    g.box("leather", (-0.16, 0.05, -0.12), (0.16, 0.06, 0.12), col(90, 60, 30))


@prop("hard_drive", (0.15, 0.04, 0.1), INTER)
def _(g):
    g.box("metal", (-0.075, 0, -0.05), (0.075, 0.04, 0.05), BLACK)


@prop("laptop", (0.36, 0.25, 0.25), INTER)
def _(g):
    g.box("metal", (-0.18, 0, -0.12), (0.18, 0.02, 0.12), GREY)
    g.box("metal", (-0.18, 0.02, 0.1), (0.18, 0.25, 0.12), GREY)
    g.quad("screen", (-0.16, 0.04, 0.099), (0.16, 0.04, 0.099), (0.16, 0.23, 0.099), (-0.16, 0.23, 0.099), facing=(0, 0, -1))


@prop("watch_case", (0.25, 0.1, 0.18), INTER)
def _(g):
    g.box("veneer", (-0.12, 0, -0.09), (0.12, 0.07, 0.09))
    g.box("glass", (-0.11, 0.07, -0.08), (0.11, 0.1, 0.08))
    g.cyl("gold", (0, 0, 0), 0.03, 0.06, 0.08, 8)


# Doors and dynamic fixtures (pivot at the hinge, leaf extends along +X).
@prop("door_wood", (1.0, 2.2, 0.05), 0)
def _(g):
    g.box("veneer", (0.0, 0, -0.025), (0.98, 2.18, 0.025))
    g.box("metal", (0.85, 1.0, -0.06), (0.9, 1.04, 0.06), GREY)


@prop("door_metal", (1.0, 2.2, 0.06), 0)
def _(g):
    g.box("metal", (0.0, 0, -0.03), (0.98, 2.18, 0.03), col(120, 125, 130))
    g.box("metal", (0.82, 1.0, -0.07), (0.92, 1.05, 0.07), BLACK)


@prop("door_glass", (1.0, 2.2, 0.04), 0)
def _(g):
    g.box("metal", (0.0, 0, -0.02), (0.06, 2.18, 0.02), BLACK)
    g.box("metal", (0.92, 0, -0.02), (0.98, 2.18, 0.02), BLACK)
    g.box("metal", (0.06, 0, -0.02), (0.92, 0.1, 0.02), BLACK)
    g.box("metal", (0.06, 2.1, -0.02), (0.92, 2.18, 0.02), BLACK)
    g.box("glass", (0.06, 0.1, -0.01), (0.92, 2.1, 0.01))


@prop("door_vault", (1.0, 2.2, 0.3), 0)
def _(g):
    g.box("metal", (0.0, 0, -0.15), (0.98, 2.18, 0.15), col(150, 150, 155))
    g.cyl("metal", (0.5, 1.1, 0), 0.28, -0.25, -0.15, 12, col(190, 190, 195), axis="z")
    for a in range(3):
        ang = a * math.pi / 3
        dx, dy = math.cos(ang) * 0.3, math.sin(ang) * 0.3
        g.box("metal", (0.5 - abs(dx) - 0.02, 1.1 - abs(dy) - 0.02, -0.3), (0.5 + abs(dx) + 0.02, 1.1 + abs(dy) + 0.02, -0.25), GREY)


@prop("door_shutter", (1.0, 2.2, 0.08), 0)
def _(g):
    g.box("corrugated", (0.0, 0, -0.04), (0.98, 2.18, 0.04), col(140, 140, 140))


@prop("ladder", (0.5, 4.0, 0.15), INTER)
def _(g):
    for x in (-0.25, 0.21):
        g.box("metal", (x, 0, -0.03), (x + 0.04, 4.0, 0.03), GREY)
    for k in range(13):
        y = 0.25 + k * 0.3
        g.box("metal", (-0.22, y, -0.02), (0.22, y + 0.03, 0.02), GREY)


@prop("phone", (0.12, 0.02, 0.06), INTER)
def _(g):
    g.box("plastic", (-0.06, 0, -0.03), (0.06, 0.02, 0.03), BLACK)


@prop("keycard", (0.09, 0.01, 0.06), INTER)
def _(g):
    g.box("plastic", (-0.045, 0, -0.03), (0.045, 0.004, 0.03), col(235, 235, 240))
    g.box("plastic", (-0.045, 0.004, -0.012), (0.045, 0.006, 0.004), col(40, 90, 200))
    g.box("metal", (-0.035, 0.004, 0.01), (-0.02, 0.006, 0.022), col(210, 180, 80))


@prop("code_note", (0.08, 0.01, 0.08), INTER)
def _(g):
    g.box("paper", (-0.04, 0, -0.04), (0.04, 0.003, 0.04), col(250, 225, 90))
    for k in range(3):
        z = -0.022 + k * 0.016
        g.box("paper", (-0.028, 0.003, z), (0.02 - k * 0.008, 0.004, z + 0.005), col(40, 40, 60))


def build_props():
    os.makedirs(os.path.join(SRC, "props"), exist_ok=True)
    for name, size, flags, fn in PROPS:
        g = Geo()
        fn(g)
        gl = Gltf()
        m = gl.mesh(name, g)
        gl.node(name, mesh=m, root=True)
        gl.save(os.path.join(SRC, "props", name + ".gltf"))


# --------------------------------------------------------------------------- characters

NAVY, TROUSER, BOOT, CAP = col(45, 55, 80), col(35, 35, 40), col(25, 22, 20), col(30, 35, 50)


def guard_geo():
    """Returns {bone: Geo}; geometry in model space (feet at y=0, facing -Z)."""
    parts = {b: Geo() for b in ("hips", "chest", "head", "uarm_l", "farm_l", "uarm_r", "farm_r",
                                "thigh_l", "shin_l", "thigh_r", "shin_r")}
    p = parts
    p["hips"].box("cloth", (-0.17, 0.88, -0.11), (0.17, 1.05, 0.11), TROUSER)
    p["hips"].box("leather", (-0.175, 1.0, -0.115), (0.175, 1.05, 0.115), BOOT)
    p["chest"].box("cloth", (-0.2, 1.05, -0.12), (0.2, 1.45, 0.12), NAVY)
    p["chest"].box("cloth", (-0.21, 1.3, -0.13), (0.21, 1.48, 0.13), NAVY)
    p["chest"].box("metal", (0.06, 1.3, -0.15), (0.12, 1.42, -0.12), BLACK)  # radio
    p["chest"].box("gold", (-0.13, 1.32, -0.135), (-0.08, 1.37, -0.13))      # badge
    p["chest"].cyl("skin", (0, 0, 0), 0.05, 1.48, 1.55, 6)
    p["head"].box("skin", (-0.09, 1.55, -0.11), (0.09, 1.79, 0.1))
    p["head"].box("cloth", (-0.1, 1.74, -0.11), (0.1, 1.82, 0.11), CAP)
    p["head"].box("cloth", (-0.1, 1.74, -0.19), (0.1, 1.76, -0.1), CAP)
    p["head"].box("plastic", (-0.07, 1.65, -0.112), (-0.02, 1.67, -0.11), BLACK)
    p["head"].box("plastic", (0.02, 1.65, -0.112), (0.07, 1.67, -0.11), BLACK)
    for s, x in (("l", -1), ("r", 1)):
        p["uarm_" + s].box("cloth", (x * 0.21 - 0.05, 1.17, -0.05), (x * 0.21 + 0.05, 1.46, 0.05), NAVY)
        p["farm_" + s].box("cloth", (x * 0.21 - 0.045, 0.93, -0.045), (x * 0.21 + 0.045, 1.17, 0.045), NAVY)
        p["farm_" + s].box("skin", (x * 0.21 - 0.04, 0.83, -0.04), (x * 0.21 + 0.04, 0.93, 0.04))
        p["thigh_" + s].box("cloth", (x * 0.1 - 0.075, 0.5, -0.08), (x * 0.1 + 0.075, 0.9, 0.08), TROUSER)
        p["shin_" + s].box("cloth", (x * 0.1 - 0.065, 0.1, -0.07), (x * 0.1 + 0.065, 0.5, 0.07), TROUSER)
        p["shin_" + s].box("leather", (x * 0.1 - 0.07, 0.0, -0.16), (x * 0.1 + 0.07, 0.1, 0.08), BOOT)
    # Torch in the right hand, pointing forward.
    p["farm_r"].cyl("metal", (0.21, 0.86, 0), 0.025, -0.25, 0.0, 8, BLACK, axis="z")
    p["farm_r"].cyl("glass", (0.21, 0.86, 0), 0.03, -0.26, -0.25, 8, col(255, 250, 220), axis="z")
    return parts


# joint positions (model space) and parents
GUARD_JOINTS = {
    "hips": ((0, 0.95, 0), None), "chest": ((0, 1.05, 0), "hips"), "head": ((0, 1.53, 0), "chest"),
    "uarm_l": ((-0.21, 1.44, 0), "chest"), "farm_l": ((-0.21, 1.17, 0), "uarm_l"),
    "uarm_r": ((0.21, 1.44, 0), "chest"), "farm_r": ((0.21, 1.17, 0), "uarm_r"),
    "thigh_l": ((-0.1, 0.9, 0), "hips"), "shin_l": ((-0.1, 0.5, 0), "thigh_l"),
    "thigh_r": ((0.1, 0.9, 0), "hips"), "shin_r": ((0.1, 0.5, 0), "thigh_r"),
}


def build_skeleton(gl, joints, geo_by_bone, name):
    """Create bone nodes with meshes expressed in each bone's local space."""
    idx = {}
    children = {}
    for b, (pos, par) in joints.items():
        children.setdefault(par, []).append(b)

    def make(b):
        pos, par = joints[b]
        ppos = joints[par][0] if par else (0, 0, 0)
        local = tuple(pos[k] - ppos[k] for k in range(3))
        geo = geo_by_bone.get(b)
        mesh = None
        if geo and geo.prims:
            # Shift geometry into bone-local space.
            for prim in geo.prims.values():
                prim["pos"] = [(x - pos[0], y - pos[1], z - pos[2]) for x, y, z in prim["pos"]]
            mesh = gl.mesh(name + "_" + b, geo)
        kids = [make(c) for c in children.get(b, [])]
        n = gl.node(b, mesh=mesh, t=local, children=kids, extras={"bone": True})
        idx[b] = n
        return n

    root = [b for b, (_, par) in joints.items() if par is None][0]
    gl.roots.append(make(root))
    return idx


def keyed(gl, bones, rest_t, name, dur, poses, loop=True, root_bob=None, root="hips"):
    """poses: list of (time, {bone: (x, y, z) euler radians}). Bones missing in a
    pose use identity. root_bob: list of (time, dy) added to hips translation."""
    tracks = []
    times = [t for t, _ in poses]
    if loop and times[-1] < dur:
        times = times + [dur]
        poses = poses + [(dur, poses[0][1])]
    for b, node in bones.items():
        vals = [euler(*pose.get(b, (0, 0, 0))) for _, pose in poses]
        tracks.append((node, "rotation", times, vals))
    if root_bob:
        t = [x for x, _ in root_bob]
        base = rest_t[root]
        tracks.append((bones[root], "translation", t, [(base[0], base[1] + dy, base[2]) for _, dy in root_bob]))
    gl.animation(name, tracks, loop)


def build_guard():
    gl = Gltf()
    bones = build_skeleton(gl, GUARD_JOINTS, guard_geo(), "guard")
    rest = {b: p for b, (p, _) in GUARD_JOINTS.items()}
    torch_fwd = {"uarm_r": (1.35, 0, 0.05), "farm_r": (0.15, 0, 0)}
    d = math.radians

    def walk(name, dur, swing, knee, arm, lean, torch, bob):
        ps = []
        for k in range(4):
            ph = k / 4
            s = math.sin(ph * 2 * math.pi)
            c = math.cos(ph * 2 * math.pi)
            pose = {
                "thigh_l": (s * swing, 0, 0), "thigh_r": (-s * swing, 0, 0),
                "shin_l": (-max(0, -c) * knee, 0, 0), "shin_r": (-max(0, c) * knee, 0, 0),
                "uarm_l": (-s * arm, 0, 0), "farm_l": (0.2, 0, 0),
                "chest": (-lean, 0, 0),
            }
            if torch:
                pose.update(torch_fwd)
            else:
                pose["uarm_r"] = (s * arm, 0, 0)
                pose["farm_r"] = (0.2, 0, 0)
            ps.append((ph * dur, pose))
        keyed(gl, bones, rest, name, dur, ps,
              root_bob=[(t * dur / 4, bob * abs(math.cos(t / 4 * 2 * math.pi))) for t in range(5)])

    walk("walk", 1.1, d(24), d(40), d(18), d(4), False, -0.03)
    walk("walk_torch", 1.1, d(24), d(40), d(14), d(4), True, -0.03)
    walk("run", 0.66, d(42), d(80), d(40), d(14), False, -0.06)
    walk("search", 1.6, d(16), d(25), d(10), d(10), True, -0.02)

    keyed(gl, bones, rest, "idle", 3.0, [
        (0.0, {"chest": (0, 0, 0), "farm_l": (0.15, 0, 0), "farm_r": (0.15, 0, 0)}),
        (1.5, {"chest": (d(2), 0, 0), "head": (d(-2), 0, 0), "farm_l": (0.18, 0, 0), "farm_r": (0.18, 0, 0)}),
    ], root_bob=[(0, 0), (1.5, 0.008), (3.0, 0)])
    keyed(gl, bones, rest, "look_around", 4.0, [
        (0.0, {"head": (0, 0, 0), "chest": (0, 0, 0)}),
        (1.0, {"head": (0, d(55), 0), "chest": (0, d(15), 0)}),
        (2.0, {"head": (0, 0, 0)}),
        (3.0, {"head": (0, d(-55), 0), "chest": (0, d(-15), 0)}),
    ])
    keyed(gl, bones, rest, "suspicious", 2.4, [
        (0.0, dict(torch_fwd, head=(d(-5), d(-20), 0), chest=(d(-8), 0, 0), thigh_l=(d(8), 0, 0), shin_l=(d(-14), 0, 0))),
        (1.2, dict(torch_fwd, head=(d(-5), d(20), 0), chest=(d(-8), d(8), 0), thigh_l=(d(8), 0, 0), shin_l=(d(-14), 0, 0))),
    ])
    keyed(gl, bones, rest, "alert", 1.0, [
        (0.0, dict(torch_fwd, chest=(d(-10), 0, 0), uarm_l=(d(70), 0, d(-10)), farm_l=(d(40), 0, 0),
                   thigh_l=(d(15), 0, 0), shin_l=(d(-25), 0, 0), thigh_r=(d(-10), 0, 0))),
        (0.5, dict(torch_fwd, chest=(d(-12), d(4), 0), uarm_l=(d(72), 0, d(-10)), farm_l=(d(40), 0, 0),
                   thigh_l=(d(15), 0, 0), shin_l=(d(-25), 0, 0), thigh_r=(d(-10), 0, 0))),
    ])
    keyed(gl, bones, rest, "radio", 2.0, [
        (0.0, {"uarm_l": (d(30), 0, d(-60)), "farm_l": (d(120), 0, 0), "head": (d(-10), d(-15), 0)}),
        (1.0, {"uarm_l": (d(32), 0, d(-62)), "farm_l": (d(125), 0, 0), "head": (d(-12), d(-10), 0)}),
    ])
    keyed(gl, bones, rest, "smoke", 4.0, [
        (0.0, {"uarm_r": (0, 0, 0), "farm_r": (d(20), 0, 0)}),
        (1.0, {"uarm_r": (d(25), 0, d(10)), "farm_r": (d(130), 0, 0), "head": (d(8), 0, 0)}),
        (2.0, {"uarm_r": (d(25), 0, d(10)), "farm_r": (d(130), 0, 0), "head": (d(15), 0, 0)}),
        (3.0, {"uarm_r": (0, 0, 0), "farm_r": (d(20), 0, 0)}),
    ])
    keyed(gl, bones, rest, "chat", 3.0, [
        (0.0, {"uarm_l": (d(20), 0, 0), "farm_l": (d(60), 0, 0), "head": (0, d(5), 0)}),
        (0.8, {"uarm_l": (d(35), 0, d(-10)), "farm_l": (d(80), 0, 0), "head": (d(-5), d(-5), 0), "chest": (0, d(5), 0)}),
        (1.6, {"uarm_r": (d(30), 0, d(10)), "farm_r": (d(70), 0, 0), "head": (d(3), d(8), 0)}),
        (2.3, {"uarm_l": (d(10), 0, 0), "farm_l": (d(40), 0, 0)}),
    ])
    keyed(gl, bones, rest, "grab", 0.8, [
        (0.0, dict(torch_fwd)),
        (0.35, {"uarm_l": (d(85), 0, 0), "uarm_r": (d(85), 0, 0), "chest": (d(-20), 0, 0),
                "thigh_l": (d(25), 0, 0), "shin_l": (d(-20), 0, 0)}),
        (0.8, {"uarm_l": (d(80), 0, 0), "uarm_r": (d(80), 0, 0), "chest": (d(-15), 0, 0)}),
    ], loop=False)
    # Knocked out: pitch the hips backwards onto the floor.
    keyed(gl, bones, rest, "knockout", 1.2, [
        (0.0, {}),
        (0.4, {"hips": (d(-20), 0, 0), "shin_l": (d(-50), 0, 0), "shin_r": (d(-50), 0, 0), "head": (d(-20), 0, 0)}),
        (1.2, {"hips": (d(-88), 0, 0), "uarm_l": (d(10), 0, d(-60)), "uarm_r": (d(10), 0, d(60)),
               "head": (d(-10), d(30), 0), "thigh_l": (d(5), 0, 0), "thigh_r": (d(-5), 0, 0)}),
    ], loop=False, root_bob=[(0, 0), (0.4, -0.3), (1.2, -0.82)])
    keyed(gl, bones, rest, "dragged", 1.0, [
        (0.0, {"hips": (d(-80), 0, 0), "uarm_l": (d(150), 0, 0), "uarm_r": (d(150), 0, 0),
               "head": (d(-30), d(20), 0), "thigh_l": (d(5), 0, 0)}),
        (0.5, {"hips": (d(-80), 0, 0), "uarm_l": (d(150), 0, 0), "uarm_r": (d(150), 0, 0),
               "head": (d(-30), d(-20), 0), "thigh_r": (d(5), 0, 0)}),
    ], root_bob=[(0, -0.78), (0.5, -0.76), (1.0, -0.78)])
    keyed(gl, bones, rest, "nap", 4.0, [
        (0.0, {"chest": (d(-25), 0, 0), "head": (d(-35), 0, d(10)), "uarm_l": (d(15), 0, 0), "uarm_r": (d(15), 0, 0),
               "thigh_l": (d(80), 0, 0), "thigh_r": (d(80), 0, 0), "shin_l": (d(-85), 0, 0), "shin_r": (d(-85), 0, 0)}),
        (2.0, {"chest": (d(-28), 0, 0), "head": (d(-38), 0, d(12)), "uarm_l": (d(15), 0, 0), "uarm_r": (d(15), 0, 0),
               "thigh_l": (d(80), 0, 0), "thigh_r": (d(80), 0, 0), "shin_l": (d(-85), 0, 0), "shin_r": (d(-85), 0, 0)}),
    ], root_bob=[(0, -0.45), (2.0, -0.44), (4.0, -0.45)])
    os.makedirs(os.path.join(SRC, "chars"), exist_ok=True)
    gl.save(os.path.join(SRC, "chars", "guard.gltf"))


DOG_JOINTS = {
    "body": ((0, 0.5, 0), None), "head": ((0, 0.6, -0.4), "body"), "tail": ((0, 0.6, 0.38), "body"),
    "leg_fl": ((-0.12, 0.42, -0.3), "body"), "leg_fr": ((0.12, 0.42, -0.3), "body"),
    "leg_bl": ((-0.12, 0.42, 0.3), "body"), "leg_br": ((0.12, 0.42, 0.3), "body"),
}


def build_dog():
    fur = col(70, 55, 40)
    g = {b: Geo() for b in DOG_JOINTS}
    g["body"].box("cloth", (-0.14, 0.38, -0.4), (0.14, 0.64, 0.4), fur)
    g["head"].box("cloth", (-0.1, 0.55, -0.62), (0.1, 0.78, -0.38), fur)
    g["head"].box("cloth", (-0.06, 0.55, -0.74), (0.06, 0.66, -0.6), fur)
    g["head"].box("cloth", (-0.1, 0.76, -0.5), (-0.05, 0.86, -0.44), fur)
    g["head"].box("cloth", (0.05, 0.76, -0.5), (0.1, 0.86, -0.44), fur)
    g["tail"].box("cloth", (-0.025, 0.58, 0.38), (0.025, 0.63, 0.65), fur)
    for b in ("leg_fl", "leg_fr", "leg_bl", "leg_br"):
        x, y, z = DOG_JOINTS[b][0]
        g[b].box("cloth", (x - 0.04, 0.0, z - 0.04), (x + 0.04, 0.42, z + 0.04), fur)
    gl = Gltf()
    bones = build_skeleton(gl, DOG_JOINTS, g, "dog")
    rest = {b: p for b, (p, _) in DOG_JOINTS.items()}
    d = math.radians

    def gait(name, dur, sw):
        ps = []
        for k in range(4):
            s = math.sin(k / 4 * 2 * math.pi)
            ps.append((k / 4 * dur, {"leg_fl": (s * sw, 0, 0), "leg_br": (s * sw, 0, 0),
                                     "leg_fr": (-s * sw, 0, 0), "leg_bl": (-s * sw, 0, 0),
                                     "tail": (d(20), s * 0.4, 0), "head": (s * 0.05, 0, 0)}))
        keyed(gl, bones, rest, name, dur, ps)

    gait("dog_walk", 0.7, d(25))
    gait("dog_run", 0.4, d(45))
    keyed(gl, bones, rest, "dog_idle", 2.0, [(0, {"tail": (d(30), d(20), 0), "head": (d(5), 0, 0)}),
                                             (1.0, {"tail": (d(30), d(-20), 0), "head": (d(-5), d(10), 0)})])
    keyed(gl, bones, rest, "dog_sniff", 1.6, [(0, {"head": (d(35), d(-10), 0), "body": (d(5), 0, 0)}),
                                              (0.8, {"head": (d(40), d(10), 0), "body": (d(6), 0, 0)})])
    keyed(gl, bones, rest, "dog_bark", 0.6, [(0, {"head": (d(-20), 0, 0)}), (0.3, {"head": (d(-5), 0, 0)})])
    keyed(gl, bones, rest, "dog_sleep", 3.0, [(0, {"body": (0, 0, d(80)), "head": (d(10), 0, 0)}),
                                              (1.5, {"body": (0, 0, d(80)), "head": (d(12), 0, 0)})],
          root_bob=[(0, -0.28), (3.0, -0.28)], root="body")
    gl.save(os.path.join(SRC, "chars", "dog.gltf"))


# --------------------------------------------------------------------------- outputs

TEX_FORMATS = {"cookie": ("I8", 128, False), "glow": ("IA8", 64, False), "blob": ("IA8", 64, False),
               "dust": ("IA8", 32, False), "beam": ("IA8", 64, False), "glass": ("RGB5A3", 64, True),
               "white": ("I8", 8, True), "glow_panel": ("RGB565", 16, True), "sign_exit": ("CMPR", (128, 64), True)}


def write_manifest():
    tex = {}
    for f in sorted(os.listdir(os.path.join(SRC, "tex"))):
        if not f.endswith(".png"):
            continue
        name = f[:-4]
        fmt, size, wrap = TEX_FORMATS.get(name, ("CMPR", 128, True))
        if name in POLYHAVEN:
            size = 256 if name in ("concrete", "plaster", "carpet", "parquet", "tiles", "marble", "brick",
                                   "facade", "asphalt") else 128
        tex[name] = {"src": "src/tex/" + f, "fmt": fmt, "size": size, "wrap": wrap,
                     "filter": True, "mips": name not in ("cookie", "white", "glow", "blob", "dust", "beam")}
    models = {name: {"src": "src/props/%s.gltf" % name} for name, *_ in PROPS}
    models["guard"] = {"src": "src/chars/guard.gltf", "anims": True}
    models["dog"] = {"src": "src/chars/dog.gltf", "anims": True}
    with open(os.path.join(ROOT, "assets", "manifest.json"), "w") as f:
        json.dump({"textures": tex, "models": models}, f, indent=1, sort_keys=True)


POLYHAVEN = set()


def write_prop_table():
    h = ["/* Generated by tools/make_art.py - do not edit by hand. */",
         "#ifndef NH_PROPTAB_H", "#define NH_PROPTAB_H", "", '#include "nh_math.h"', "",
         "typedef enum NhPropType {"]
    for i, (name, *_rest) in enumerate(PROPS):
        h.append("    PROP_%s = %d," % (name.upper(), i))
    h += ["    PROP_COUNT = %d" % len(PROPS), "} NhPropType;", "",
          "typedef struct NhPropInfo {", "    const char *name;", "    NhVec3 half;   /* collision half extents */",
          "    uint8_t flags; /* LV_P_* */", "} NhPropInfo;", "",
          "extern const NhPropInfo nh_prop_info[PROP_COUNT];", "int nh_prop_find(const char *name);", "",
          "#endif"]
    c = ["/* Generated by tools/make_art.py - do not edit by hand. */", '#include "nh_proptab.h"', "",
         "#include <string.h>", "", "const NhPropInfo nh_prop_info[PROP_COUNT] = {"]
    for name, size, flags, _ in PROPS:
        c.append('    { "%s", { %.3ff, %.3ff, %.3ff }, %d },' % (name, size[0] / 2, size[1] / 2, size[2] / 2, flags))
    c += ["};", "", "int nh_prop_find(const char *name)", "{",
          "    for (int i = 0; i < PROP_COUNT; i++)", "        if (strcmp(nh_prop_info[i].name, name) == 0)",
          "            return i;", "    return -1;", "}"]
    with open(os.path.join(ROOT, "src", "core", "nh_proptab.h"), "w") as f:
        f.write("\n".join(h) + "\n")
    with open(os.path.join(ROOT, "src", "core", "nh_proptab.c"), "w") as f:
        f.write("\n".join(c) + "\n")


def main():
    srcj = os.path.join(ROOT, "assets", "polyhaven_sources.json")
    if os.path.exists(srcj):
        POLYHAVEN.update(json.load(open(srcj)).keys())
    make_textures()
    build_props()
    build_guard()
    build_dog()
    write_manifest()
    write_prop_table()
    print("art: %d props, guard, dog, textures -> %s" % (len(PROPS), SRC))
    return 0


if __name__ == "__main__":
    sys.exit(main())
