# Night Heist

A first-person stealth heist game for the Nintendo Wii, run as homebrew from the
Homebrew Channel.

You run a small crew. Between jobs you are at the safehouse: pick a contract
from the board, choose who comes along, buy gear and fence the take. On a job
you work through a procedurally generated warehouse, office, bank, mansion or
museum at night. Pick locks, crack safes, hack terminals, cut the power,
dodge cameras and lasers, and get out before anyone knows you were there.

The game:

- **4 districts.** The Docklands, Midtown, The Old Quarter and The Heights,
  each with a finale. About 8 hours to the post-game, then "Legend"
  contracts.
- **5 building types in 3 sizes.** Every building is generated from a seed,
  with 44 authored set pieces mixed in. 10,000 seeds are tested for
  solvability.
- **11 guard types.** Rookies, veterans, lazy guards, supervisors with
  radio check-ins, camera operators, elites with thermal goggles, janitors,
  dog teams and police.
- **14 gadgets.** Coins, noisemakers, smoke, taser, dart gun, camera
  jammer, EMP, glass cutter, card cloner, thermal goggles, laser mirror,
  safe drill, phone dialer and lockpicks.
- **3 minigames** that use the Wii Remote: lockpicking and safecracking by
  twisting the remote, and hacking with the pointer.
- **Crew.** 6 specialists with perks, flaws and loyalty jobs.
- **Progression.** 8 upgrade lines, loot sets for the fence, heat, and
  reputation.
- **Audio.** Every sound and all the music is synthesised at boot. The music
  layers up as guards get suspicious.

The code is portable C11 with two backends:

- **Wii**: GX / libogc. This is the real target.
- **Desktop**: SDL2 and OpenGL 3.3, used for fast iteration, debug tools and
  tests. It deliberately emulates the Wii's limits: per-vertex GX lighting,
  TEV-style combining, GX texture formats and a 640x480 framebuffer.

See [PROGRESS.md](PROGRESS.md) for status, [DESIGN.md](DESIGN.md) for the
design, [PACING.md](PACING.md) for the campaign curve and
[ASSETS.md](ASSETS.md) for every asset and its licence.

## Installing on a Wii

1. Get the game:
   - download the `NightHeist-apps` artifact from a CI run, or
   - build `dist/NightHeist.zip` with `make wii package` (or
     `make wii-docker package`).
2. Unzip it to the root of an SD card or USB drive. You should end up with
   `SD:/apps/NightHeist/boot.dol`, `meta.xml`, `icon.png` and `data/`.
3. Launch **Night Heist** from the Homebrew Channel.
4. Connect a Nunchuk. The game saves next to `boot.dol`, in `save.dat`, and
   keeps the previous good save as `save.bak`.

**USB drives.**
- Format the drive FAT32 with an MBR partition table. The Wii's FAT
  driver can't read exFAT or NTFS, and most new sticks ship as exFAT.
- Copy the `apps` folder to the root of the drive, then plug the drive into
  the back USB port nearest the edge (port 0).
- The Homebrew Channel lists apps from SD and USB. Night Heist runs from
  whichever one it was launched from, and keeps its saves there.
- A slow drive gets a few seconds to spin up.
- Saves are flushed to the device as they're written, so switching off at
  the wall won't lose them.

**Dolphin:**

1. Enable the virtual SD card with folder sync (*Config → Wii → SD Card*).
2. Copy `dist/apps` into the sync folder.
3. Boot `apps/NightHeist/boot.dol`.
4. Set *Controllers → Wii Remote 1* to *Emulated*, with the *Nunchuk*
   extension.

## Controls

| Wii | Desktop | Action |
|---|---|---|
| Pointer | Mouse | Look and aim; turn by pointing at the screen edge |
| Nunchuk stick | WASD | Move |
| Z | Ctrl | Crouch: quieter and harder to see |
| Tilt the Nunchuk | Q / E | Lean round corners |
| C | F | Use, open, take (hold for timed actions) |
| A | Left mouse | Use the gadget; take a guard down from behind; select in menus |
| B | Right mouse | Torch on or off; back in menus |
| D-pad left / right | ← / → | Choose a gadget |
| D-pad up | ↑ | Thermal goggles |
| + / − | Enter / Tab | Map / objectives |
| Twist the remote | [ and ] (\ levels it) | Lockpick and safe dial |
| HOME | Esc | Pause on a job; back to the title from the safehouse |

On a job:

- Stay out of the light.
- Crouch near guards.
- Keep your torch off when someone could see the beam.
- Watch the detection markers above guards' heads.

## Building

| Target | What it does |
|---|---|
| `make desktop` | Desktop build at `build/desktop/nightheist` (needs SDL2 dev + OpenGL) |
| `make test` | Builds and runs the unit tests (host compiler only, no SDL) |
| `make wii` | `build/wii/nightheist.dol` (needs devkitPPC, see below) |
| `make wii-docker` | Same as `make wii`, inside the official `devkitpro/devkitppc` Docker image |
| `make package` | `dist/apps/NightHeist/{boot.dol,meta.xml,icon.png,data/}` + `dist/NightHeist.zip` |
| `make assets` | Regenerates generated sources and art (needs Python 3 + Pillow) |
| `make seedmap` | `build/seedmap`: renders floor plans of generated buildings to PNG |
| `make audiodump` | `build/audiodump DIR`: writes every sound effect and music stem as WAV |
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
`devkitpro/devkitppc` image.

### Desktop

Debian/Ubuntu: `sudo apt install build-essential libsdl2-dev libgl-dev`, then
`make desktop test`.

Run it with `build/desktop/nightheist --data apps/NightHeist`. Testing
options:

| Option | Effect |
|---|---|
| `--job TYPE,SIZE,SEED[,SECURITY]` | Go straight into a generated building. TYPE: 0 warehouse, 1 office, 2 bank, 3 mansion, 4 museum. SIZE: 0–2. |
| `--test-level` | The hand-built test building |
| `--start x,y,z,yaw,pitch` | Start the player there |
| `--script FILE` | Scripted input, one `FRAME command` per line: `ir X Y`, `noir`, `stick X Y`, `press BTN`, `hold BTN`, `release BTN`, `twist DEG`, `shot NAME`, `quit` |
| `--screenshot PREFIX` | Where script shots are saved |
| `--overlay` | Start with the debug overlay on |
| `--wide` | Emulate a 16:9 Wii |
| `--novsync` | Don't wait for vertical sync |

On a Wii or in Dolphin, `apps/NightHeist/debug_start.txt` does the same:

- a line `job TYPE,SIZE,SEED,SECURITY` picks a building;
- a line `x,y,z,yaw,pitch` sets the start;
- a line `overlay` turns the debug overlay on.

## Repository layout

```
src/core/       portable, deterministic, platform-free code: maths, RNG, lighting model, texture codec,
                level model, generator, AI, navigation, jobs, security, minigames, campaign, saves, synth
src/game/       game code (title, safehouse, heist, HUD, audio); talks to the machine only through src/platform/*.h
src/platform/   platform.h + gfx.h interfaces, wii/ and desktop/ backends
tests/          unit tests (make test)
tools/          generators and offline tools (art, textures, seedmap, audiodump)
apps/NightHeist meta.xml, icon.png and data/ for the Homebrew Channel
```

## Licence

All of the game's own art, models, sounds and music are original and CC0. The
surface textures are CC0 from Poly Haven. See [ASSETS.md](ASSETS.md).
