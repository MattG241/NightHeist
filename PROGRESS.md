# Progress

## Current phase: 7–8, Campaign (complete); next is 9–10, presentation and release

| Phase | Status |
|---|---|
| 0. Toolchain, both backends, test scene | Done |
| 1. Asset pipeline, level model, FP controller, collision, renderer v2 | Done |
| 2. Lighting: bake, projected torch, portals, transparency, frame pipelining | Done |
| 3. Guards: nav, AI, skinned characters, guard torches, detection HUD | Done |
| 4. Procedural generator (5 types × 3 sizes, 44 set pieces, 10k-seed test) | Done |
| 5–6. Interactables, 14 gadgets, 3 minigames, objectives, security, alarm/police, extraction, rating | Done |
| 7–8. Hideout, crew, upgrades, fence, saves, districts, post-game, balance | Done |
| 9–10. Audio and music, UI and title, settings, performance audit, release | Next |

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

## Phase 4 results

| Check | Result |
|---|---|
| Solvability, 10,000 seeds (all types, sizes and security levels) | 10,000/10,000 valid, 0 needed a retry; 28 rooms on average; about 1.3 ms each on desktop |
| Set pieces | 44 defined; all 44 appear across the 10k run |
| Determinism | Identical level, entities and guards on repeat runs; 15 golden fingerprints; Wii (Dolphin) matches desktop (`D667D5CA`) |
| Distinctness | 30 seeds per type and size: no duplicate fingerprints, no pair closer than 0.1 Jaccard on interior walls, mean distance above 0.3 |
| Anti-staleness | Avoided fingerprints never return; avoiding recent set pieces replaces most of them |
| Nav reach | 45 buildings: every room reachable from the street on the guards' nav grid, every guard spawn walkable |
| Wii | Generated office (3 storeys, 33 rooms): 59.9 fps, level arena 3.9/12 MB, geometry 4.4 MB, generation + build + bake 4.9 s emulated |

Dressing check across 1,500 buildings: no room ends up bare. About 20% of
open offices and 45% of archives need a second try at their furniture.

## Phase 5–6 results

- Tests: minigames, job, security (31.8k checks in all, 0 failures).
  - An ideal player always solves every lockpick, safe and hack tier.
  - Mistakes slip, and a hack times out into a trace.
  - Main objective, partial and bust outcomes; conduct objectives failing;
    no bonus for driving straight off; payout never rises with more
    sightings.
  - A camera spots a lit player; cutting the security power, jamming or
    smoke stops it; a laser trips and the mirror bypasses it; the
    door-alarm countdown and disarming work.
- Scripted desktop playtests, with screenshots in the PR:
  - door prompt, then the lockpick UI and a slip;
  - keycard pickup;
  - fuse box cut, with the room going dark;
  - thermal goggles showing guards through walls;
  - smoke;
  - objectives screen and summary screen.
- Wii (Dolphin): the same prompts and HUD at 59.9 fps.

## Phase 7–8 results

- **Campaign.** 4 districts, a job board with finales and loyalty jobs,
  and 6 crew with perks, flaws, cuts and loyalty.
- **Meta-progression.** 8 upgrade lines (16 levels), the fence with 5
  loot sets, heat and lying low, a schedule that unlocks gadgets and
  modifiers, and a post-game.
  Design is in `DESIGN.md` §12.
- **Safehouse UI.** JOBS, CREW, GEAR, FENCE and SETTINGS tabs, a plan
  screen and a payday screen. Works with the pointer or the D-pad.
- **Saves.** Versioned, big-endian and CRC-checked, with a backup slot and
  saving after every change. There is an opt-in CSV playtest log.
- **Tests:** save, board, economy (31.9k checks, 0 failures).
  - Save round trip:
    - byte-exact re-encode;
    - every single-byte corruption caught;
    - truncated and newer-version saves rejected.
  - Board rules, hiring and upgrade gates, modifiers reaching the
    generator, crew perks reaching the job, and the unlock schedule.
  - The economy simulation plays a full campaign on real generated
    buildings:
    - post-game after 33 jobs and about 8.6 hours;
    - districts open at jobs 7, 18 and 24;
    - never broke;
    - all 16 upgrade levels bought;
    - never more than 38 minutes without something new.
  - The timeline is in `PACING.md`.
- **Scripted desktop playtest:** board, plan, heist, summary, payday, then
  every tab, with `save.dat` and `save.bak` written.
- **Wii (Dolphin):** the safehouse renders and runs.

## Next: Phase 9–10, presentation and release

- Audio: sound effects for every system, and layered music driven by the
  alert level.
- A title screen, a pause menu, and UI polish.
- A performance and memory audit on Wii.
- Release packaging for the Homebrew Channel, and the README.
