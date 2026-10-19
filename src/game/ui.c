#include "ui.h"

#include "audio.h"
#include "text.h"

static const NhColor FG = { 230, 230, 230, 255 };
static const NhColor DIM = { 120, 120, 130, 255 };
static const NhColor HI = { 255, 200, 90, 255 };

void ui_centered(float y, float scale, NhColor c, const char *s) { text_draw(320 - text_width(s, scale) * 0.5f, y, scale, c, s); }

static bool row_enabled(const UiMenu *m, int i) { return !m->enabled || m->enabled[i]; }

int ui_menu_tick(const UiMenu *m, int *sel, const NhInput *in)
{
    int before = *sel;
    int dir = (in->pressed & NH_BTN_DOWN) ? 1 : (in->pressed & NH_BTN_UP) ? -1 : 0;
    if (dir)
        for (int k = 1; k <= m->n; k++) {
            int s = (*sel + dir * k + m->n * 2) % m->n;
            if (row_enabled(m, s)) {
                *sel = s;
                break;
            }
        }
    if (in->ir_valid && in->ir_x >= m->x && in->ir_x < m->x + m->w) {
        int r = (int)((in->ir_y - m->y) / m->row_h);
        if (in->ir_y >= m->y && r >= 0 && r < m->n && row_enabled(m, r))
            *sel = r;
    }
    if (*sel != before)
        audio_play(SFX_UI_MOVE, 0.35f, 0.0f, 1.0f);
    if (!(in->pressed & NH_BTN_A))
        return -1;
    if (!row_enabled(m, *sel)) {
        audio_play(SFX_UI_DENY, 0.5f, 0.0f, 1.0f);
        return -1;
    }
    audio_play(SFX_UI_SELECT, 0.5f, 0.0f, 1.0f);
    return *sel;
}

void ui_menu_draw(const UiMenu *m, int sel)
{
    for (int i = 0; i < m->n; i++) {
        float y = m->y + (float)i * m->row_h;
        bool on = row_enabled(m, i);
        if (i == sel) {
            text_rect(m->x, y, m->w, m->row_h - 4, nh_rgba(60, 50, 30, 210));
            text_rect(m->x, y, 4, m->row_h - 4, HI);
        }
        float th = 7.0f * m->scale;
        text_draw(m->x + 16, y + (m->row_h - 4 - th) * 0.5f, m->scale, !on ? DIM : i == sel ? HI : FG, m->items[i]);
    }
}

void ui_draw_controls(float y)
{
    static const char *const rows[][2] = {
        { "Nunchuk stick", "up/down: walk   left/right: turn (Simple)" },
        { "Pointer", "aim the torch; look up or down at the screen edge" },
        { "Z", "sneak (quieter, harder to see)" },
        { "D-pad down (hold)", "run (loud!)" },
        { "C", "use, open, take (hold for timed actions)" },
        { "A", "use the gadget, or take down from behind" },
        { "B", "torch on and off; back in menus" },
        { "D-pad left / right", "choose a gadget" },
        { "D-pad up", "thermal goggles" },
        { "+  /  -", "map  /  objectives" },
        { "HOME", "pause" },
    };
    text_rect(40, y - 10, 560, 22.0f * 11 + 70, nh_rgba(8, 10, 16, 230));
    ui_centered(y, 2, HI, "CONTROLS");
    for (int i = 0; i < (int)NH_ARRAY_LEN(rows); i++) {
        float ry = y + 28 + (float)i * 22;
        text_draw(60, ry, 2, HI, rows[i][0]);
        text_draw(316, ry + 3, 1, FG, rows[i][1]);
    }
    float ty = y + 28 + 22.0f * 11 + 4;
    ui_centered(ty, 1, DIM, "Follow the yellow marker; GO (top left) says what to do next.");
    ui_centered(ty + 12, 1, DIM, "Pointer turning instead: SETTINGS > Controls in the safehouse.");
}
