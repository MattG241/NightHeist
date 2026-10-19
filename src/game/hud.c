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

static const NhColor GOOD = { 120, 220, 130, 255 };

static void panel(float x, float y, float w, float hgt) { text_rect(x, y, w, hgt, nh_rgba(0, 0, 0, 170)); }

static void centered(float y, float scale, NhColor c, const char *s)
{
    text_draw(320 - text_width(s, scale) * 0.5f, y, scale, c, s);
}

/* ---- minigames -------------------------------------------------------------------- */

static void draw_lockpick(const Heist *h)
{
    const NhLockpick *lp = &h->lp;
    panel(150, 120, 340, 210);
    centered(130, 2, HI, "LOCKPICK");
    centered(150, 1, DIM, "Twist the remote to feel for each pin. A to set it. B to stop.");
    float pw = 300.0f / (float)lp->npins;
    for (int i = 0; i < lp->npins; i++) {
        float x = 170 + (float)i * pw;
        bool set = i < lp->cur;
        float hgt = set ? 30 : 60;
        NhColor c = set ? GOOD : i == lp->cur ? nh_rgba((uint8_t)(150 + 105 * lp->feel), (uint8_t)(150 + 50 * lp->feel), 90, 255)
                               : DIM;
        text_rect(x + pw * 0.3f, 250 - hgt, pw * 0.4f, hgt, c);
    }
    /* Feel meter: how close the pick is. */
    text_rect(170, 270, 300, 10, nh_rgba(40, 40, 40, 255));
    text_rect(170, 270, 300 * lp->feel, 10, lp->feel > 0.75f ? GOOD : HI);
    if (lp->stun > 0.0f)
        centered(290, 2, RED, "SLIPPED");
    else
        text_printf(170, 295, 1, DIM, "pins %d/%d   slips %d", lp->cur, lp->npins, lp->slips);
}

static void draw_safe(const Heist *h)
{
    const NhSafecrack *sc = &h->sc;
    panel(170, 100, 300, 260);
    centered(110, 2, HI, "SAFE");
    centered(130, 1, DIM, "Twist to spin the dial. Stop on the clunk, press A.");
    float cx = 320, cy = 240, r = 70;
    for (int k = 0; k < 100; k += 5) {
        float a = (float)k / 100.0f * 6.2832f - 1.5708f - sc->dial / 100.0f * 6.2832f;
        float len = k % 10 == 0 ? 10 : 5;
        text_rect(cx + cosf(a) * r - 1, cy + sinf(a) * r - 1, 3, 3, FG);
        (void)len;
    }
    text_rect(cx - 1, cy - r - 14, 3, 12, RED); /* index mark */
    text_printf(cx - 14, cy - 8, 2, FG, "%02d", (int)sc->dial);
    if (sc->clunk > 0.0f)
        text_rect(cx - r, cy + r + 6, 2 * r * sc->clunk, 6, HI);
    text_printf(185, 335, 1, DIM, "number %d/%d  turn %s", sc->cur + 1, sc->nnums, sc_direction(sc) > 0 ? "RIGHT" : "LEFT");
}

static void draw_hack(const Heist *h)
{
    const NhHack *hk = &h->hk;
    panel(160, 90, 320, 290);
    centered(96, 2, HI, "HACK");
    centered(114, 1, DIM, "Match the signal with the pointer. Hold it steady.");
    /* Window 170..470 x 120..360 maps 0..1 (pointer). */
    text_rect(170, 120, 300, 240, nh_rgba(5, 25, 15, 230));
    float tf = hk->freq[hk->cur], ta = hk->amp[hk->cur];
    float pf = NH_CLAMP((h->player.cursor_x - 170.0f) / 300.0f, 0.0f, 1.0f);
    float pa = 1.0f - NH_CLAMP((h->player.cursor_y - 120.0f) / 240.0f, 0.0f, 1.0f);
    for (int k = 0; k < 150; k++) {
        float x = (float)k / 150.0f;
        float yt = sinf(x * (2.0f + tf * 14.0f) * 3.14159f + (float)h->tick * 0.05f) * ta * 100.0f;
        float yp = sinf(x * (2.0f + pf * 14.0f) * 3.14159f + (float)h->tick * 0.05f) * pa * 100.0f;
        text_rect(170 + x * 300, 240 - yt, 2, 2, nh_rgba(60, 220, 120, 255));
        text_rect(170 + x * 300, 240 - yp, 2, 2, nh_rgba(255, 220, 90, (uint8_t)(120 + 135 * hk->match)));
    }
    text_rect(170, 364, 300 * hk->lock, 6, GOOD);
    text_printf(170, 372, 1, hk->time_limit - hk->time < 8 ? RED : DIM, "layer %d/%d   trace in %.0fs", hk->cur + 1,
                hk->layers, (double)(hk->time_limit - hk->time));
}

