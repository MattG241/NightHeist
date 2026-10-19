# Assets and Third-Party Code

Every asset and third-party component, with its source and licence. Add a row
for anything new **before** it lands in the repo.

## Game assets

| Asset | Path | Source | Licence |
|---|---|---|---|
| 5x7 bitmap font | `tools/gen_font.py` → `src/core/nh_font5x7.c` | Original work for Night Heist | CC0 1.0 |
| Homebrew Channel icon | `tools/gen_icon.py` → `apps/NightHeist/icon.png` | Original, generated procedurally | CC0 1.0 |
| Brick and concrete test textures | `src/core/nh_texgen.c` (generated at runtime) | Original, procedural | CC0 1.0 |
| Click sound | `src/game/demo_scene.c` (synthesised at runtime) | Original, procedural | CC0 1.0 |
| Torus, floor and wall test meshes | `src/core/nh_meshgen.c` (generated at runtime) | Original, procedural | CC0 1.0 |

Planned sources, each to be verified per file before use: Poly Haven and
ambientCG textures (CC0, downscaled for the Wii), Mixamo animations (Adobe
Mixamo terms allow use in games, to be checked before adding them), and
CC0 modular kit pieces.

## Code and libraries

| Component | Use | Licence |
|---|---|---|
| devkitPPC (GCC, binutils, newlib) | Wii toolchain | GPL (toolchain), newlib licences (runtime) |
| libogc 3.2.1 (incl. wiiuse, ASND, bte) | Wii system library | zlib-style libogc licence; RTEMS-derived threading code under a BSD-style notice (see libogc `LICENSE`). Credit libogc in the release README. |
| libfat-ogc 2.1.0 | SD/USB FAT access | ZPL (zlib-style) |
| SDL2 | Desktop backend | zlib |
| PCG32 algorithm | `src/core/nh_rng.c` | Algorithm by Melissa O'Neill; implementation follows the minimal C reference (Apache-2.0) |
