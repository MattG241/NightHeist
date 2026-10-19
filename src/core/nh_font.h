/* Night Heist - built-in 5x7 bitmap font and atlas builder. */
#ifndef NH_FONT_H
#define NH_FONT_H

#include "nh_types.h"

#define NH_FONT_FIRST 32
#define NH_FONT_GLYPHS 96   /* 32..127; 127 is a solid block */
#define NH_FONT_SOLID 127
#define NH_FONT_GLYPH_W 5
#define NH_FONT_GLYPH_H 7
#define NH_FONT_CELL_W 8    /* atlas cell, glyph sits at (1,1) with a clear border */
#define NH_FONT_CELL_H 8
#define NH_FONT_COLS 16
#define NH_FONT_ATLAS_W 128
#define NH_FONT_ATLAS_H 64
#define NH_FONT_ADVANCE 6   /* glyph width + 1 px spacing */

extern const uint8_t nh_font5x7[NH_FONT_GLYPHS][7];

/* Render the font into an RGBA8 atlas (white, alpha = coverage). */
void nh_font_build_atlas(uint8_t *rgba /* NH_FONT_ATLAS_W*NH_FONT_ATLAS_H*4 */);
/* Atlas UVs of a glyph's 5x7 box (non-printable codes map to '?'). */
void nh_font_glyph_uv(int code, float *u0, float *v0, float *u1, float *v1);

#endif
