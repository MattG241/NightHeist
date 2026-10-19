/* Night Heist - procedural RGBA8 textures for test scenes (deterministic). */
#ifndef NH_TEXGEN_H
#define NH_TEXGEN_H

#include "nh_types.h"

/* Red-brown bricks with mortar lines and per-brick tint variation. */
void nh_texgen_bricks(uint8_t *rgba, int w, int h, uint32_t seed);
/* Grey concrete with large tiles and speckle. */
void nh_texgen_concrete(uint8_t *rgba, int w, int h, uint32_t seed);

#endif
