# Night Heist

A first-person stealth heist game for the Nintendo Wii, run as homebrew from the
Homebrew Channel. The game is portable C11 with two backends:

- **Wii**: GX / libogc. This is the real target.
- **Desktop**: SDL2 and OpenGL 3.3, used for fast iteration, debug tools and
  tests. It deliberately emulates the Wii's limits: per-vertex GX lighting,
  TEV-style combining, GX texture formats and a 640x480 framebuffer.

**Status: Phase 0 (toolchain).** Both backends render a lit, textured, spinning
test scene with a pointer-aimed torch and read pointer and Nunchuk input. See
[PROGRESS.md](PROGRESS.md) for what's done, what's next and known issues. The
design lives in [DESIGN.md](DESIGN.md).

## Building

| Target | What it does |
|---|---|
| `make desktop` | Desktop build at `build/desktop/nightheist` (needs SDL2 dev + OpenGL) |
| `make test` | Builds and runs the unit tests (host compiler only, no SDL) |
| `make wii` | `build/wii/nightheist.dol` (needs devkitPPC, see below) |
| `make wii-docker` | Same as `make wii`, inside the official `devkitpro/devkitppc` Docker image |
| `make package` | `dist/apps/NightHeist/{boot.dol,meta.xml,icon.png}` + `dist/NightHeist.zip` |
| `make assets` | Regenerates generated sources and assets (font table, HBC icon; needs Python 3 + Pillow) |
| `make clean` | Removes `build/` and `dist/` |

### Wii toolchain (devkitPro)

Follow the current instructions at <https://devkitpro.org/wiki/Getting_Started>
to install `dkp-pacman`, then install the Wii group:

```sh
sudo dkp-pacman -S wii-dev      # devkitPPC, libogc (incl. wiiuse, ASND), libfat-ogc, tools
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
make wii
```

If you don't want a local install, `make wii-docker` uses the
`devkitpro/devkitppc` image. Phase 0 was built with devkitPPC r51, GCC 16.2,
libogc 3.2.1 and libfat-ogc 2.1.0.

### Desktop

Debian/Ubuntu: `sudo apt install build-essential libsdl2-dev libgl-dev`, then
`make desktop test`.

Useful flags: `--wide` (emulate a 16:9 Wii), `--novsync`, `--autotest`
(scripted input), `--frames N --screenshot out.bmp` (render N frames, save the
640x480 image and quit; used for automated smoke tests).

## Running

**Dolphin:** boot `build/wii/nightheist.elf` (or `dist/apps/NightHeist/boot.dol`).
To test SD access, enable the virtual SD card with folder sync
(*Config → Wii → SD Card*), copy `dist/apps` into the sync folder, and boot
`apps/NightHeist/boot.dol` from there. Set *Controllers → Wii Remote 1* to
*Emulated* with the *Nunchuk* extension.

**Wii:** copy `dist/apps/NightHeist/` to `SD:/apps/NightHeist/` (or USB) and
launch it from the Homebrew Channel.

## Controls (Phase 0 test scene)

| Wii | Desktop | Action |
|---|---|---|
| Pointer | Mouse | Aim the torch (it holds its direction when the pointer leaves the screen) |
| Nunchuk stick | WASD | Move |
| Nunchuk tilt | Q / E | Lean |
| Z (hold) | Ctrl | Crouch |
| B | Right mouse | Torch on/off |
| A | Left mouse | Click sound (panned to the pointer) + rumble pulse |
| 1 / 2 | 1 / 2 | Stress test: fewer / more tori (1, 9, 25, 36, 49) |
| C | F | Cycle spin speed |
| + | Enter | Pause spin |
| HOME | Esc | Quit to the Homebrew Channel |

## Repository layout

```
src/core/       portable, deterministic, platform-free code (maths, RNG, lighting model, texture codec, ...)
src/game/       game code; talks to the machine only through src/platform/*.h
src/platform/   platform.h + gfx.h interfaces, wii/ and desktop/ backends
tests/          unit tests (make test)
tools/          generators and offline tools
apps/NightHeist meta.xml + icon.png for the Homebrew Channel
```
