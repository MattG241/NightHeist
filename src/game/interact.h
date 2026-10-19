/* Night Heist - what the player can do with C: doors and locks, loot and
 * keys, safes, terminals, fuse boxes, the alarm panel, hiding, bodies, the
 * ladder and roof hatch, vents and the getaway. Holds and minigames too. */
#ifndef NH_INTERACT_H
#define NH_INTERACT_H

#include "heist.h"

/* Work out what C would do (fills h->act). */
void interact_find(Heist *h, const GameSettings *gs);
/* Handle C (press/hold), the running minigame and the hiding view. */
void interact_update(Heist *h, const NhInput *in, const GameSettings *gs);
/* Player can't move (minigame, hiding). */
bool interact_busy(const Heist *h);
/* Leave a hiding spot (also when a guard pulls you out). */
void interact_unhide(Heist *h);
/* Drop a carried body or heavy loot, leave a minigame and any hiding spot. */
void interact_drop_all(Heist *h);
/* Keep a carried body with the player. */
void interact_carry_tick(Heist *h);
/* Short name of a prop type for prompts. */
const char *interact_prop_name(int type);

#endif
