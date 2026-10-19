#include "nh_font.h"

#include <string.h>

void nh_font_build_atlas(uint8_t *rgba)
{
    memset(rgba, 0, NH_FONT_ATLAS_W * NH_FONT_ATLAS_H * 4);
    for (int g = 0; g < NH_FONT_GLYPHS; g++) {
        int cx = (g % NH_FONT_COLS) * NH_FONT_CELL_W + 1;
        int cy = (g / NH_FONT_COLS) * NH_FONT_CELL_H + 1;
        for (int y = 0; y < NH_FONT_GLYPH_H; y++) {
            for (int x = 0; x < NH_FONT_GLYPH_W; x++) {
                uint8_t *p = rgba + ((size_t)(cy + y) * NH_FONT_ATLAS_W + (size_t)(cx + x)) * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = (nh_font5x7[g][y] & (1u << (4 - x))) ? 255 : 0;
            }
        }
    }
}

void nh_font_glyph_uv(int code, float *u0, float *v0, float *u1, float *v1)
{
    if (code < NH_FONT_FIRST || code >= NH_FONT_FIRST + NH_FONT_GLYPHS)
        code = '?';
    int g = code - NH_FONT_FIRST;
    float x = (float)((g % NH_FONT_COLS) * NH_FONT_CELL_W + 1);
    float y = (float)((g / NH_FONT_COLS) * NH_FONT_CELL_H + 1);
    *u0 = x / NH_FONT_ATLAS_W;
    *v0 = y / NH_FONT_ATLAS_H;
    *u1 = (x + NH_FONT_GLYPH_W) / NH_FONT_ATLAS_W;
    *v1 = (y + NH_FONT_GLYPH_H) / NH_FONT_ATLAS_H;
}
