#ifndef NH_HUD_H
#define NH_HUD_H

#include "heist.h"

void hud_draw_heist(const Heist *h, const GameSettings *gs, const NhMat34 *view);
void hud_draw_debug(const Heist *h, float fps);
void hud_draw_map(const Heist *h);
void hud_draw_objectives(const Heist *h);
void hud_draw_summary(const Heist *h);
/* The summary or pause screen over the whole screen (both views in co-op). */
void hud_draw_overlay(const Heist *h, const GameSettings *gs);

#endif
