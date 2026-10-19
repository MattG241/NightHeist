# Assets and Third-Party Code

Every asset and third-party component, with its source and licence. Add a row
for anything new **before** it lands in the repo.

## Game assets

| Asset | Path | Source | Licence |
|---|---|---|---|
| 5x7 bitmap font | `tools/gen_font.py` → `src/core/nh_font5x7.c` | Original work for Night Heist | CC0 1.0 |
| Homebrew Channel icon | `tools/gen_icon.py` → `apps/NightHeist/icon.png` | Original, generated procedurally | CC0 1.0 |
| Brick and concrete test textures | `src/core/nh_texgen.c` (generated at runtime) | Original, procedural | CC0 1.0 |
| All sound effects (70) and music (heist theme in 4 stems, safehouse theme in 2) | `src/core/nh_synth.c` (synthesised at boot; `make audiodump` writes them as WAV) | Original, procedural | CC0 1.0 |
| Torus, floor and wall test meshes | `src/core/nh_meshgen.c` (generated at runtime) | Original, procedural | CC0 1.0 |
| Props (62 models), guard and dog characters with animations | `tools/make_art.py` -> `assets/src/props`, `assets/src/chars` -> `data/mdl`, `data/anim` | Original, built procedurally from primitives | CC0 1.0 |
| Procedural textures: glass, screen, plastic, paper, skin, gold, cash, cloth, paintings 0-5, exit sign, torch cookie, glow, blob shadow, dust, beam, white, glow panel | `tools/make_art.py` -> `assets/src/tex` | Original, procedural | CC0 1.0 |

### Surface textures from Poly Haven

Downloaded by `tools/fetch_polyhaven.py` (1k diffuse, multiplied by AO where provided, resized to 256x256, some brightness-normalised), converted to CMPR by `tools/nhconv.py`. Poly Haven's licence is CC0 for all assets: <https://polyhaven.com/license>.

