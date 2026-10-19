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
| Skeletal guards | Done in Phase 3 with rigid skinning: 11-bone guard, 7-bone dog, each model part draws with at most 10 bones through the GX `PTNMTXIDX` matrix palette, so no CPU skinning. Six guards plus the level hold 59.9 fps in Dolphin. |
| Thermal goggles (elite guards, player gadget) | Feasible: render a heat pass with TEV colour remapping, no shaders needed. |
| 3D audio with wall attenuation | ASND gives per-voice volume and pan; occlusion is computed on the CPU from the room graph. |
| Wii Remote speaker | Stretch goal only. |

## 8. Debug and tools

- Phase 0 HUD: fps, CPU ms, GPU-wait ms, triangles, draw calls, heap,
  MEM1/MEM2 free, input state, SD status.
- Desktop `--autotest --frames N --screenshot` runs give reproducible smoke
  tests.
- Debug overlay (1): fps, CPU/GPU ms, triangles, draws, visible rooms,
  geometry/texture/resource MB, arena use, MEM2 free, seed, build time,
  position, room, light level, and one line per guard (archetype, state,
  suspicion, visibility, awareness, floor, torch).
- AI view (2, press again for thermal): view cones and nav paths drawn on the
  floor, last-known-position markers, and state labels over every guard.
- `debug_start.txt` next to `boot.dol` (Wii) or `--start x,y,z,yaw,pitch`
  (desktop) spawns the player anywhere.
- Coming next: a top-down seed map renderer (Phase 4) and opt-in playtest
  logging to SD (Phase 8).

## 9. Guards (Phase 3)

The simulation is pure C in `src/core/nh_ai.c` and `nh_nav.c`. It has no
platform calls and is deterministic: the same seed and the same inputs give
the same guard positions on Wii and desktop (unit-tested).

**Navigation.** A 1 m grid (2×2 nodes per cell) per floor is built from the
level. Walls, windows and rails block movement; furniture lower than 1.5 m
blocks the nodes it covers. Doors are passable (guards open them) at a small
cost, and locked doors at a large one. Stair ramps link to the floor above.
Paths come from A* with a binary heap, then string-pulling, and keep a
waypoint either side of each doorway so guards walk through door centres. An
LRU cache holds 24 recent paths. Dogs path with doors closed.

**Perception.** Each guard thinks every other tick (staggered).
- *Sight.* Range is `view_range × (0.22 + 0.78 × light)`, with 12 m minimum
  for thermal goggles. The field of view is plus 35° of peripheral vision
  within 5 m. Three raycasts (head, chest, feet) give the fraction of the
  player that is visible.
- *Suspicion gain per second.* The visible fraction is multiplied by:
  - distance falloff `1 − (d/range)²`;
  - a light term;
  - stance (crouched 0.6);
  - motion (still 0.6, walking 1.0, running 1.6);
  - archetype gain;
  - awareness;
  - difficulty;
  - ×2.5 within 2.5 m.
- *Light on the player* is the baked light meter plus their own torch, plus
  any guard torch whose beam falls on them, so walking into a beam is
  dangerous.
- *The player's torch beam* hitting a surface in a guard's view raises
  suspicion even when the player is out of sight.
- *Hearing.* Noise radius × hearing × 0.45 for each blocker (wall, closed
  door, closed window, or two per floor). Sound gives only an approximate
  direction.
- *Changes noticed.* Doors left open, missing loot, bodies, dead cameras,
  lights off, broken glass and open vents are added by the game as
  anomalies. Each guard reacts to each anomaly once.

**States.** The thresholds are 0.3 SUSPICIOUS (stop and turn), 0.65
INVESTIGATE (walk over) and 1.0 ALERT (chase, radio after 1.5 s).
- *Losing the player* turns ALERT into SEARCH. Search points are:
  - the last known position;
  - hiding spots within 2.2 m of it (and within 9 m for archetypes that check
    hiding places);
  - random points in that room and the rooms one or two doors away, visited
    nearest-first.
