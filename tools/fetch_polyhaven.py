#!/usr/bin/env python3
"""Fetch CC0 surface textures from Poly Haven and prepare them for the Wii.

Downloads the 1k diffuse (and AO where available) maps into a cache outside
the repository, multiplies in a little ambient occlusion for depth, downsizes
to 256x256 and writes assets/src/tex/<name>.png. Also writes
assets/polyhaven_sources.json, which ASSETS.md is generated from.

All Poly Haven assets are CC0 (https://polyhaven.com/license); the script
records the asset page for each texture anyway so every file is traceable.
"""
import io
import json
import os
import sys
import urllib.request

from PIL import Image, ImageChops, ImageStat

# our material name -> Poly Haven asset id
TEXTURES = {
    "concrete": "concrete_floor_02",
    "garage": "garage_floor",
    "plaster": "plastered_wall_02",
    "plaster_beige": "beige_wall_001",
    "stucco": "white_stucco",
    "ceiling": "ceiling_interior",
    "brick": "large_red_bricks",
    "brick_white": "patterned_plaster_wall",
    "parquet": "herringbone_parquet",
    "planks": "brown_planks_03",
    "tiles": "interior_tiles",
    "marble": "marble_01",
    "marble_tiles": "marble_tiles",
    "granite": "granite_tile",
    "linoleum": "linoleum_brown",
    "carpet": "dirty_carpet",
    "wallpaper": "floral_jacquard",
    "metal": "metal_plate",
    "corrugated": "corrugated_iron",
    "rust": "grey_cartago_01",
    "slats": "rusty_metal",
    "wood": "kitchen_wood",
    "veneer": "black_walnut_veneer_01",
    "fabric": "rough_linen",
    "leather": "brown_leather",
    "asphalt": "asphalt_02",
    "pavement": "concrete_pavement",
    "grass": "sparse_grass",
    "facade": "concrete_tile_facade",
    "roof": "bitumen",
    "panel": "dark_paneled_wood",
}

# Some scans are much darker than the surface they stand in for; scale them so
# their mean brightness is close to this value (0-255).
TARGET_MEAN = {"ceiling": 185, "stucco": 200, "plaster": 175, "tiles": 170}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(os.path.expanduser("~"), ".cache", "nightheist", "polyhaven")
OUT = os.path.join(ROOT, "assets", "src", "tex")


def fetch(url):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, url.rsplit("/", 1)[1])
    if not os.path.exists(path):
        req = urllib.request.Request(url, headers={"User-Agent": "NightHeist-asset-fetch/1.0"})
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
        with open(path, "wb") as f:
            f.write(data)
    return path


def main():
    os.makedirs(OUT, exist_ok=True)
    sources = {}
    for name, pid in sorted(TEXTURES.items()):
        info = json.load(urllib.request.urlopen(
            urllib.request.Request("https://api.polyhaven.com/files/" + pid,
                                   headers={"User-Agent": "NightHeist-asset-fetch/1.0"}), timeout=60))
        diff = fetch(info["Diffuse"]["1k"]["jpg"]["url"])
        img = Image.open(diff).convert("RGB")
        if "AO" in info:
            ao = Image.open(fetch(info["AO"]["1k"]["jpg"]["url"])).convert("L").resize(img.size)
            # Soften: blend AO halfway toward white before multiplying.
            ao = Image.eval(ao, lambda v: 128 + v // 2)
            img = ImageChops.multiply(img, Image.merge("RGB", (ao, ao, ao)))
        img = img.resize((256, 256), Image.LANCZOS)
        if name in TARGET_MEAN:
            mean = sum(ImageStat.Stat(img).mean) / 3.0
            gain = TARGET_MEAN[name] / max(mean, 1.0)
            img = img.point(lambda v: min(255, int(v * gain)))
        img.save(os.path.join(OUT, name + ".png"))
        sources[name] = {"polyhaven_id": pid, "page": "https://polyhaven.com/a/" + pid, "license": "CC0 1.0"}
        print("ok", name, pid)
    with open(os.path.join(ROOT, "assets", "polyhaven_sources.json"), "w") as f:
        json.dump(sources, f, indent=2, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