| Material | Files | Source | Licence |
|---|---|---|---|
| asphalt | `assets/src/tex/asphalt.png` -> `data/tex/asphalt.ntx` | [Poly Haven: asphalt_02](https://polyhaven.com/a/asphalt_02) | CC0 1.0 |
| brick | `assets/src/tex/brick.png` -> `data/tex/brick.ntx` | [Poly Haven: large_red_bricks](https://polyhaven.com/a/large_red_bricks) | CC0 1.0 |
| brick_white | `assets/src/tex/brick_white.png` -> `data/tex/brick_white.ntx` | [Poly Haven: patterned_plaster_wall](https://polyhaven.com/a/patterned_plaster_wall) | CC0 1.0 |
| carpet | `assets/src/tex/carpet.png` -> `data/tex/carpet.ntx` | [Poly Haven: dirty_carpet](https://polyhaven.com/a/dirty_carpet) | CC0 1.0 |
| ceiling | `assets/src/tex/ceiling.png` -> `data/tex/ceiling.ntx` | [Poly Haven: ceiling_interior](https://polyhaven.com/a/ceiling_interior) | CC0 1.0 |
| concrete | `assets/src/tex/concrete.png` -> `data/tex/concrete.ntx` | [Poly Haven: concrete_floor_02](https://polyhaven.com/a/concrete_floor_02) | CC0 1.0 |
| corrugated | `assets/src/tex/corrugated.png` -> `data/tex/corrugated.ntx` | [Poly Haven: corrugated_iron](https://polyhaven.com/a/corrugated_iron) | CC0 1.0 |
| fabric | `assets/src/tex/fabric.png` -> `data/tex/fabric.ntx` | [Poly Haven: rough_linen](https://polyhaven.com/a/rough_linen) | CC0 1.0 |
| facade | `assets/src/tex/facade.png` -> `data/tex/facade.ntx` | [Poly Haven: concrete_tile_facade](https://polyhaven.com/a/concrete_tile_facade) | CC0 1.0 |
| garage | `assets/src/tex/garage.png` -> `data/tex/garage.ntx` | [Poly Haven: garage_floor](https://polyhaven.com/a/garage_floor) | CC0 1.0 |
| granite | `assets/src/tex/granite.png` -> `data/tex/granite.ntx` | [Poly Haven: granite_tile](https://polyhaven.com/a/granite_tile) | CC0 1.0 |
| grass | `assets/src/tex/grass.png` -> `data/tex/grass.ntx` | [Poly Haven: sparse_grass](https://polyhaven.com/a/sparse_grass) | CC0 1.0 |
| leather | `assets/src/tex/leather.png` -> `data/tex/leather.ntx` | [Poly Haven: brown_leather](https://polyhaven.com/a/brown_leather) | CC0 1.0 |
| linoleum | `assets/src/tex/linoleum.png` -> `data/tex/linoleum.ntx` | [Poly Haven: linoleum_brown](https://polyhaven.com/a/linoleum_brown) | CC0 1.0 |
| marble | `assets/src/tex/marble.png` -> `data/tex/marble.ntx` | [Poly Haven: marble_01](https://polyhaven.com/a/marble_01) | CC0 1.0 |
| marble_tiles | `assets/src/tex/marble_tiles.png` -> `data/tex/marble_tiles.ntx` | [Poly Haven: marble_tiles](https://polyhaven.com/a/marble_tiles) | CC0 1.0 |
| metal | `assets/src/tex/metal.png` -> `data/tex/metal.ntx` | [Poly Haven: metal_plate](https://polyhaven.com/a/metal_plate) | CC0 1.0 |
| panel | `assets/src/tex/panel.png` -> `data/tex/panel.ntx` | [Poly Haven: dark_paneled_wood](https://polyhaven.com/a/dark_paneled_wood) | CC0 1.0 |
| parquet | `assets/src/tex/parquet.png` -> `data/tex/parquet.ntx` | [Poly Haven: herringbone_parquet](https://polyhaven.com/a/herringbone_parquet) | CC0 1.0 |
| pavement | `assets/src/tex/pavement.png` -> `data/tex/pavement.ntx` | [Poly Haven: concrete_pavement](https://polyhaven.com/a/concrete_pavement) | CC0 1.0 |
| planks | `assets/src/tex/planks.png` -> `data/tex/planks.ntx` | [Poly Haven: brown_planks_03](https://polyhaven.com/a/brown_planks_03) | CC0 1.0 |
| plaster | `assets/src/tex/plaster.png` -> `data/tex/plaster.ntx` | [Poly Haven: plastered_wall_02](https://polyhaven.com/a/plastered_wall_02) | CC0 1.0 |
| plaster_beige | `assets/src/tex/plaster_beige.png` -> `data/tex/plaster_beige.ntx` | [Poly Haven: beige_wall_001](https://polyhaven.com/a/beige_wall_001) | CC0 1.0 |
| roof | `assets/src/tex/roof.png` -> `data/tex/roof.ntx` | [Poly Haven: bitumen](https://polyhaven.com/a/bitumen) | CC0 1.0 |
| rust | `assets/src/tex/rust.png` -> `data/tex/rust.ntx` | [Poly Haven: grey_cartago_01](https://polyhaven.com/a/grey_cartago_01) | CC0 1.0 |
| slats | `assets/src/tex/slats.png` -> `data/tex/slats.ntx` | [Poly Haven: rusty_metal](https://polyhaven.com/a/rusty_metal) | CC0 1.0 |
| stucco | `assets/src/tex/stucco.png` -> `data/tex/stucco.ntx` | [Poly Haven: white_stucco](https://polyhaven.com/a/white_stucco) | CC0 1.0 |
| tiles | `assets/src/tex/tiles.png` -> `data/tex/tiles.ntx` | [Poly Haven: interior_tiles](https://polyhaven.com/a/interior_tiles) | CC0 1.0 |
| veneer | `assets/src/tex/veneer.png` -> `data/tex/veneer.ntx` | [Poly Haven: black_walnut_veneer_01](https://polyhaven.com/a/black_walnut_veneer_01) | CC0 1.0 |
| wallpaper | `assets/src/tex/wallpaper.png` -> `data/tex/wallpaper.ntx` | [Poly Haven: floral_jacquard](https://polyhaven.com/a/floral_jacquard) | CC0 1.0 |
| wood | `assets/src/tex/wood.png` -> `data/tex/wood.ntx` | [Poly Haven: kitchen_wood](https://polyhaven.com/a/kitchen_wood) | CC0 1.0 |

Character animation: Mixamo needs an Adobe login, which isn't available to
the build environment, so the guard and dog clips are keyframed procedurally
in `tools/make_art.py`. Any glTF character with node-hierarchy animation can
replace them through the same converter.

## Code and libraries

| Component | Use | Licence |
|---|---|---|
| devkitPPC (GCC, binutils, newlib) | Wii toolchain | GPL (toolchain), newlib licences (runtime) |
| libogc 3.2.1 (incl. wiiuse, ASND, bte) | Wii system library | zlib-style libogc licence; RTEMS-derived threading code under a BSD-style notice (see libogc `LICENSE`). Credit libogc in the release README. |
| libfat-ogc 2.1.0 | SD/USB FAT access | ZPL (zlib-style) |
| SDL2 | Desktop backend | zlib |
| PCG32 algorithm | `src/core/nh_rng.c` | Algorithm by Melissa O'Neill; implementation follows the minimal C reference (Apache-2.0) |
