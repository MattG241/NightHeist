/* Night Heist - 2D text and rectangles on top of gfx_draw_quad2d. */
#ifndef NH_TEXT_H
#define NH_TEXT_H

#include "platform/gfx.h"

bool text_init(NhArena *scratch); /* builds and uploads the font atlas */
void text_draw(float x, float y, float scale, NhColor color, const char *str);
void text_printf(float x, float y, float scale, NhColor color, const char *fmt, ...);
void text_rect(float x, float y, float w, float h, NhColor color);
float text_width(const char *str, float scale);
/* Stretch small text (scale <= 1.5) vertically by k; 2 in a half-height co-op view. */
void text_set_tall(float k);

#endif
