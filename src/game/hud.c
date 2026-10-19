#include "hud.h"

#include <math.h>
#include <stdio.h>

#include "app.h"
#include "core/nh_rooms.h"
#include "res.h"
#include "text.h"

static const NhColor FG = { 230, 230, 230, 255 };
static const NhColor DIM = { 150, 150, 160, 255 };
static const NhColor HI = { 255, 200, 90, 255 };
#define SUS_HUD_INVESTIGATE 0.65f

static const NhColor RED = { 235, 60, 45, 255 };

/* World point to 640x480 screen coordinates; false if behind the camera. */
static bool project(const NhMat34 *view, NhVec3 p, float *sx, float *sy)
{
    NhVec3 v = nh_m34_mul_point(view, p);
    if (v.z > -0.2f)
        return false;
    float t = tanf(NH_DEG2RAD(HEIST_FOVY * 0.5f));
    *sx = 320.0f + (v.x / -v.z) / (t * gfx_display_aspect()) * 320.0f;
    *sy = 240.0f - (v.y / -v.z) / t * 240.0f;
    return true;
}

static NhColor sus_color(float s, bool alert, uint8_t a)
{
    if (alert)
        return nh_rgba(235, 50, 40, a);
    if (s < SUS_HUD_INVESTIGATE)
        return nh_rgba(235, 235, 220, a);
    return nh_rgba(250, (uint8_t)(200 - 90 * (s - 0.65f) / 0.35f), 40, a);
}

/* Detection: a pip on a ring around the cursor pointing at each guard who is
 * getting suspicious, filling as suspicion grows; "?" or "!" over the head of
 * guards in view. */
static void draw_detection(const Heist *h, const NhMat34 *view)
{
    const AiWorld *ai = &h->ai;
    for (int i = 0; i < ai->n; i++) {
        const AiGuard *g = &ai->g[i];
        if (!g->active || g->state == AS_KO || g->state == AS_CARRIED)
            continue;
        bool alert = g->state == AS_ALERT || g->state == AS_FLEE;
        float s = alert ? 1.0f : g->suspicion;
        bool hunting = g->state == AS_SEARCH || g->state == AS_INVESTIGATE;
        if (s < 0.06f && !hunting)
            continue;
        NhVec3 v = nh_m34_mul_point(view, g->pos);
        float ang = atan2f(v.x, -v.z);
        float r = 118.0f;
        float px = 320.0f + sinf(ang) * r, py = 240.0f - cosf(ang) * r * 0.8f;
        NhColor c = sus_color(s, alert, 230);
        text_rect(px - 8, py - 8, 16, 16, nh_rgba(0, 0, 0, 130));
        float fill = NH_CLAMP(s, 0.1f, 1.0f) * 12.0f;
        text_rect(px - 6, py + 6 - fill, 12, fill, c);
        /* Over the head */
        float sx, sy;
        NhVec3 head = nh_v3(g->pos.x, g->pos.y + (g->arch == ARCH_DOG ? 1.0f : 2.15f), g->pos.z);
        if (project(view, head, &sx, &sy) && sx > 10 && sx < 630 && sy > 10 && sy < 470 && v.z < -1.0f &&
            -v.z < 25.0f) {
            const char *mark = alert ? "!" : (s >= 0.3f || hunting) ? "?" : NULL;
            if (mark) {
                float sc = -v.z < 8.0f ? 3.0f : 2.0f;
                text_draw(sx - text_width(mark, sc) * 0.5f, sy, sc, sus_color(NH_MAX(s, 0.3f), alert, 255), mark);
            }
        }
    }
}

static void draw_ai_labels(const Heist *h, const NhMat34 *view)
{
    const AiWorld *ai = &h->ai;
    for (int i = 0; i < ai->n; i++) {
        const AiGuard *g = &ai->g[i];
        if (!g->active)
            continue;
        float sx, sy;
        NhVec3 head = nh_v3(g->pos.x, g->pos.y + 2.5f, g->pos.z);
        if (!project(view, head, &sx, &sy) || sx < 0 || sx > 640 || sy < 0 || sy > 480)
            continue;
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s %.2f", nh_ai_arch[g->arch].name, nh_ai_state_names[g->state],
                 g->suspicion);
        text_rect(sx - text_width(buf, 1) * 0.5f - 2, sy - 2, text_width(buf, 1) + 4, 11, nh_rgba(0, 0, 0, 140));
        text_draw(sx - text_width(buf, 1) * 0.5f, sy, 1, FG, buf);
    }
}

