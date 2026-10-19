# Progress

## Current phase: 3, Guards (complete); next is 4, the procedural generator

| Phase | Status |
|---|---|
| 0. Toolchain, both backends, test scene | Done |
| 1. Asset pipeline, level model, FP controller, collision, renderer v2 | Done |
| 2. Lighting: bake, projected torch, portals, transparency, frame pipelining | Done |
| 3. Guards: nav, AI, skinned characters, guard torches, detection HUD | Done |
| 4. Procedural generator (5 types × 3 sizes, 40+ set pieces, 10k-seed test) | Next |
| 5–6. Interactables, gadgets, minigames, objectives, alarm/police, extraction | Pending |
| 7–8. Hideout, crew, upgrades, fence, saves, districts, post-game, balance | Pending |
| 9–10. Audio and music, UI and title, settings, performance audit, release | Pending |

## What exists

**Build.**
- Make targets: `desktop`, `test`, `wii` (or `wii-docker` with the
  devkitPro image), `assets`, `package`, `clean`.
- CI builds the desktop and Wii targets, runs the tests under ASan and
  UBSan, and uploads the HBC package.

**Core (`src/core`)**, portable C11 with no platform calls:
- maths, PCG32, arenas, fixed 60 Hz timestep;
- the GX light model;
- the GX texture codec;
- the asset formats (`.ntx`, `.nmd`, `.nan`) and pose blending and skinning;
- the level model (cells, edges, doors, stairs, props, lights, raycasts,
  cylinder collision);
- the modular kit mesh builder;
- the light baker (GX attenuation, grid DDA shadows, bounce and AO);
- portals;
- the first-person controller;
- navigation;
- guard AI.

**Wii backend.**
- Display-list-free indexed s16 meshes with separate colour arrays.
- A 5-stage TEV chain:
  - two projected cookies (player torch, nearest guard torch);
  - baked or dynamic vertex lighting;
  - albedo.
- Rigid skinning through the matrix palette.
- Thermal mode.
- Fog.
- Triple XFB with draw-done pipelining.

**Desktop backend.** SDL2 and GL 3.3, with shaders that mirror the TEV chain
and quantise to 8 bits like GX.
- `--script` runs scripted input with screenshots.
- `--start` spawns the player anywhere.

**Content pipeline.**
- `tools/make_art.py` generates:
  - 62 props;
  - the guard and dog rigs with their clips (15 for the guard, 6 for the
    dog);
  - procedural textures.
- `tools/fetch_polyhaven.py` pulls 31 CC0 surface textures.
- `tools/nhconv.py` converts everything to GX formats (vectorised CMPR
  encoder). The data comes to about 1.5 MB.

**Test building.**
- 2 storeys plus a roof and 14 rooms.
- Locked doors, windows, a vent run, a roof hatch.
- Lights on circuits, moonlight, street lights.
- Hiding spots.
- 6 guards:
  - a patrol guard, a veteran upstairs, a lazy napper;
  - a supervisor;
  - a dog handler and dog outside.

## Verified

| Check | Result |
|---|---|
| `make test` | 17 suites, 29,296 checks, 0 failures; clean under ASan and UBSan |
| Pathfinding | 300 random walkable pairs across both floors all path; stairs, doors, the dog without doors, and the cache are covered |
| Detection maths | Cone, peripheral, occlusion by walls and floors, darkness range, crouch ×0.6, motion, thermal, hiding and vents, archetype ordering |
| AI scenarios | Standing lit in front of a patrol gets you caught in under 12 s; crouching still in a dark corner for 40 s is never spotted; footsteps through a wall barely register; distractions, takedown rules, body → search plus radio, alarm threshold, supervisor missed check-in |
| Determinism | Two 30 s runs of the full roster end bit-identical |
| Desktop | Guards patrol, open and close doors, carry torches in the dark, spot, radio, chase, catch and reset; screenshots in the PR |
| Wii (Dolphin 2609a) | Same scene at 59.9 fps, about 19.5k tris and 59 draws in the corridor view; the guard is in the same place as on desktop at the same moment |

## Numbers (Dolphin, not representative of hardware)

| Scene | Triangles | Draws | Notes |
|---|---|---|---|
| Street view (Phase 2) | 17.5k | 32 | |
| Ground-floor corridor with 2 guards visible | 19.6k | 59 | |
| Level build plus bake | | | 1.87 s emulated |

- Arenas in use: persistent 2.3/4 MB, level 2.5/12 MB (the nav grid takes
  about 1 MB of that), scratch 6.6/10 MB.
- Dolphin reports CPU and GPU times that don't mean anything; real numbers
  have to come from hardware.

## Known issues / open items

1. **Nunchuk in headless Dolphin**: still not detected in this CI-style
   setup (libogc never sees the extension). It needs checking in a desktop
   Dolphin or on hardware. The keyboard and pointer paths work.
2. Bodies can't be dragged yet. Hiding in lockers, wardrobes and under desks
   is supported by the AI (`hiding_prop`) but has no player interaction yet;
   both come in Phase 5.
3. Guard voices are subtitles only; audio arrives in Phase 9.
4. Real-hardware unknowns: fill rate with two projected cookies in large
   rooms, libfat init time, rumble feel.

## Next: Phase 4, procedural generator

- An integer-only generator, so the same seed gives the same building on Wii
  and desktop:
  - 5 building types in 3 size tiers;
  - a room graph on the 2 m grid, multiple floors, stairs, vents, roofs;
  - at least 40 set-piece templates;
  - furniture dressing;
  - lights on circuits.
- Lock-and-key placement validated by search, with regeneration on failure.
- Guard rosters and patrol routes generated per building.
- Tests: solvability over 10,000 seeds, layout distinctness, and a
  top-down seed-map renderer.
