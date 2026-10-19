#include "hud.h"

#include <stdio.h>

#include "app.h"
#include "core/nh_rooms.h"
#include "res.h"
#include "text.h"

static const NhColor FG = { 230, 230, 230, 255 };
static const NhColor DIM = { 150, 150, 160, 255 };
static const NhColor HI = { 255, 200, 90, 255 };

void hud_draw_heist(const Heist *h, const GameSettings *gs)
{
    const NhPlayer *p = &h->player;
    /* Cursor: a small ring that dims when the pointer is lost. */
    float cx = p->cursor_x, cy = p->cursor_y;
    NhColor cc = nh_rgba(255, 255, 255, 170);
    text_rect(cx - 7, cy - 1, 5, 2, cc);
    text_rect(cx + 3, cy - 1, 5, 2, cc);
    text_rect(cx - 1, cy - 7, 2, 5, cc);
    text_rect(cx - 1, cy + 3, 2, 5, cc);
    /* Turn box edges as faint corner ticks while the pointer is outside it. */
    float tx, ty;
    nh_player_turn_amount(p, &gs->controls, &tx, &ty);
    if (tx != 0.0f || ty != 0.0f) {
        float bw = 320.0f * gs->controls.box_w, bh = 240.0f * gs->controls.box_h;
        NhColor bc = nh_rgba(255, 255, 255, 40);
        text_rect(320 - bw, 240 - bh, 12, 2, bc);
        text_rect(320 + bw - 12, 240 - bh, 12, 2, bc);
        text_rect(320 - bw, 240 + bh, 12, 2, bc);
        text_rect(320 + bw - 12, 240 + bh, 12, 2, bc);
    }
    /* Light meter (bottom left): how visible the player is. */
    float lv = h->light_level;
    text_rect(20, 446, 104, 14, nh_rgba(0, 0, 0, 120));
    NhColor lc = nh_rgba((uint8_t)(90 + 165 * lv), (uint8_t)(90 + 140 * lv), (uint8_t)(70 + 60 * lv), 230);
    text_rect(22, 448, 100 * lv, 10, lc);
    text_draw(22, 430, 2, DIM, p->crouch > 0.5f ? "LIGHT  crouched" : "LIGHT");
    /* Torch + battery (bottom right). */
    text_printf(470, 430, 2, h->torch_on ? HI : DIM, "TORCH %s", h->torch_on ? "ON" : "off");
    text_rect(470, 448, 104, 12, nh_rgba(0, 0, 0, 120));
    text_rect(472, 450, 100 * h->battery, 8, h->battery < 0.2f ? nh_rgba(220, 70, 50, 230) : nh_rgba(120, 200, 120, 230));
    if (h->status_ticks > 0)
        text_draw(320 - text_width(h->status, 2) * 0.5f, 300, 2, HI, h->status);
}

void hud_draw_debug(const Heist *h, float fps)
{
    const World *w = &h->world;
    const NhPlayer *p = &h->player;
    GfxStats st = gfx_last_stats();
    NhMemStats mem;
    plat_mem_stats(&mem);
    int room = nh_room_at_point(&w->L, nh_v3_add(p->pos, nh_v3(0, 1.0f, 0)));
    const char *rname = room >= LV_ROOM_FIRST ? nh_room_styles[w->L.rooms[room].func].name : "outside";
    float x = 10, y = 10, lh = 16;
    text_rect(4, 4, 420, 8 * lh + 10, nh_rgba(0, 0, 0, 160));
    text_printf(x, y, 2, HI, "%5.1f fps  cpu %4.1f gpu %4.1f ms", fps, st.cpu_us / 1000.0f, st.gpu_wait_us / 1000.0f);
    y += lh;
    text_printf(x, y, 2, FG, "tris %6u  draws %3u  rooms %d", (unsigned)st.triangles, (unsigned)st.draw_calls,
                w->nvisible);
    y += lh;
    text_printf(x, y, 2, FG, "geo %.1fM tex %.1fM res %.1fM", st.mesh_bytes / 1048576.0f, st.tex_bytes / 1048576.0f,
                res_bytes() / 1048576.0f);
    y += lh;
    size_t ar[6];
    app_arena_stats(ar);
    text_printf(x, y, 2, FG, "P%.1f/%.0f L%.1f/%.0f S%.1f/%.0f M2 %.1f", ar[0] / 1048576.0f, ar[1] / 1048576.0f,
                ar[2] / 1048576.0f, ar[3] / 1048576.0f, ar[4] / 1048576.0f, ar[5] / 1048576.0f,
                mem.mem2_free / 1048576.0f);
    y += lh;
    text_printf(x, y, 2, FG, "seed %08X  build %u ms", (unsigned)w->L.seed, (unsigned)w->build_ms);
    y += lh;
    text_printf(x, y, 2, FG, "pos %.1f %.1f %.1f  f%d", p->pos.x, p->pos.y, p->pos.z, p->floor);
    y += lh;
    text_printf(x, y, 2, FG, "%s #%d  light %.2f", rname, room, h->light_level);
    y += lh;
    text_printf(x, y, 2, DIM, "1 debug  2 view  B torch  C door");
}
