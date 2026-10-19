#!/usr/bin/env python3
"""Generate apps/NightHeist/icon.png, the 128x48 Homebrew Channel icon.

Original work for Night Heist (CC0). Drawn procedurally: a dark wall, a
torch beam cone and the title in the game's own 5x7 font.
"""
import math
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("gen_icon.py needs Pillow: pip install pillow")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_font import GLYPHS  # noqa: E402

W, H = 128, 48


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    img = Image.new("RGBA", (W, H))
    px = img.load()

    # Background: near-black blue with a torch cone from the left edge.
    origin = (-6.0, 30.0)
    axis = (math.cos(math.radians(-12)), math.sin(math.radians(-12)))
    for y in range(H):
        for x in range(W):
            base = (10, 11, 18)
            dx, dy = x - origin[0], y - origin[1]
            dist = math.hypot(dx, dy)
            c = (dx * axis[0] + dy * axis[1]) / dist if dist else 1.0
            cutoff = math.cos(math.radians(22))
            beam = 0.0
            if c > cutoff:
                beam = ((c - cutoff) / (1 - cutoff)) ** 1.5 * max(0.0, 1.0 - dist / 150.0)
            r = base[0] + 235 * beam
            g = base[1] + 215 * beam
            b = base[2] + 170 * beam
            px[x, y] = (int(min(r, 255)), int(min(g, 255)), int(min(b, 255)), 255)

    def draw_text(text, ox, oy, scale, colour):
        for i, ch in enumerate(text):
            rows = GLYPHS[ch]
            for gy, row in enumerate(rows):
                for gx, p in enumerate(row):
                    if p != "#":
                        continue
                    for sy in range(scale):
                        for sx in range(scale):
                            x = ox + (i * 6 + gx) * scale + sx
                            y = oy + gy * scale + sy
                            if 0 <= x < W and 0 <= y < H:
                                px[x, y] = colour

    scale = 2
    for line, oy in (("NIGHT", 8), ("HEIST", 26)):
        tw = (len(line) * 6 - 1) * scale
        ox = W - tw - 10
        draw_text(line, ox + 1, oy + 1, scale, (0, 0, 0, 255))  # drop shadow
        draw_text(line, ox, oy, scale, (255, 214, 120, 255))

    out = os.path.join(root, "apps", "NightHeist", "icon.png")
    img.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
