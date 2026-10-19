# Progress

## Current phase: 0, Toolchain (complete, awaiting hardware test)

### Done

- Repo skeleton, Makefile targets: `desktop`, `test`, `wii`, `wii-docker`,
  `assets`, `package`, `clean`.
- Portable core (`src/core`): maths (GX-layout matrices), PCG32 RNG, arena
  allocator, fixed-timestep accumulator, GX light model with libogc-identical
  coefficient helpers, GX texture codec (I8/IA8/RGB565/RGBA8, tiled,
  big-endian, mips), procedural meshes and textures, an original 5x7 font.
- Platform interface (`platform.h`, `gfx.h`) and two backends:
  - **Wii**: VIDEO/GX init (480p via the preferred mode, anamorphic 16:9 when
    the console is set to widescreen), display-list meshes, hardware
    per-vertex lights, batched 2D, WPAD pointer/buttons/Nunchuk/rumble, ASND,
    libfat (data dir taken from `argv[0]` so USB launches work), reset/power
    button handling.
  - **Desktop**: SDL2 + GL 3.3 core, 640x480 offscreen EFB, GX lighting
    equation per vertex, textures decoded from the same GX bytes, mouse +
    WASD input mapping, an ASND-like 16-voice mixer, and
    `--autotest/--frames/--screenshot` for automated runs.
- Phase 0 test scene: a dark brick room, spinning lit and textured tori, a
  pointer-aimed torch (GX hardware spotlight) that holds direction when IR is
  lost, crouch, lean, click sound panned by pointer, rumble pulse, a stress
  test (1/9/25/36/49 tori), and a live input/perf/memory/SD readout.
- Unit tests: 9 suites, about 28k checks (maths, RNG golden values,
  arena, timestep at 60/50 Hz, light model vs libogc formulas, texture codec
  round trips, mesh winding vs normals, font, sim determinism and IR hold).
  Clean under ASan and UBSan.
- HBC packaging: `meta.xml` and a generated `icon.png` (128x48).

### Verified

| Check | Result |
|---|---|
| `make test` | 9/9 suites pass; ASan/UBSan clean |
| Desktop (Xvfb + Mesa llvmpipe) | Renders; 400-frame autotest clean under sanitizers |
| Wii build | Clean with `-Wall -Wextra`; `boot.dol` 520 KB |
| Dolphin 2609a: boot ELF and packaged `apps/NightHeist/boot.dol` from virtual SD | Boots, 59.9 fps, SD read OK, HOME exits |
| Dolphin: IR pointer, Wii Remote buttons, 16:9 path | Working |
| Dolphin vs desktop image | Visually matching lighting and textures |
| Dolphin: Nunchuk | **Not detected** (see known issues) |

### Numbers so far (Dolphin, not representative of hardware)

| Scene | Triangles | Draw calls |
|---|---|---|
| 1 torus (default) | 9,850 | 7 |
| 49 tori (stress max) | 120,426 | 55 |

Memory in Dolphin: heap 2.0 MB used, MEM1 16.7 MB free, MEM2 53.8 MB free.
Dolphin's CPU and GPU-wait timings are meaningless (the GPU runs on its own
thread), so the real CPU ms, GPU-wait ms and fps for each stress step must
come from hardware.

### Known issues / open items

1. **Nunchuk not detected in my headless Dolphin setup.** Dolphin attaches
   the extension (its log says "Switching to Extension 1") but libogc's
   `exp.type` stays 0. The same happens with a Classic Controller and with
   MotionPlus detached, so it is the libogc ↔ Dolphin extension handshake
   in that environment, not the Nunchuk code. The HUD now shows the raw
   expansion id. **Needs checking** in a desktop Dolphin (Wii Remote 1 →
   Emulated → Extension: Nunchuk; also try unticking *Attach MotionPlus*)
   and on real hardware.
2. The CPU waits on `GX_DrawDone` every frame (CPU and GPU serialised).
   That's fine for Phase 0. Phase 2 will pipeline with draw-done tokens or a
   third framebuffer.
3. Vertex data is still float32 (12 B per position or normal). The Phase 1
   asset converter quantises to s16 and adds triangle strips.
4. Real-hardware unknowns: 480p detection on component cables, the 16:9
   VI width trick on your TV, rumble feel, ASND click latency, libfat SD
   init time.

### Next: Phase 1, Foundations (after your go-ahead)

- Asset pipeline: glTF 2.0 → quantised binary meshes with strips; PNG →
  CMPR/I8/IA8/RGB565 with mips; animation tracks.
- First-person controller with the bounding-box turn scheme and its settings.
- Capsule collision against level geometry.
- Debug overlay: FPS, triangles, draw calls, memory, seed.