- *After the search time* the guard returns to the route. Awareness rises
  with each alert and makes the guard faster to notice next time.
- *Radio calls* send the two nearest guards running and the rest walking.
  Elite and police guards flank 4 m to either side.
- *The building alarm* trips after `alarm_threshold` alerts.
- *Caught* is ALERT, in sight, within 1.3 m.

**Archetypes** (`nh_ai_arch`):
- *Rookie:* distractible, slow to suspect.
- *Guard:* the standard.
- *Veteran:* checks hiding spots, searches for longer.
- *Lazy:* skips patrol points, naps, carries no torch.
- *Handler and dog:* the dog follows the handler, smells you within 6 m (3 m
  under a door), barks, and brings the handler over. It can't open doors.
- *Camera operator:* stays at the console.
- *Supervisor:* radio check-ins. A knocked-out guard who misses one triggers
  a search and counts as an alert.
- *Elite:* thermal vision, flanking.
- *Janitor (civilian):* flees to the nearest guard to report you.
- *Police:* arrive after the alarm (Phase 6).

**Player interactions so far.**
- *Takedown (A)* from behind or on an unaware or sleeping guard. It is noisy
  (4.5 m) and leaves a body anomaly.
- *Opening a door* adds a door anomaly; a guard who investigates closes the
  door again.
- *Distractions:* `ai_distraction` sends guards to a spot to look around,
  with little suspicion.
- *Scripted events:* `ai_send_guard` moves one guard somewhere.

**Presentation** (`src/game/guards.c`).
- *Animation.* Clips are chosen from the AI state, cross-faded over 0.25 s,
  with locomotion playback scaled to the actual speed. A head turn is layered
  on top for looking around.
- *Torches.* The nearest lit guard torch gets the second projected cookie
  (TEV `ALPHA1` path); up to four more become per-vertex spot lights, and
  every lens gets a camera-facing glow.
- *Lighting.* Characters use `GFX_MAT_VCOLOR`: vertex colour as albedo, lit
  by ambient plus dynamic lights. On GX this is only the channel's material
  source set to the vertex.
- *HUD.*
  - A pip on a ring around the cursor for every guard who is getting
    suspicious, pointing their way and filling with suspicion.
  - "?" or "!" over guards in view.
  - Bark subtitles within 16 m.
  - "ALARM" and "CAUGHT" screens.
  - A heartbeat rumble that speeds up from 0.45 suspicion upwards.


## 10. Procedural buildings (Phase 4)

Code: `src/core/nh_gen.c` (layout), `nh_gen_dress.c` (furniture and set
pieces), `nh_gen_sec.c` (security, loot, guards).
`nh_gen_building(Level*, NhGenParams*, NhPlan*)` fills the level and a
**plan** for the heist:
- the entities: loot and the objective, keycards, code notes, keypads,
  cameras, lasers, alarm panels, fuse boxes, consoles, safes, terminals,
  phones, the ladder and the hatch;
- guard rosters with patrol routes;
- the entry points and the extraction point.

**Determinism.** Every decision is integer maths on PCG32, and floats only
appear as outputs. The same seed gives the same building on Wii and desktop:
office medium #7 gives fingerprint `D667D5CA` in Dolphin and on desktop.
`tests/gen_golden.inc` pins 15 layouts.

**Footprint.** Five types in three sizes, with ±2 cells of variation:

| Type | Storeys (S/M/L) | Plan |
|---|---|---|
| Warehouse | 1/1/2 | Racking hall. An office block on the left, right or along the front. Loading bays (1–3, 2–4 cells deep). An optional row of cages and an optional hall partition. |
| Office | 2/3/4 | Corridor spine with strips of rooms either side; a cross corridor in large buildings. |
| Bank | 2/2/3 | Spine with deep back strips, so vaults sit behind antechambers. |
| Mansion | 2/2/3 | A grand hall from the front door to the stairs; rooms from a BSP split, joined to the hall and to each other. |
| Museum | 1/2/2 | Spine of galleries, with neighbouring galleries joined by arches. |

