#include "title.h"

#include <math.h>
#include <stdio.h>

#include "audio.h"
#include "core/nh_rng.h"
#include "platform/gfx.h"
#include "res.h"
#include "text.h"
#include "ui.h"

static const NhColor FG = { 230, 230, 230, 255 };
static const NhColor DIM = { 130, 130, 145, 255 };
static const NhColor HI = { 255, 200, 90, 255 };

static const char *const k_items[] = { "CONTINUE", "NEW CAREER", "CONTROLS", "QUIT" };

static UiMenu menu(const Title *t)
{
    static bool enabled[4];
    for (int i = 0; i < 4; i++)
        enabled[i] = i != 0 || t->has_save;
    return (UiMenu){ 40, 300, 250, 34, 3, 4, k_items, enabled };
}

void title_init(Title *t, bool has_save)
{
    *t = (Title){ 0 };
    t->has_save = has_save;
    t->sel = has_save ? 0 : 1;
}

TitleAction title_tick(Title *t, const NhInput *in)
{
    t->tick++;
    t->cursor_valid = in->ir_valid;
    t->cursor_x = in->ir_x;
    t->cursor_y = in->ir_y;
    if (t->page == 1) {
        if (in->pressed & (NH_BTN_A | NH_BTN_B)) {
            audio_play(SFX_UI_BACK, 0.5f, 0.0f, 1.0f);
            t->page = 0;
        }
        return TA_NONE;
    }
    if (t->page == 2) {
        if (in->pressed & NH_BTN_A) {
            t->page = 0;
            audio_play(SFX_UI_SELECT, 0.5f, 0.0f, 1.0f);
            return TA_NEW;
        }
        if (in->pressed & NH_BTN_B) {
            audio_play(SFX_UI_BACK, 0.5f, 0.0f, 1.0f);
            t->page = 0;
        }
        return TA_NONE;
    }
    if (in->pressed & NH_BTN_HOME)
        return TA_QUIT;
    UiMenu m = menu(t);
    switch (ui_menu_tick(&m, &t->sel, in)) {
    case 0: return TA_CONTINUE;
    case 1:
        if (!t->has_save)
            return TA_NEW;
        t->page = 2;
        return TA_NONE;
    case 2: t->page = 1; return TA_NONE;
    case 3: return TA_QUIT;
    default: return TA_NONE;
    }
}

/* ---- the skyline ------------------------------------------------------------------- */

static void skyline(uint32_t tick)
{
    /* Sky: a few bands from deep blue to the city's orange glow. */
    static const NhColor sky[] = { { 6, 8, 20, 255 },   { 9, 12, 28, 255 },  { 14, 16, 34, 255 },
                                   { 22, 20, 40, 255 }, { 36, 26, 44, 255 }, { 52, 34, 42, 255 } };
    for (int i = 0; i < 6; i++)
        text_rect(0, (float)i * 70, 640, 70, sky[i]);
    GfxTexture glow = res_texture("glow");
    /* Moon */
    gfx_draw_quad2d(glow, 430, 10, 180, 180, 0, 0, 1, 1, nh_rgba(120, 130, 170, 90));
    gfx_draw_quad2d(glow, 490, 70, 60, 60, 0, 0, 1, 1, nh_rgba(235, 235, 220, 255));
    /* A searchlight sweeping the clouds. */
    float sx = 320.0f + 260.0f * sinf((float)tick / 240.0f);
    gfx_draw_quad2d(glow, sx - 120, 60, 240, 120, 0, 0, 1, 1, nh_rgba(160, 170, 200, 40));
    /* Far towers, then near ones with lit windows. */
    NhRng r;
    for (int layer = 0; layer < 2; layer++) {
        nh_rng_seed(&r, 1234u + (uint64_t)layer, 9);
        float x = -10.0f;
        NhColor body = layer ? nh_rgba(10, 11, 18, 255) : nh_rgba(24, 24, 38, 255);
        while (x < 650.0f) {
            float w = 34.0f + 50.0f * nh_rng_float(&r);
            float hgt = (layer ? 90.0f : 140.0f) + (layer ? 190.0f : 160.0f) * nh_rng_float(&r);
            float top = 420.0f - hgt;
            text_rect(x, top, w, hgt, body);
            if (layer && nh_rng_float(&r) < 0.3f) /* a mast with a red light */
                text_rect(x + w * 0.5f - 1, top - 18, 2, 18, body);
            for (float wy = top + 8; wy < 410.0f; wy += 12.0f)
                for (float wx = x + 6; wx < x + w - 8; wx += 10.0f) {
                    float lit = nh_rng_float(&r);
                    uint32_t blink = (uint32_t)(nh_rng_float(&r) * 4000.0f);
                    if (lit < (layer ? 0.14f : 0.08f) && ((tick + blink) / 600u) % 7u != 0u)
                        text_rect(wx, wy, 5, 6, layer ? nh_rgba(255, 196, 110, 220) : nh_rgba(170, 150, 120, 120));
                }
            if (layer && nh_rng_float(&r) < 0.3f && (tick / 40u) % 2u)
                text_rect(x + w * 0.5f - 2, top - 21, 4, 4, nh_rgba(255, 50, 40, 255));
            x += w + (layer ? 4.0f : 2.0f) + 10.0f * nh_rng_float(&r);
        }
    }
    /* Street with lamps */
    text_rect(0, 420, 640, 60, nh_rgba(8, 8, 12, 255));
    for (int i = 0; i < 5; i++) {
        float lx = 60.0f + (float)i * 130.0f;
        text_rect(lx, 380, 3, 40, nh_rgba(20, 20, 26, 255));
        gfx_draw_quad2d(glow, lx - 40, 352, 84, 84, 0, 0, 1, 1, nh_rgba(255, 190, 110, 70));
        gfx_draw_quad2d(glow, lx - 70, 410, 144, 30, 0, 0, 1, 1, nh_rgba(255, 190, 110, 50));
    }
}

