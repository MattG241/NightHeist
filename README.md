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
- **Local co-op.** Two players in split screen with two Wii Remotes and
  Nunchuks, in the same campaign.

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

Two control schemes, set in the safehouse under SETTINGS → Controls:

- **Simple** (the default): the stick walks and turns.
- **Pointer**: the classic Wii shooter style, where pointing past the edge
  of the screen turns you.

| Wii | Desktop | Action |
|---|---|---|
| Nunchuk stick up/down | W / S | Walk forward and back |
| Nunchuk stick left/right | A / D | **Simple:** turn smoothly. **Pointer:** step sideways |
| Pointer | Mouse | **Simple:** not needed; you aim where you face. **Pointer:** aim, and turn or look at the screen edges |
| Z | Ctrl | Sneak: quieter, harder to see |
| D-pad down (hold) | ↓ | Run. It's loud, and only happens when you ask for it |
| C | F | Use, open, take (hold for timed actions) |
| A | Left mouse | Use the gadget; take a guard down from behind; select in menus |
| B | Right mouse | Torch on or off; back in menus |
| D-pad left / right | ← / → | Choose a gadget |
| D-pad up | ↑ | Thermal goggles |
| + / − | Enter / Tab | Map / objectives |
| Stick left/right (Simple) or twist the remote (Pointer) | A / D, or [ and ] | Lockpick and safe dial. In **Simple** the stick also steers the hacking signal |
| Mash A when grabbed | Left mouse | Break free from a guard's grip (2 escapes per job on normal) |
| HOME | Esc | Pause on a job; back to the title from the safehouse |

How a job goes:

- Every campaign job starts with a **briefing** card: the goal, the bonus
  objectives and how to play.
- During the job, **GO:** at the top left always says what to do next and
  how far away it is.
- A **yellow marker** follows the walking route to it: the target first,
  then back to the van. When it's off screen, an arrow at the edge (or
  "TURN AROUND") points the way.
- The first time each new thing comes up, a **tip** explains it: guards,
  locks, cameras, light, hiding.
- If a guard grabs you, **mash A** to break free. You get 3 escapes on easy,
  2 on normal and 1 on hard; after that, a grab means you're caught.
- Too dark? Turn up **SETTINGS → Brightness**. Changing it doesn't make you
  easier to spot.

On a job:

- Stay out of the light.
- Sneak near guards.
- Keep your torch off when someone could see the beam.
- Watch the detection markers above guards' heads.

## Co-op

1. Connect a second Wii Remote with a Nunchuk.
2. In the safehouse, pick a job.
3. On the plan screen, press left or right on the D-pad to choose
   **2 PLAYERS**, then GO.

How it plays:

- The screen splits top and bottom: player 1 on top, player 2 below.
- Each remote points across the whole TV, and its pointer drives its own
  view.
- You share the kit, the gadget charges, the keycards and codes, and one
  bigger bag (×1.5).
- Each player has their own torch, thermal goggles, gadget choice and
  hands.
- Guards and cameras watch whichever of you they can see best, and hear
  both of you.
- If a guard catches one of you, that player is cuffed where they stand
  and the alarm goes up.
  - The other player has 90 seconds to reach them and hold C to free them.
  - The van won't leave without a cuffed partner.
  - If both of you are caught, or time runs out, you're busted.
- Either player's HOME pauses the game for both.
- Each player has their own map and objectives (+ and −), and the job
  carries on while you read them.

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
| `--coop` | Make the `--job` building a two-player split-screen job |
| `--wide` | Emulate a 16:9 Wii |
| `--novsync` | Don't wait for vertical sync |

On a Wii or in Dolphin, `apps/NightHeist/debug_start.txt` does the same:

- a line `job TYPE,SIZE,SEED,SECURITY` picks a building;
- a line `x,y,z,yaw,pitch` sets the start;
- a line `overlay` turns the debug overlay on;
- a line `coop` makes that job two-player.

On desktop, player 2 uses a gamepad:

| Gamepad | Action |
|---|---|
| Left stick | Move |
| Right stick | Pointer |
| A / B | A / B |
| X | C |
| Left shoulder | Z |
| Triggers | Twist the remote |
| D-pad, Start, Back, Guide | D-pad, +, −, HOME |

In scripts, prefix a command with `p2`, as in `120 p2 stick 0 1`.

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