/* ---- in-play HUD ------------------------------------------------------------------- */

static void draw_gadgets(const Heist *h)
{
    const NhJob *j = &h->state;
    int g = j->gadget;
    const NhGadgetInfo *gi = &nh_gadgets[g];
    panel(232, 436, 176, 30);
    if (gi->charges == 255)
        text_printf(238, 440, 2, FG, "%s", gi->name);
    else
        text_printf(238, 440, 2, j->charges[g] ? FG : DIM, "%s x%d", gi->name, j->charges[g]);
    text_draw(238, 456, 1, DIM, gi->hint);
    /* Bag and hands */
    text_printf(20, 410, 1, FG, "BAG $%d  (%d/%d)", j->bag_value * 10, j->bag_items, j->bag_cap);
    if (j->holding >= 0)
        text_draw(20, 398, 1, HI, "Carrying loot");
    if (j->carrying >= 0)
        text_draw(20, 398, 1, HI, "Carrying a body");
    if (h->thermal)
        text_printf(470, 410, 1, HI, "THERMAL %d%%", (int)(h->thermal_battery * 100));
    int keys = 0;
    for (int k = 0; k < 32; k++)
        keys += (j->keycards >> k) & 1u;
    if (keys)
        text_printf(470, 398, 1, FG, "keycards %d", keys);
}

static void draw_prompt(const Heist *h)
{
    const Action *a = &h->act;
    if (a->kind == ACT_NOTHING)
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), a->blocked ? "%s" : "C  %s", a->label);
    float w = text_width(buf, 2);
    text_rect(320 - w * 0.5f - 6, 316, w + 12, 20, nh_rgba(0, 0, 0, 140));
    centered(320, 2, a->blocked ? DIM : FG, buf);
    if (a->hold > 0.0f && h->hold_t > 0.0f) {
        text_rect(320 - 60, 340, 120, 6, nh_rgba(40, 40, 40, 200));
        text_rect(320 - 60, 340, 120 * NH_MIN(1.0f, h->hold_t / a->hold), 6, HI);
    }
}

static void draw_timers(const Heist *h)
{
    const NhJob *j = &h->state;
    if (j->police_eta > 0.0f)
        text_printf(250, 44, 2, RED, "POLICE  %02d:%02d", (int)j->police_eta / 60, (int)j->police_eta % 60);
    else if (j->police_here)
        centered(44, 2, RED, "POLICE ON SITE: GET OUT");
    if (h->sec.entry_door >= 0)
        text_printf(214, 64, 2, HI, "DOOR ALARM IN %.0f  disarm the panel", (double)h->sec.entry_t);
    if (h->cams_looped > 0.0f)
        text_printf(20, 386, 1, GOOD, "camera feeds looped %ds", (int)h->cams_looped);
    if (h->drill_t > 0.0f)
        text_printf(20, 374, 1, HI, "drilling %ds", (int)h->drill_t);
    /* Camera detection: an eye that fills up. */
    if (h->sec.max_sus > 0.05f) {
        float s = h->sec.max_sus;
        text_rect(300, 160, 40, 16, nh_rgba(0, 0, 0, 150));
        text_rect(302, 162, 36 * s, 12, s > 0.6f ? RED : HI);
        centered(180, 1, s > 0.6f ? RED : HI, "CAMERA");
    }
}

static void draw_hiding(void)
{
    /* Peeking through the slats. */
    text_rect(0, 0, 640, 150, nh_rgba(0, 0, 0, 235));
    text_rect(0, 330, 640, 150, nh_rgba(0, 0, 0, 235));
    for (int k = 0; k < 6; k++)
        text_rect(0, 150 + (float)k * 32, 640, 12, nh_rgba(0, 0, 0, 200));
}