static void logo(void)
{
    const char *name = "NIGHT HEIST";
    float s = 7.0f, w = text_width(name, s);
    text_draw(320 - w * 0.5f + 5, 105, s, nh_rgba(0, 0, 0, 200), name);
    text_draw(320 - w * 0.5f, 100, s, HI, name);
    ui_centered(160, 2, nh_rgba(200, 190, 170, 255), "in and out before anyone knows you were there");
}

static void career(const NhCampaign *c)
{
    int newest = 0;
    for (int d = 0; d < DI_COUNT; d++)
        if (camp_district_open(c, d))
            newest = d;
    text_rect(330, 300, 270, 92, nh_rgba(8, 10, 16, 200));
    text_draw(346, 310, 2, HI, c->postgame ? "LEGEND" : "YOUR CREW");
    text_printf(346, 334, 2, FG, "$%d", (int)c->money * 10);
    text_printf(346, 356, 1, DIM, "reputation %d   %d jobs   %d ghosts", (int)c->reputation, c->jobs_done, c->ghosts);
    text_printf(346, 370, 1, DIM, "working %s   %.1f h played", nh_districts[newest].name, (double)c->play_time / 3600.0);
}

void title_render(const Title *t, const NhCampaign *c)
{
    gfx_begin_frame(nh_rgba(0, 0, 0, 255));
    skyline(t->tick);
    if (t->page != 1)
        logo();
    if (t->page == 1) {
        ui_draw_controls(120);
        ui_centered(456, 1, DIM, "A or B: back");
    } else if (t->page == 2) {
        text_rect(110, 280, 420, 110, nh_rgba(8, 10, 16, 235));
        ui_centered(296, 2, HI, "Start a new career?");
        ui_centered(326, 1, FG, "Your saved crew, money and reputation will be lost.");
        ui_centered(356, 2, FG, "A  start over      B  keep it");
    } else {
        UiMenu m = menu(t);
        text_rect(m.x - 8, m.y - 10, m.w + 16, m.row_h * (float)m.n + 16, nh_rgba(8, 10, 16, 200));
        ui_menu_draw(&m, t->sel);
        if (t->has_save && c)
            career(c);
        ui_centered(456, 1, DIM, "Point and press A, or use the D-pad");
    }
    if (t->cursor_valid)
        text_draw(t->cursor_x - 7, t->cursor_y - 7, 2, nh_rgba(255, 255, 255, 230), "+");
    gfx_end_frame();
}
