# Night Heist: Design Document

Living document. It records what the game is, how it is built, and every
decision that constrains later work. Update it whenever a decision changes.

## 1. Pitch

A first-person stealth heist game for the Wii, with a grounded, realistic look
within Wii limits (*Silent Hill: Shattered Memories*, *The Conduit*,
*GoldenEye 007 (Wii)*): dark interiors, a strong dynamic torch beam and
believable materials. Procedural buildings with hand-designed set-piece rooms
sit on an authored four-district campaign of at least 6 hours.

Core loop: hideout (3D hub) → job board → choose crew member and loadout →
infiltrate → objective → escape → payout, heat and reputation → upgrades and
crew → repeat.

## 2. Platform and budgets

| | Target |
|---|---|
| CPU | Broadway, ~729 MHz PowerPC 750CL |
| GPU | Hollywood: fixed-function TEV (16 stages), 8 hardware lights, EFB copies |
| RAM | 24 MB MEM1 + 64 MB MEM2 |
| Output | 640x480 EFB, 480p when available, 4:3 or anamorphic 16:9 |
| Frame rate | 30 fps locked minimum, 60 fps if achievable |
| Geometry | 40–60k triangles per frame (starting budget, revise after profiling) |
| Draw calls | low; static geometry in display lists, batched per room and material |
| Textures | CMPR / I8 / IA8 (RGB565 where needed), mipmapped, under ~16 MB resident per level |

Budgets get locked in Phase 2 after profiling on real hardware.

## 3. Architecture

### 3.1 Layers

```
src/core      Portable C11. Deterministic, no platform calls, no globals with
              hidden state, unit-tested on desktop. Maths, RNG, arenas, the GX
              light model, the GX texture codec, mesh and texture generators.
              Later: level generator, AI, detection, economy, saves.
src/game      Game code. Talks to the machine only through platform.h and gfx.h.
              Simulation code (e.g. demo_sim.c) is pure and tested; scene,
              render and app code may call the platform.
src/platform  platform.h (lifecycle, time, input, files, audio, mem stats)
              gfx.h (renderer). Backends: wii/ (GX/libogc) and desktop/ (SDL2/GL 3.3).
```

### 3.2 Main loop

- Fixed **60 Hz** simulation (`nh_fixedstep`). Time is accumulated in units of
  µs × 60, so one tick is exactly 1,000,000 units and nothing drifts. At most
  4 ticks run per frame; any backlog beyond that is dropped rather than
  allowed to spiral.
- Rendering is decoupled and interpolates between the previous and current
  tick (`alpha`). A 50 Hz PAL display still simulates at 60 Hz.
- Button press and release edges collected on a frame that runs zero ticks
  carry over to the next tick, so no press is ever lost or applied twice.
- No allocation in the hot loop. Load-time data comes from arenas
  (`nh_arena`, 32-byte aligned for GX DMA). Entities, AI and particles will
  use fixed pools.

### 3.3 Determinism

- All randomness uses `NhRng` (PCG32), seeded explicitly. `rand()` is banned.
- The simulation is deterministic given a seed **on a given platform**.
- IEEE float results can differ between x86-64 and the 750CL (fused
  multiply-add, compiler contraction). Because "same seed → same building" must
  hold across desktop and Wii, the **level generator will use integer and
  fixed-point maths only**, and floats will appear only after the layout is
  decided (e.g. converting grid cells to metres). The generator's unit tests
  will hash layouts, and the Wii build will be checked against the same hashes.
- `-ffast-math` is not used anywhere.

### 3.4 Coordinate and matrix conventions

- Right-handed, +Y up, camera looks down −Z, units are metres.
- `NhMat34` is a row-major 3x4 affine matrix with column vectors, the same
  memory layout as libogc's `Mtx`, so it goes to GX without conversion. GL
  receives it with `transpose = GL_TRUE`.
- Meshes use counter-clockwise front faces. GX treats clockwise as front, so
  the Wii backend culls `GX_CULL_FRONT` (verified in Dolphin: walls render,
  back faces are culled). A unit test checks every generated mesh's winding
  against its normals.
- GX does **not** renormalise transformed normals, so models use rotation,
  translation and uniform scale only. The desktop shader doesn't renormalise
  either.

## 4. Rendering

### 4.1 Lighting model (shared by GX, the desktop shader and CPU code)

Per vertex, per light (`src/core/nh_light.h`):

```
L     = normalize(light_pos − P), d = |light_pos − P|
diff  = max(N·L, 0)                                  GX_DF_CLAMP
cos   = max(−L · light_dir, 0)
atten = max(a0 + a1·cos + a2·cos², 0) / (k0 + k1·d + k2·d²)   GX_AF_SPOT
out   = material × clamp(ambient + Σ colour × diff × atten, 0, 1)
```

The coefficient helpers reproduce libogc's `GX_InitLightSpot` and
`GX_InitLightDistAttn`. `GX_InitLightDir` negates its input (checked by
disassembling libogc 3.2.1), so `NhLight.dir` is the direction the light
points. The same CPU function (`nh_light_factor`) will drive the player
light-level meter and guards noticing torch beams, so gameplay agrees with
what is on screen.

