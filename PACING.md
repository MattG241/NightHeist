# Pacing: Novelty Cadence

Rule: something meaningfully new (a gadget, guard type, security system,
building type, district or job modifier) appears at least every **30–45
minutes** of first-playthrough time. The campaign target is **≥ 6 hours**.

## Where the numbers come from

`test_economy` (in `tests/test_campaign.c`) plays the whole campaign with a
simulated player. Each job is a real generated building from the real job
board, and the player makes plausible choices: the finale when offered,
otherwise the best-paying job, the cheapest affordable upgrade, hiring crew
and lying low when heat climbs. Job length is modelled as
`5 + 4 × size + 2 × security` minutes, which gives 9–25 minutes per heist.

The test fails the build if any of these break:

- the post-game is reached in 20–60 jobs and in 5–10 hours;
- the four districts open in order, with Midtown opening between jobs 4 and 14;
- money never goes negative, and the player is never broke for more than 2 jobs running;
- at least 60% of all upgrade levels are bought by the end;
- no stretch longer than 45 minutes passes without something new;
- the unlock schedule never skips two career steps in a row.

To print the full timeline, run `NH_PACING=1 make test`.

The **Measured** column is for real play. Set *Playtest log (SD)* in the
safehouse settings, and `playlog.csv` records every job with its time, grade
and payout. Until enough logs exist, the simulated minutes stand in.

## What arrives when

Gadgets and job modifiers unlock by **career step** (jobs finished or
failed), defined in `nh_campaign.c`:

- `k_gadget_at` and `k_mod_at` hold the step for each gadget and modifier.
- The payday screen announces each new arrival ("New gadget: Smoke").
- Upgrades for a gadget that hasn't arrived yet show "later".

Districts open on reputation **and** the previous district's finale. Each
district brings its own building types and a base security level.

Security level and building type set the following:

- **Cameras.** Every building has them; the count is `2 + 2 × security + 2 × size`.
- **Security 2 and up.** Alarmed exterior doors, and keycards on semi-secure rooms.
- **Security 3 and up.** Harder locks, powered exhibit cases, and elite guards (also in any large building).
- **Banks and museums.** Their set pieces bring laser grids (vaults, cash cages, jewel exhibits, the Diamond).
- **Rosters by building type:**
  - offices, banks and museums get camera operators;
  - offices, banks and big warehouses get supervisors;
  - warehouses and mansions get dog teams;
  - offices and museums get janitors.

### Simulated timeline (seed 2024)

| Job | Min | District | Job taken | What's new | Measured min |
|---|---|---|---|---|---|
| 1 | 0 | Docklands | medium office | Start kit: lockpicks, coins, taser; rookies, guards, lazy guards, janitors; *night shift* and *skeleton crew* modifiers | |
| 2 | 9 | Docklands | small office | Noisemaker | |
| 3 | 14 | Docklands | medium warehouse | *Cleaners in* (lights on) | |
| 4 | 25 | Docklands | medium office | Smoke | |
| 5 | 38 | Docklands | large office | *Blackout* | |
| 6 | 53 | Docklands | large office | Jammer | |
| 7 | 68 | Docklands | **finale**, large warehouse | *Storm*; finale contract (double pay, bonus target) | |
| 8 | 87 | Midtown | **loyalty job**, medium office | **Midtown**: banks, security 1, more cameras, supervisor radio check-ins; dart gun | |
| 9 | 98 | Docklands | large office | *Inside man* | |
| 10 | 113 | Midtown | medium bank | First bank: vault doors, safecracking, laser grids | |
| 11 | 124 | Docklands | large office | Card cloner | |
| 12 | 139 | Midtown | large bank | *Bonus target* | |
| 13 | 154 | Midtown | large bank | EMP | |
| 15 | 190 | Midtown | medium office | *Fast response* | |
| 16 | 203 | Midtown | large office | Phone dialer | |
| 17 | 220 | Midtown | **finale**, large bank | *Clean job* (no takedowns) | |
| 19 | 258 | Old Quarter | medium mansion | **Old Quarter**: museums and mansions, security 2, alarmed doors, keycards, dog teams; laser mirror | |
| 20 | 271 | Old Quarter | medium museum | First museum: exhibits, display cases, the Diamond | |
| 21 | 284 | Old Quarter | large museum | *Dog patrol* | |
| 22 | 301 | Old Quarter | medium museum | Safe drill | |
| 24 | 329 | Old Quarter | **finale**, large museum | Glass cutter | |
| 26 | 371 | Heights | medium bank | **Heights**: security 3, harder locks, powered cases, elite guards; *lockdown* | |
| 28 | 405 | Heights | medium mansion | Thermal goggles (and the *thermal cells* upgrade) | |
| 30 | 437 | Heights | medium museum | *Thermal team* | |
| 33 | 492 | Heights | **finale**, large bank | Post-game: Legend contracts (+1 security, +1 modifier) | |

Jobs 14, 18, 23, 25, 27, 29, 31 and 32 bring nothing from the schedule. They
are where the player uses what they have just unlocked. Two jobs never pass
without a new element.

Result for this seed:

- post-game after **33 jobs** and about **8.6 hours**;
- districts opened at jobs 7, 18 and 24;
- longest stretch without something new: **38 minutes**.

## Woven through all of it

- **Crew.** Six specialists, each with a perk and a flaw. Hiring opens with
  reputation. A loyalty job appears once a member reaches loyalty 60, and
  finishing it unlocks their second perk. The first loyalty job appeared at
  job 8 in the simulation.
- **Loot sets.** Five collections the fence pays a bonus for. These make
  small side targets worth taking.
- **Upgrades.** Eight lines with 16 levels in total. In the simulation all 16
  were bought by job 30.
- **Set pieces.** 44 authored room arrangements. The same set piece never
  repeats within 3 jobs, and the same layout never repeats within the last
  24 jobs.
- **Heat.** Every 34 heat in a district raises its security by one level.
  Lying low cools it down, and is the main money sink.