**Stairs.** The stairwell is 3 cells wide. Even floors climb in the left
column, odd floors climb back in the right column, and the centre column is
a walkway that stays connected on every floor. A 2-wide switchback would
strand one end of the room on alternate floors, because each flight climbs a
whole storey.

**Functions.** Each type has a list of needs: function, floor, size range,
front/back/exterior preference, count per size, and the security level it
wants. The rest of the rooms get filler functions.

**Set pieces.** There are 44 hand-designed layouts (`k_setpieces`), for
example:
- gold vault with lasers;
- server farm with a hackable terminal;
- the diamond on a pedestal inside a laser cage;
- master bedroom with a safe;
- bonded cage;
- security hub with consoles.

Secure rooms get one first. Ordinary rooms get one 70% of the time, and a
piece is not repeated in a building unless it is marked as repeatable.
`avoid_setpieces` (the campaign's recent history) is preferred against, and
ordinary rooms fall back to plain furniture rather than repeating a stale
piece.

**Furniture.**
- Props snap to a 0.5 m occupancy grid. Doorways (1.2 × 1.25 m), stairs,
  vent grates and the hatch are kept clear, and tall furniture never covers
  a window.
- After a room is dressed, its 1 m nav nodes are checked with the same
  blocking test the guards' nav grid uses. Every doorway, arch and grate must
  connect, and at most 2 free nodes may be cut off.
- If the check fails, the room is rolled back and re-dressed: a second try,
  then default furniture, then a light dressing.

**Security.**
- *Exterior doors* take a lockpick; they are harder at higher security and
  alarmed from security 2.
- *Secure rooms* get keycard, code, maglock (cut the security fuse) or vault
  (card plus code) locks. Arches into them become doors so a lock can't be
  walked around.
- *Keys are placed constructively.* Locks are processed shallowest first.
  Each key goes in a room reachable with that lock still closed and only the
  keys placed so far, preferring desks in offices, security rooms and locker
  rooms. So every building is solvable by construction.
- *Cameras* go in the corners of corridors, lobbies and secure rooms.
- *Fuse boxes:* one per floor for the lights, plus the security circuit.
- *Alarm panel* by the front door.
- *Vents:* straight ducts lead from an ordinary room into a secure one.

**Validation.** `nh_gen_validate` is an independent fixpoint search over
rooms. It starts outside and crosses doors, arches, stairs and vents using
the keys collected so far. It requires every room, the objective and an
extraction point to be reachable; on failure the generator retries with the
next attempt. `avoid_layouts` (recent fingerprints) are never returned.

**Guards.**
- *Rosters* are per type: dog handlers at warehouses and mansions, camera
  operators at banks, offices and museums, janitors in offices and museums,
  elites at high security.
- *Count:* 3, 5 or 7 by size, plus security/2, plus extras, up to 14.
- *Stations:* camera operators sit at the console. Veterans may be posted
  outside the objective's door. Handlers walk the perimeter with the dog.
- *Patrols* are loops through 3–6 rooms of one floor, always through its
  corridor, with actions by room (coffee in break rooms, a smoke outside).
  Lazy guards get a nap.

**Tools.**
- `make seedmap` builds `build/seedmap`. It renders a building, or a sheet of
  seeds, as a top-down PNG showing rooms, locks by colour, keys, loot, the
  objective, cameras, lasers, vents and patrol loops.
- In game, with the debug overlay on (1), + regenerates with a new seed and
  − cycles the building type.
- Desktop `--job TYPE,SIZE,SEED[,SEC]` or `--test-level` picks the job; on
  Wii, a `job ...` line in `debug_start.txt` does the same.

## 11. Heist systems (Phases 5–6)

The pure logic lives in core and is unit-tested:
- `nh_job.c`: objectives, inventory, gadgets, stats, the alarm and police
  clock, and the rating.
- `nh_minigame.c`: lockpick, safecrack and hack.
- `nh_security.c`: cameras, lasers, alarmed doors, power and EMP.

The game layer is `interact.c`, `gadgets.c`, `heist.c` and `hud.c`.

**Interaction (C).**
- *Targeting.* Everything within 2.3 m along the aim (the torch direction)
  is scored and the best target wins. Line of sight is required. Small
  items win ties.
- *Timed actions.* Some actions need C held: swipe 0.5 s, keypad 1 s,
  carry heavy loot 1.2 s, cut the power 2 s, disarm the alarm 3 s, vault
  4 s. C must be released before the next action can start.

| Target | Action |
|---|---|
| Doors | Open/close. Pick (lockpick minigame); swipe a keycard; enter a known code; maglocks open when the security circuit is cut; the vault needs card + code. Opening an alarmed exterior door starts a 15 s entry delay: disarm at the panel inside or the alarm sounds. |
| Loot | Small items go in the bag (12 slots). Heavy items (gold, paintings, statues, the crate) are carried in your hands at 60% speed and only count at the van. Items in display cases trip the alarm unless the security power is off. Taking loot leaves a "missing" anomaly for guards. |
| Keycards, code notes | Picked up into the inventory. |
| Safes | Safecrack minigame, or the drill gadget: 18 s, loud. Contents become takeable. |
| Terminals | Hack minigame. Success completes hack/wipe objectives and downloads every keypad code; running out of time traces you and guards converge. |
| Security console | Loop the camera feeds for 2 minutes. |
| Fuse boxes | Cut or restore a lighting circuit or the security circuit. Rooms re-light one per tick. A guard who reaches a cut box restores it after 2.5 s. |
| Alarm panel | Disarm the door alarms. |
| Phones | Plant a bug (objective). |
| Hiding spots | Lockers, wardrobes, stalls, dumpsters, and desks when crouched. Guards can't see you, but a veteran checking that spot pulls you out. |
| Bodies | Pick up (carried behind you, slow), put down, or hide in a locker, wardrobe or dumpster, which removes the body anomaly. |
| Vents, ladder, roof hatch | Climb through the building's alternative routes. |
| The van | Leave: success with the target, otherwise a partial/abort. |

**Gadgets (D-pad to choose, A to use).** There are 14:

| Gadget | Effect |
|---|---|
| Lockpicks | Always carried. |
| Coins | Thrown: a small distraction. |
| Noisemaker | Thrown: beeps for 9 s, drawing guards from 16 m. |
| Smoke | Thrown: a cloud that blocks guard and camera sight for 16 s. |
| EMP | Thrown: cameras and lasers within 7 m dead for 15 s. |
| Taser | Knocks out up close, noisily. |
| Dart gun | Silent; the guard drops after 3.5 s and may bark first. |
| Jammer | Blinds a camera for 20 s. |
| Glass cutter | Cuts a window open quietly. Guards notice the cut glass. |
| Card cloner | Copies keycards from an unaware guard: level 1–3 by rank. |
| Thermal goggles | D-pad up toggles them; 90 s battery. Warm guards drawn through walls with `GFX_MAT_XRAY`. |
| Laser mirror | Bends one laser beam out of the way. |
| Safe drill | Drills a safe open: 18 s, loud. |
| Phone dialer | Rings a desk phone to pull a guard over. |

**Minigames.**
- *Lockpick:* twist the Wii Remote to each pin's sweet spot. The feel meter
  and rumble ticks show how close you are; A sets the pin. A slip makes
  noise and costs 0.6 s.
- *Safecrack:* the twist angle sets the dial speed, alternating direction
  for each number. Stop on the clunk and press A.
- *Hack:* the pointer matches a signal's frequency (x) and amplitude (y);
  hold the match to fill each layer before the trace timer runs out.

**Security.**
- *Cameras* sweep ±40° over 12 s. Their detection fills faster with light,
  closeness and movement.
- When a camera fills, what happens depends on the operator:
  - with an operator at the console, the two nearest guards converge on you
    and it counts as an alert;
  - with no operator, the camera only records, except at security 3+, where
    it raises the alarm.
- *Lasers* trip the alarm if the beam passes within 0.28 m of the player's
  capsule (crouching changes its height).

**Alarm and police.**
- The alarm starts a police clock of 100 − 12×security seconds.
- Three police officers then arrive at the street entry and sweep from your
  last known position; two more come every 45 s.
- Being caught is a bust.

**Objectives and rating.** Each job has the main target plus 1–2 optional
objectives drawn from what the building holds, and one conduct objective:

| Kind | Objectives |
|---|---|
| Main | Steal (the target) |
| Optional | hack, wipe, safe, photograph, bug |
| Conduct | ghost, no alarm, no takedowns, a loot target, out within 8 minutes |

- *Stealth score:* starts at 100.
  - −12 per time spotted by a guard;
  - −8 per camera spot;
  - −25 per alarm;
  - −10 per body found;
  - −4 per takedown;
  - +10 for a clean ghost.
- *Grade:* S, A, B, C or D, from stealth and objectives done. It scales the
  payout from 80% to 120%.
- *Heat:* rises with alarms, sightings and bodies; it is 0 for a ghost.
- *Results:* a bust pays nothing. Leaving without the target pays no
  bonuses, so walking straight back to the van earns nothing.

**Screens.**
- \+ opens the floor map, showing only rooms you've seen: explored rooms,
  walls, doors (red when locked), objective markers and the van.
- − opens the objectives, bag, stats and gadgets.
- The summary shows the outcome, objectives, loot, bonuses, payout, grade,
  stealth and heat. A takes the next job; B retries.

## 12. Campaign and safehouse (Phases 7–8)

The pure logic lives in core and is unit-tested:
- `nh_campaign.c`: districts, the job board, crew, upgrades, the fence,
  heat, payday and the unlock schedule.
- `nh_save.c`: save files.

The safehouse UI is `src/game/hideout.c`. `app.c` switches between the
safehouse and a heist.

**Money.** Amounts are stored in units of $10 so they fit comfortably in
32 bits. A new career starts with $20,000, the Driver and the Hacker.

**Districts.**

| District | Opens at | Buildings | Base security | Pay |
|---|---|---|---|---|
| The Docklands | start | warehouse, office | 0 | ×1.00 |
| Midtown | 70 rep + Docklands finale | office, bank | 1 | ×1.35 |
| The Old Quarter | 190 rep + Midtown finale | museum, mansion | 2 | ×1.70 |
| The Heights | 340 rep + Old Quarter finale | bank, mansion, museum | 3 | ×2.20 |

**Job board.**
- Four offers, mostly from the newest open district. About 35% come from
  older districts once more than one is open.
- An offer has a seed, building type, size and security, with security
  raised by 1 for every 34 heat.
- It has 0–3 modifiers from those unlocked so far; contradictory pairs are
  skipped.
- It has an estimated payout, from typical generator loot totals × the
  district multiplier × (1 + 15% per security level).
- Size grows with the jobs done in that district: mostly small at first,
  then medium and large.
- After 5 jobs in the newest district a **finale** is offered. It is large,
  +1 security, carries the *bonus target* modifier and pays double.
  Finishing it completes the district.
- A **loyalty job** is offered for a hired crew member with loyalty ≥ 60.
  It is a medium building of their story type, and finishing it unlocks
  their second perk.
- The **plan screen** shows the modifiers with their descriptions. The
  player picks up to two hired crew to bring.

**Crew.**

| Member | Role | Perk | Flaw | Cut | Hire |
|---|---|---|---|---|---|
| Nadia Volkova | Hacker | Hacks one layer shorter; one keypad code known | — | 18% | start |
| Eddie Price | Locksmith | One pin fewer | Noisier slips | 14% | 40 rep |
| Rosa Delgado | Driver | Police +30 s | +5 heat per job | 12% | start |
| Big Tam | Muscle | Silent takedowns; faster carrying | Bodies found sooner | 12% | 90 rep |
| Kit Mercer | Lookout | Guards and cameras on the map | One fewer bag slot | 10% | 130 rep |
| Oscar Bell | Face | Fence pays 15% more | — | 20% | 220 rep |

Second perks:
- Hacker: every code known.
- Locksmith: +1 drill charge.
- Driver: police +60 s.
- Muscle: +2 taser charges.
- Lookout and Face keep their perks.

Hiring costs `$30,000 + rep × $600`. Each job a member comes on adds 12
loyalty, or 20 on an A or S grade.

**Upgrades.** Eight lines with 16 levels in total, from $25k to $320k:
torch, bag, soft soles, picks, gadget kit, thermal cells, fast darts and the
scanner. Thermal cells and fast darts are offered only once their gadget is
in the kit.

**Payday.**
- The take is the loot banked plus bonuses, scaled by grade.
- Net pay is the take minus the crew's cut, plus any **loot set** bonus.
  The fence pays for five collections: old masters, crown jewels, watches,
  data and bullion, with 3–4 pieces each and bonuses of $100k–$200k.
- Reputation by grade: S +25, A +18, B +12, C +8, D +4. A bust costs 10.
  A ghost adds 5, a finale adds 30, and a partial take with loot earns 3.
- Heat rises by 5 × the job's heat in that district and falls by 6
  everywhere else. Lying low costs `$25,000 + rep × $100` and cools every
  district by 35.

**Unlock schedule.**
- A new career has lockpicks, coins and the taser.
- Every other gadget and modifier arrives at a fixed career step (jobs
  finished or failed): `k_gadget_at` and `k_mod_at`.
- Payday announces each arrival.
- Over steps 1–30 no two consecutive steps go without one. `PACING.md`
  lays this out against play time.

**Post-game.**
- Finishing the Heights finale unlocks the post-game.
- Every gadget and modifier is available.
- Every contract gets +1 security and +1 modifier, and the board keeps
  coming.

**Anti-staleness.**
- The last 24 layout fingerprints are passed to the generator, which
  rerolls any repeat.
- Set pieces used in the last 3 jobs are avoided.

**Saves.**
- `save.dat` in the app folder, holding settings and the campaign.
- Header: `'NHSV'`, u16 version, u16 reserved (must be 0), u32 payload
  length, CRC-32 of the payload.
- The payload is big-endian and written field by field, so the same bytes
  load on Wii and desktop.
- Decoding range-checks every field and rejects:
  - a newer version;
  - a bad magic or a non-zero reserved field;
  - truncation;
  - a checksum mismatch.
- Before writing, a valid previous save is copied to `save.bak`. Loading
  falls back to the backup if the main file fails.
- The game saves after every payday, purchase, hire or settings change.

**Playtest log.** An opt-in setting appends one CSV row per job to
`playlog.csv`: career step, district, type, size, security, modifiers,
outcome, grade, seconds, sightings, alarms, takedowns, payout, net, money and
reputation. It is how `PACING.md`'s
measured column gets filled.

**Safehouse UI.**
- Tabs: JOBS, CREW, GEAR, FENCE and SETTINGS.
- Driven by the IR pointer (A to click) or the D-pad (with A). B backs out
  of the plan screen.
- Settings cover pointer smoothing, the turn box, turn speed, crouch
  toggle, invert, rumble, subtitles, music and SFX volume, difficulty and
  the playtest log.