### 4.2 Texture formats

Textures are stored in GX's native tiled, big-endian layout with all mip
levels concatenated (`src/core/nh_texconv.c`). The Wii uploads the bytes
directly. The desktop decodes them with the same codec, so RGB565/IA8
quantisation is identical. Phase 0 supports I8, IA8, RGB565 and RGBA8. CMPR
(DXT1-like) arrives with the Phase 1 asset converter.

### 4.3 Desktop emulation rules

- Lighting is per vertex only, never per pixel, and the vertex colour is
  quantised to 8 bits like GX.
- One TEV stage, `GX_MODULATE` (texture × vertex colour). More stages will be
  emulated as TEV setups are added, never with arbitrary shader effects.
- Rendering goes to an offscreen 640x480 RGB8 + Z24 buffer, scaled to the
  window. `--wide` emulates the anamorphic 16:9 mode.

### 4.4 Plan for later phases

- **Static lighting**: baked into vertex colours at load, behind a loading
  screen. Occlusion raycasts will march through the modular grid (DDA over
  2 m cells and wall flags) instead of testing triangles, which keeps the
  bake to a few seconds on Broadway. Ambient occlusion will be approximated
  from neighbouring cell occupancy.
- **Torch**: a projected cookie texture via texgen (`GX_TG_MTX3x4` from
  `GX_TG_POS` with a light-space matrix), combined in TEV with baked lighting,
  plus a GX hardware spotlight for the per-vertex term. Guard torches use the
  same technique, capped in number, with distant ones falling back to vertex
  lights.
- **Visibility**: room/portal culling (doors close portals) plus frustum
  culling.

## 5. Controls

| Wii | Action |
|---|---|
| Nunchuk stick | Move |
| Pointer | Look/aim with a Conduit-style bounding box: the torch and cursor move freely inside a central box, and the camera turns when the pointer pushes past its edge. Sensitivity, box size and turn speed are settings. |
| Z | Crouch (toggle or hold) |
| C | Interact |
| Nunchuk tilt | Lean |
| A | Gadget / takedown |
| B | Torch on/off |
| D-pad | Cycle gadgets |
| + / − | Pause and floor map / objectives |
| Rumble | Heartbeat as detection rises; clicks in lock and safe minigames |

If IR tracking is lost, the torch holds its last direction and the camera
stops turning. This is already implemented in the Phase 0 sim.

## 6. Game design (summary of the brief; details grow per phase)

- **Torch and darkness**: interiors are genuinely dark. The torch is essential
  but raises visibility, and guards can notice the beam on surfaces.
  Upgrades: focus/width, red filter, battery, quiet switch. The HUD shows the
  player's light level.
- **Procedural buildings**: warehouse, office, bank, mansion and museum in 3
  size tiers, grown as room graphs on a 2 m grid, across multiple floors with
  stairs, vents and roofs. At least 40 hand-designed set-piece rooms. A
  lock-and-key graph is validated by search, with regeneration on failure.
  The same seed always gives the same building, and the seed is shown on the
  job card.
- **Guard AI**: PATROL → SUSPICIOUS → INVESTIGATE → ALERT/CHASE → SEARCH →
  RETURN. Vision cones with multi-point raycasts, hearing through walls,
  noticing changes, radio, systematic search, A* on a nav grid, and 10–12
  active guards with staggered updates.
- **Player options**: hiding, distractions, takedowns and body dragging,
  cameras, power, vents, and in-world minigames (lockpick, safecrack, hack).
- **Meta**: 6 crew members (role, perk, flaw, loyalty job), upgrades, fence
  and loot sets, reputation and heat, and SD saves (versioned, checksummed,
  with a backup slot).
- **Content and pacing**: 4 districts of about 1.5 hours each, at least 10
  objective types, 10 modifiers, 8 guard archetypes and 14 gadgets, plus an
  anti-staleness history and adaptive security. See [PACING.md](PACING.md).

## 7. Feasibility notes (flagged early)

| Feature | Assessment |
|---|---|
| Projected-texture torch | Feasible: texgen from position plus TEV is standard GX practice. Fill rate is the risk; measure in Phase 2. |
| Baked vertex lighting at load | Feasible if raycasts use the grid rather than triangles (see 4.4). |
| Skeletal guards (~30 bones) | GX has 10 position matrices per draw; meshes get split by bone palette, or CPU skinning is used. Both will be measured in Phase 3. |
| Thermal goggles (elite guards, player gadget) | Feasible: render a heat pass with TEV colour remapping, no shaders needed. |
| 3D audio with wall attenuation | ASND gives per-voice volume and pan; occlusion is computed on the CPU from the room graph. |
| Wii Remote speaker | Stretch goal only. |

## 8. Debug and tools

- Phase 0 HUD: fps, CPU ms, GPU-wait ms, triangles, draw calls, heap,
  MEM1/MEM2 free, input state, SD status.
- Desktop `--autotest --frames N --screenshot` runs give reproducible smoke
  tests.
- Coming next: the full debug overlay (Phase 1), AI debug views (Phase 3),
  a top-down seed map renderer (Phase 4), and opt-in playtest logging to SD
  (Phase 8).