void hud_draw_heist(const Heist *h, const GameSettings *gs, const NhMat34 *view)
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
    float lv = NH_MAX(h->light_level, h->ai.player_light);
    text_rect(20, 446, 104, 14, nh_rgba(0, 0, 0, 120));
    NhColor lc = nh_rgba((uint8_t)(90 + 165 * lv), (uint8_t)(90 + 140 * lv), (uint8_t)(70 + 60 * lv), 230);
    text_rect(22, 448, 100 * lv, 10, lc);
    text_draw(22, 430, 2, DIM, p->crouch > 0.5f ? "LIGHT  crouched" : "LIGHT");
    /* Torch + battery (bottom right). */
    text_printf(470, 430, 2, h->torch_on ? HI : DIM, "TORCH %s", h->torch_on ? "ON" : "off");
    text_rect(470, 448, 104, 12, nh_rgba(0, 0, 0, 120));
    text_rect(472, 450, 100 * h->battery, 8, h->battery < 0.2f ? nh_rgba(220, 70, 50, 230) : nh_rgba(120, 200, 120, 230));
    draw_detection(h, view);
    if (h->debug_view)
        draw_ai_labels(h, view);
    if (h->takedown >= 0)
        text_draw(320 - text_width("A  Takedown", 2) * 0.5f, 340, 2, FG, "A  Takedown");
    if (h->subtitle_ticks > 0) {
        float w = text_width(h->subtitle, 2);
        text_rect(320 - w * 0.5f - 6, 394, w + 12, 22, nh_rgba(0, 0, 0, 150));
        text_draw(320 - w * 0.5f, 398, 2, nh_rgba(230, 230, 210, 255), h->subtitle);
    }
    if (h->ai.alarm && ((h->tick / 30) & 1))
        text_draw(320 - text_width("ALARM", 3) * 0.5f, 20, 3, RED, "ALARM");
    if (h->caught_t > 0.0f) {
        text_rect(0, 0, 640, 480, nh_rgba(60, 0, 0, (uint8_t)(160 * NH_MIN(1.0f, 2.5f - h->caught_t + 0.2f))));
        text_draw(320 - text_width("CAUGHT", 5) * 0.5f, 200, 5, RED, "CAUGHT");
    } else if (h->status_ticks > 0) {
        text_draw(320 - text_width(h->status, 2) * 0.5f, 300, 2, HI, h->status);
    }
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
    text_printf(x, y, 2, FG, "%s fp %08X %ums", h->job_name, (unsigned)h->plan.fingerprint, (unsigned)w->build_ms);
    y += lh;
    text_printf(x, y, 2, FG, "pos %.1f %.1f %.1f  f%d", p->pos.x, p->pos.y, p->pos.z, p->floor);
    y += lh;
    text_printf(x, y, 2, FG, "%s #%d  light %.2f", rname, room, h->light_level);
    y += lh;
    text_printf(x, y, 2, DIM, "1 dbg 2 view +seed -type C door");
    y += lh + 6;
    const AiWorld *ai = &h->ai;
    text_rect(4, y - 4, 420, (float)ai->n * 11 + 22, nh_rgba(0, 0, 0, 160));
    text_printf(x, y, 1, HI, "guards %d  alerts %d/%d  alarm %d  max sus %.2f  nav exp %d", ai->n, ai->alerts,
                ai->alarm_threshold, ai->alarm, ai->max_suspicion, h->nav ? h->nav->expansions : 0);
    y += 12;
    for (int i = 0; i < ai->n; i++) {
        const AiGuard *g = &ai->g[i];
        text_printf(x, y, 1, FG, "%d %-10s %-11s sus %.2f vis %.2f aw %.2f f%d %s", i, nh_ai_arch[g->arch].name,
                    nh_ai_state_names[g->state], g->suspicion, g->vis, g->awareness, g->floor,
                    g->torch_on ? "torch" : "");
        y += 11;
    }
}
