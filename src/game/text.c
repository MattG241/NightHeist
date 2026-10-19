#include "text.h"

#include <stdarg.h>
#include <stdio.h>

#include "core/nh_font.h"

static GfxTexture s_font;
static float s_solid_u, s_solid_v; /* centre of the solid glyph cell */

bool text_init(NhArena *scratch)
{
    size_t mark = nh_arena_mark(scratch);
    uint8_t *rgba = nh_arena_alloc(scratch, NH_FONT_ATLAS_W * NH_FONT_ATLAS_H * 4);
    uint8_t *tiled = nh_arena_alloc(scratch, nh_tex_level_size(NH_TF_IA8, NH_FONT_ATLAS_W, NH_FONT_ATLAS_H));
    if (!rgba || !tiled) {
        nh_arena_reset_to(scratch, mark);
        return false;
    }
    nh_font_build_atlas(rgba);
    nh_tex_encode(NH_TF_IA8, rgba, NH_FONT_ATLAS_W, NH_FONT_ATLAS_H, tiled);

    GfxTextureDesc desc = { NH_TF_IA8, NH_FONT_ATLAS_W, NH_FONT_ATLAS_H, 1, false, false, tiled };
    s_font = gfx_texture_create(&desc);
    nh_arena_reset_to(scratch, mark);

    float u0, v0, u1, v1;
    nh_font_glyph_uv(NH_FONT_SOLID, &u0, &v0, &u1, &v1);
    s_solid_u = (u0 + u1) * 0.5f;
    s_solid_v = (v0 + v1) * 0.5f;
    return s_font != 0;
}

static float s_tall = 1.0f;

void text_set_tall(float k) { s_tall = k; }

void text_draw(float x, float y, float scale, NhColor color, const char *str)
{
    float start_x = x;
    /* Small text keeps its height when the HUD is squashed into half the
     * screen (co-op); larger text stays squat but readable. */
    float tall = scale <= 1.5f ? s_tall : 1.0f;
    for (const char *c = str; *c; c++) {
        if (*c == '\n') {
            x = start_x;
            y += (NH_FONT_GLYPH_H + 3) * scale;
            continue;
        }
        if (*c != ' ') {
            float u0, v0, u1, v1;
            nh_font_glyph_uv((unsigned char)*c, &u0, &v0, &u1, &v1);
            gfx_draw_quad2d(s_font, x, y - NH_FONT_GLYPH_H * scale * (tall - 1.0f) * 0.5f, NH_FONT_GLYPH_W * scale,
                            NH_FONT_GLYPH_H * scale * tall, u0, v0, u1, v1, color);
        }
        x += NH_FONT_ADVANCE * scale;
    }
}

void text_printf(float x, float y, float scale, NhColor color, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    text_draw(x, y, scale, color, buf);
}

void text_rect(float x, float y, float w, float h, NhColor color)
{
    gfx_draw_quad2d(s_font, x, y, w, h, s_solid_u, s_solid_v, s_solid_u, s_solid_v, color);
}

float text_width(const char *str, float scale)
{
    int n = 0;
    while (str[n] && str[n] != '\n')
        n++;
    return (float)(n * NH_FONT_ADVANCE) * scale;
}