void hud_draw_heist(const Heist *h, const GameSettings *gs, const NhMat34 *view)
{
    if (h->screen == SCR_SUMMARY) {
        hud_draw_summary(h);
        return;
    }
    if (h->screen == SCR_MAP) {
        hud_draw_map(h);
        return;
    }
    if (h->screen == SCR_OBJECTIVES) {
        hud_draw_objectives(h);
        return;
    }
    const NhPlayer *p = &h->player;
    if (h->hide_prop >= 0)
        draw_hiding();
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
    text_draw(22, 430, 2, DIM, h->hide_prop >= 0 ? "HIDDEN" : p->crouch > 0.5f ? "LIGHT  crouched" : "LIGHT");
    /* Torch + battery (bottom right). */
    text_printf(470, 430, 2, h->torch_on ? HI : DIM, "TORCH %s", h->torch_on ? "ON" : "off");
    text_rect(470, 448, 104, 12, nh_rgba(0, 0, 0, 120));
    text_rect(472, 450, 100 * h->battery, 8, h->battery < 0.2f ? nh_rgba(220, 70, 50, 230) : nh_rgba(120, 200, 120, 230));
    draw_detection(h, view);
    if (h->debug_view)
        draw_ai_labels(h, view);
    if (h->generated)
        draw_gadgets(h);
    draw_timers(h);
    if (h->mg == MG_KIND_LOCK)
        draw_lockpick(h);
    else if (h->mg == MG_KIND_SAFE)
        draw_safe(h);
    else if (h->mg == MG_KIND_HACK)
        draw_hack(h);
    else if (h->takedown >= 0)
        centered(340, 2, FG, "A  Takedown");
    else
        draw_prompt(h);
    if (h->subtitle_ticks > 0) {
        float w = text_width(h->subtitle, 2);
        text_rect(320 - w * 0.5f - 6, 394, w + 12, 22, nh_rgba(0, 0, 0, 150));
        text_draw(320 - w * 0.5f, 398, 2, nh_rgba(230, 230, 210, 255), h->subtitle);
    }
    if (h->ai.alarm && ((h->tick / 30) & 1))
        centered(20, 3, RED, "ALARM");
    if (h->caught_t > 0.0f) {
        text_rect(0, 0, 640, 480, nh_rgba(60, 0, 0, (uint8_t)(160 * NH_MIN(1.0f, 2.5f - h->caught_t + 0.2f))));
        centered(200, 5, RED, "CAUGHT");
    } else if (h->status_ticks > 0) {
        centered(290, 2, HI, h->status);
    }
}

/* ---- pause screens -------------------------------------------------------------------- */

void hud_draw_map(const Heist *h)
{
    const Level *L = &h->world.L;
    int f = NH_CLAMP(h->player.floor, 0, L->floors);
    text_rect(0, 0, 640, 480, nh_rgba(4, 6, 12, 235));
    text_printf(20, 16, 2, HI, "%s   floor %d", h->job_name, f);
    centered(456, 1, DIM, "+ map   - objectives   B back");
    /* Fit the building in 600x400. */
    float cs = NH_MIN(600.0f / (float)L->w, 400.0f / (float)L->d);
    float ox = 320 - cs * (float)L->w * 0.5f, oy = 44;
    for (int z = 0; z < L->d; z++)
        for (int x = 0; x < L->w; x++) {
            const LvCell *c = &L->cells[f][z][x];
            if (c->room < LV_ROOM_FIRST || !h->explored[c->room])
                continue;
            NhColor col = (L->rooms[c->room].flags & LV_R_CORRIDOR) ? nh_rgba(70, 80, 95, 255) : nh_rgba(50, 58, 70, 255);
            if (c->stair)
                col = nh_rgba(90, 90, 100, 255);
            text_rect(ox + (float)x * cs, oy + (float)z * cs, cs, cs, col);
        }
    /* Walls and doors of explored rooms */
    for (int z = 0; z < L->d; z++)
        for (int x = 0; x < L->w; x++)
            for (int sd = 0; sd < 2; sd++) {
                const LvEdge *e = lv_edge_side_c(L, f, x, z, sd);
                if (!e || e->type == LV_E_OPEN || e->type == LV_E_ARCH)
                    continue;
                int a = L->cells[f][z][x].room, b = lv_neighbor_room(L, f, x, z, sd);
                if (!(a >= LV_ROOM_FIRST && h->explored[a]) && !(b >= LV_ROOM_FIRST && h->explored[b]))
                    continue;
                NhColor col = e->type == LV_E_DOOR ? (L->doors[e->ref - 1].locked ? RED : GOOD)
                              : e->type == LV_E_WINDOW ? nh_rgba(120, 170, 230, 255)
                                                       : nh_rgba(200, 200, 205, 255);
                if (sd == 0)
                    text_rect(ox + (float)(x + 1) * cs - 1, oy + (float)z * cs, 2, cs, col);
                else
                    text_rect(ox + (float)x * cs, oy + (float)(z + 1) * cs - 1, cs, 2, col);
            }
    /* Objectives in explored rooms */
    for (int i = 0; i < h->state.nobj; i++) {
        const NhObjective *o = &h->state.obj[i];
        if (o->ent < 0 || o->done)
            continue;
        const NhEntity *e = &h->plan.ent[o->ent];
        if (e->floor != f || !h->explored[e->room])
            continue;
        text_rect(ox + e->pos.x / LV_CELL * cs - 4, oy + e->pos.z / LV_CELL * cs - 4, 8, 8, HI);
    }
    /* The van and the player */
    if (f == 0)
        text_rect(ox + h->plan.extract[0].x / LV_CELL * cs - 5, oy + h->plan.extract[0].z / LV_CELL * cs - 5, 10, 10, GOOD);
    float px = ox + h->player.pos.x / LV_CELL * cs, pz = oy + h->player.pos.z / LV_CELL * cs;
    NhVec3 d = nh_dir_from_yaw_pitch(h->player.yaw, 0);
    text_rect(px - 3, pz - 3, 7, 7, FG);
    text_rect(px + d.x * 8 - 1, pz + d.z * 8 - 1, 3, 3, FG);
}

