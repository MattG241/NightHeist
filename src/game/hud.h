#ifndef NH_HUD_H
#define NH_HUD_H

#include "heist.h"

void hud_draw_heist(const Heist *h, const GameSettings *gs);
void hud_draw_debug(const Heist *h, float fps);

#endif