void hud_draw_objectives(const Heist *h)
{
    const NhJob *j = &h->state;
    text_rect(0, 0, 640, 480, nh_rgba(4, 6, 12, 225));
    text_printf(40, 30, 3, HI, "%s", h->job_name);
    float y = 80;
    for (int i = 0; i < j->nobj; i++) {
        const NhObjective *o = &j->obj[i];
        NhColor c = o->done ? GOOD : o->failed ? RED : FG;
        text_printf(40, y, 2, c, "%s %s%s", o->done ? "[x]" : o->failed ? "[-]" : "[ ]", o->text,
                    o->optional ? "" : "  (main)");
        if (o->bonus)
            text_printf(520, y, 2, DIM, "+$%u", (unsigned)o->bonus * 10u);
        y += 26;
    }
    y += 10;
    text_printf(40, y, 2, FG, "Bag: $%d   items %d/%d", j->bag_value * 10, j->bag_items, j->bag_cap);
    y += 22;
    text_printf(40, y, 2, DIM, "Time %d:%02d   spotted %d   takedowns %d   alarms %d", (int)j->time / 60,
                (int)j->time % 60, j->spotted + j->camera_spots, j->takedowns, j->alarms);
    y += 30;
    text_draw(40, y, 2, DIM, "Gadgets:");
    y += 20;
    for (int g = 1; g < GD_COUNT; g++) {
        if (!j->charges[g])
            continue;
        text_printf(40 + (float)((g - 1) % 2) * 290, y, 1, g == j->gadget ? HI : FG, "%s %s", nh_gadgets[g].name,
                    nh_gadgets[g].charges == 255 ? "" : "");
        if ((g - 1) % 2 == 1)
            y += 14;
    }
    centered(456, 1, DIM, "- objectives   + map   B back");
}

void hud_draw_summary(const Heist *h)
{
    const NhJobResult *r = &h->result;
    const NhJob *j = &h->state;
    text_rect(0, 0, 640, 480, nh_rgba(4, 6, 12, 240));
    static const char *const k_out[] = { "", "JOB DONE", "GOT OUT (target missing)", "JOB ABORTED", "BUSTED" };
    centered(30, 3, r->outcome == JOB_BUSTED ? RED : HI, k_out[r->outcome]);
    char grade[2] = { r->grade, 0 };
    text_draw(560, 100, 6, r->grade == 'F' ? RED : r->grade == 'S' ? HI : FG, grade);
    text_draw(548, 150, 1, DIM, "grade");
    float y = 110;
    for (int i = 0; i < j->nobj; i++) {
        const NhObjective *o = &j->obj[i];
        text_printf(60, y, 2, o->done ? GOOD : o->failed ? RED : DIM, "%s %s", o->done ? "[x]" : "[ ]", o->text);
        y += 22;
    }
    y += 12;
    text_printf(60, y, 2, FG, "Loot            $%d", r->loot * 10);
    y += 22;
    text_printf(60, y, 2, FG, "Bonuses         $%d", r->bonus * 10);
    y += 22;
    text_printf(60, y, 2, HI, "Payout          $%d", r->payout * 10);
    y += 30;
    text_printf(60, y, 2, FG, "Stealth %d%%%s   heat +%d", r->stealth, r->ghost ? "  GHOST" : "", r->heat);
    y += 22;
    text_printf(60, y, 2, DIM, "Time %d:%02d  spotted %d  takedowns %d  alarms %d  locks %d", (int)j->time / 60,
                (int)j->time % 60, j->spotted + j->camera_spots, j->takedowns, j->alarms, j->locks_picked);
    centered(440, 2, FG, "A  next job      B  retry this one");
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
