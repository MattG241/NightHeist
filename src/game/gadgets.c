#include "gadgets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "core/nh_fixedstep.h"
#include "core/nh_proptab.h"
#include "res.h"

static NhVec3 eye_pos(const Heist *h) { return nh_player_eye(&h->player, &h->world.L, 1.0f); }
static NhVec3 aim(const Heist *h) { return nh_player_aim_dir(&h->player, gfx_display_aspect(), HEIST_FOVY); }

/* Guard best lined up with the aim within range (line of sight required). */
static int aimed_guard(const Heist *h, float range, float min_cos, bool any_state)
{
    NhVec3 eye = eye_pos(h), dir = aim(h);
    int best = -1;
    float bc = min_cos;
    for (int i = 0; i < h->ai.n; i++) {
        const AiGuard *g = &h->ai.g[i];
        if (!g->active || g->state == AS_KO || g->state == AS_CARRIED || (!any_state && g->ko_timer > 0.0f))
            continue;
        NhVec3 c = nh_v3(g->pos.x, g->pos.y + (g->arch == ARCH_DOG ? 0.4f : 1.2f), g->pos.z);
        NhVec3 to = nh_v3_sub(c, eye);
        float d = nh_v3_len(to);
        if (d > range || d < 0.1f)
            continue;
        float cs = nh_v3_dot(nh_v3_scale(to, 1.0f / d), dir);
        float t;
        if (cs > bc && !lv_raycast(&h->world.L, eye, c, &t)) {
            bc = cs;
            best = i;
        }
    }
    return best;
}

static int aimed_camera(const Heist *h, float range)
{
    NhVec3 eye = eye_pos(h), dir = aim(h);
    int best = -1;
    float bc = 0.95f;
    for (int i = 0; i < h->sec.ncam; i++) {
        NhVec3 to = nh_v3_sub(h->sec.cam[i].pos, eye);
        float d = nh_v3_len(to);
        if (d > range || d < 0.1f)
            continue;
        float cs = nh_v3_dot(nh_v3_scale(to, 1.0f / d), dir);
        float t;
        if (cs > bc && !lv_raycast(&h->world.L, eye, nh_v3_sub(h->sec.cam[i].pos, nh_v3_scale(to, 0.3f / d)), &t)) {
            bc = cs;
            best = i;
        }
    }
    return best;
}

static int aimed_entity(const Heist *h, int kind, float range, float min_cos)
{
    NhVec3 eye = eye_pos(h), dir = aim(h);
    int best = -1;
    float bc = min_cos;
    for (int i = 0; i < h->plan.nent; i++) {
        const NhEntity *e = &h->plan.ent[i];
        if (e->kind != kind || job_taken(&h->state, i))
            continue;
        NhVec3 to = nh_v3_sub(nh_v3_add(e->pos, nh_v3(0, 0.3f, 0)), eye);
        float d = nh_v3_len(to);
        if (d > range || d < 0.05f)
            continue;
        float cs = nh_v3_dot(nh_v3_scale(to, 1.0f / d), dir);
        if (cs > bc) {
            bc = cs;
            best = i;
        }
    }
    return best;
}

static bool cut_window(Heist *h)
{
    NhPlayer *p = &h->player;
    Level *L = &h->world.L;
    NhVec3 dir = aim(h);
    int cx = lv_cell_of(p->pos.x), cz = lv_cell_of(p->pos.z);
    /* The wall edge the player faces in this cell or the next one over. */
    static const int dx[4] = { 1, 0, -1, 0 }, dz[4] = { 0, 1, 0, -1 };
    int best_s = -1;
    float bc = 0.6f;
    for (int s = 0; s < 4; s++) {
        float c = dir.x * (float)dx[s] + dir.z * (float)dz[s];
        if (c > bc) {
            bc = c;
            best_s = s;
        }
    }
    if (best_s < 0)
        return false;
    LvEdge *e = lv_edge_side(L, p->floor, cx, cz, best_s);
    NhVec3 ctr = lv_cell_center(p->floor, cx, cz);
    float dist = fabsf((best_s & 1 ? p->pos.z - ctr.z : p->pos.x - ctr.x) * (best_s >= 2 ? -1.0f : 1.0f));
    if (!e || e->type != LV_E_WINDOW || e->ref != LV_WIN_CLOSED || LV_CELL * 0.5f - dist > 1.4f)
        return false;
    e->ref = LV_WIN_CUT;
    audio_play(SFX_GLASS_CUT, 0.6f, 0.0f, 1.0f);
    NhVec3 wp = nh_v3(ctr.x + (float)dx[best_s], lv_floor_y(p->floor) + 1.5f, ctr.z + (float)dz[best_s]);
    ai_add_anomaly(&h->ai, AN_GLASS_BROKEN, wp, 1000 + cx * 64 + cz);
    heist_noise(h, wp, 2.0f, NH_NOISE_GLASS);
    return true;
}

static void throw_gadget(Heist *h, int g)
{
    for (int i = 0; i < (int)NH_ARRAY_LEN(h->proj); i++) {
        Projectile *pr = &h->proj[i];
        if (pr->active)
            continue;
        memset(pr, 0, sizeof(*pr));
        pr->active = 1;
        pr->gadget = (uint8_t)g;
        NhVec3 d = aim(h);
        pr->pos = nh_v3_add(eye_pos(h), nh_v3_scale(d, 0.4f));
        pr->vel = nh_v3_add(nh_v3_scale(d, 11.0f), nh_v3(0, 2.2f, 0));
        audio_play(SFX_THROW, 0.5f, 0.2f, 1.0f);
        return;
    }
}

static void land(Heist *h, Projectile *pr)
{
    pr->landed = true;
    switch (pr->gadget) {
    case GD_COIN:
        audio_at(SFX_COIN, pr->pos, 0.9f, 20.0f);
        ai_distraction(&h->ai, pr->pos, 8.0f, NH_NOISE_THROW);
        pr->t = 2.0f;
        break;
    case GD_NOISEMAKER:
        audio_at(SFX_COIN, pr->pos, 0.6f, 20.0f);
        pr->t = 9.0f;
        break;
    case GD_SMOKE:
        audio_at(SFX_SMOKE, pr->pos, 0.9f, 20.0f);
        ai_add_smoke(&h->ai, nh_v3_add(pr->pos, nh_v3(0, 1.0f, 0)), 2.6f, 16.0f);
        heist_noise(h, pr->pos, 4.0f, NH_NOISE_THROW);
        pr->t = 16.0f;
        break;
    case GD_EMP:
        audio_at(SFX_EMP, pr->pos, 1.0f, 25.0f);
        sec_emp(&h->sec, pr->pos, 7.0f, 15.0f);
        heist_noise(h, pr->pos, 3.0f, NH_NOISE_GADGET);
        pr->t = 1.0f;
        break;
    default:
        pr->t = 0.5f;
        break;
    }
}

bool gadgets_update(Heist *h, const NhInput *in, bool a_free)
{
    const float dt = NH_TICK_DT;
    NhJob *j = &h->state;
    /* Cycle with the D-pad (skipping empty gadgets). */
    int dir = (in->pressed & NH_BTN_RIGHT) ? 1 : (in->pressed & NH_BTN_LEFT) ? -1 : 0;
    if (dir) {
        for (int k = 1; k <= GD_COUNT; k++) {
            int g = (j->gadget + dir * k + GD_COUNT * 2) % GD_COUNT;
            if (j->charges[g] > 0) {
                if (g != j->gadget)
                    audio_play(SFX_UI_MOVE, 0.3f, 0.0f, 1.0f);
                j->gadget = g;
                break;
            }
        }
    }
    /* D-pad up toggles the thermal goggles directly. */
    if ((in->pressed & NH_BTN_UP) && h->thermal_battery > 0.0f)
        h->thermal = !h->thermal;
    if (h->thermal) {
        h->thermal_battery -= dt / (90.0f * h->perks.thermal_life);
        if (h->thermal_battery <= 0.0f) {
            h->thermal_battery = 0.0f;
            h->thermal = false;
        }
    }
    /* Projectiles in flight and landed effects. */
    for (int i = 0; i < (int)NH_ARRAY_LEN(h->proj); i++) {
        Projectile *pr = &h->proj[i];
        if (!pr->active)
            continue;
        if (!pr->landed) {
            pr->vel.y -= 9.8f * dt;
            NhVec3 next = nh_v3_add(pr->pos, nh_v3_scale(pr->vel, dt));
            float t;
            if (lv_raycast(&h->world.L, pr->pos, next, &t)) {
                pr->pos = nh_v3_lerp(pr->pos, next, NH_MAX(0.0f, t - 0.05f));
                land(h, pr);
            } else {
                pr->pos = next;
                float g = lv_ground_height(&h->world.L, pr->pos.x, pr->pos.z, pr->pos.y + 0.1f);
                if (pr->pos.y <= g + 0.05f) {
                    pr->pos.y = g + 0.05f;
                    land(h, pr);
                }
            }
            if ((pr->t += dt) > 6.0f)
                land(h, pr);
        } else {
            float before = pr->t;
            pr->t -= dt;
            if (pr->gadget == GD_NOISEMAKER && (int)(before / 2.0f) != (int)(pr->t / 2.0f))
                ai_distraction(&h->ai, pr->pos, 16.0f, NH_NOISE_GADGET);
            if (pr->t <= 0.0f)
                pr->active = 0;
        }
    }
    /* The safe drill grinds on by itself, loudly. */
    if (h->drill_t > 0.0f) {
        float before = h->drill_t;
        h->drill_t -= dt;
        const NhEntity *e = &h->plan.ent[h->drill_target];
        if ((int)(before / 1.5f) != (int)(h->drill_t / 1.5f))
            heist_noise(h, e->pos, 10.0f, NH_NOISE_GADGET);
        if (h->drill_t <= 0.0f) {
            job_mark_taken(j, h->drill_target);
            job_event(j, &h->plan, JE_SAFE_OPEN, h->drill_target, 0);
            ai_add_anomaly(&h->ai, AN_SAFE_OPEN, e->pos, h->drill_target);
            heist_status(h, "The drill's through: safe open");
        }
    }
    if (!a_free || !(in->pressed & NH_BTN_A))
        return false;
    int g = j->gadget;
    if (j->charges[g] == 0 || g == GD_LOCKPICK)
        return false;
    if (j->carrying >= 0 || j->holding >= 0) {
        heist_status(h, "Hands full");
        return true;
    }
    bool used = false;
    switch (g) {
    case GD_COIN: case GD_NOISEMAKER: case GD_SMOKE: case GD_EMP:
        throw_gadget(h, g);
        used = true;
        break;
    case GD_TASER: {
        int t = aimed_guard(h, nh_gadgets[g].range, 0.85f, true);
        if (t >= 0) {
            ai_knockout(&h->ai, t);
            audio_at(SFX_TASER, h->ai.g[t].pos, 0.9f, 15.0f);
            heist_noise(h, h->ai.g[t].pos, 5.0f, NH_NOISE_TAKEDOWN);
            used = true;
        }
        break;
    }
    case GD_DART: {
        int t = aimed_guard(h, nh_gadgets[g].range, 0.985f, false);
        if (t >= 0) {
            ai_tranquilise(&h->ai, t, h->perks.dart_time);
            audio_play(SFX_DART, 0.6f, 0.1f, 1.0f);
            used = true;
        }
        break;
    }
    case GD_JAMMER: {
        int c = aimed_camera(h, nh_gadgets[g].range);
        if (c >= 0) {
            h->sec.cam[c].jam_t = 20.0f;
            audio_play(SFX_JAMMER, 0.5f, 0.0f, 1.0f);
            heist_status(h, "Camera jammed for 20 s");
            used = true;
        }
        break;
    }
    case GD_CUTTER:
        used = cut_window(h);
        if (used)
            heist_status(h, "Window cut");
        break;
    case GD_CLONER: {
        float d;
        for (int i = 0; i < h->ai.n && !used; i++) {
            const AiGuard *gd = &h->ai.g[i];
            if (!gd->active || gd->arch == ARCH_DOG || nh_gadgets[GD_CLONER].range < nh_v3_dist(gd->pos, h->player.pos))
                continue;
            if (!ai_can_takedown(&h->ai, i, h->player.pos, &d))
                continue;
            int levels = gd->arch == ARCH_ELITE || gd->arch == ARCH_SUPERVISOR ? 3 : gd->arch == ARCH_VETERAN ? 2 : 1;
            for (int k = 1; k <= levels; k++)
                j->keycards |= 1u << k;
            char buf[48];
            snprintf(buf, sizeof(buf), "Cloned keycards 1-%d", levels);
            audio_play(SFX_SWIPE, 0.6f, 0.0f, 1.1f);
            heist_status(h, buf);
            used = true;
        }
        break;
    }
    case GD_THERMAL:
        if (h->thermal_battery > 0.0f)
            h->thermal = !h->thermal;
        return true;
    case GD_MIRROR: {
        NhVec3 eye = eye_pos(h), dir = aim(h);
        for (int i = 0; i < h->sec.nlas && !used; i++) {
            SecLaser *l = &h->sec.las[i];
            if (l->mirrored)
                continue;
            NhVec3 mid = nh_v3_lerp(l->a, l->b, 0.5f);
            NhVec3 to = nh_v3_sub(mid, eye);
            float d = nh_v3_len(to);
            if (d < 3.5f && nh_v3_dot(nh_v3_scale(to, 1.0f / d), dir) > 0.8f) {
                l->mirrored = 1;
                audio_play(SFX_SAFE_SET, 0.5f, 0.0f, 1.4f);
                heist_status(h, "Beam bent out of the way");
                used = true;
            }
        }
        break;
    }
    case GD_DRILL: {
        int s = aimed_entity(h, ENT_SAFE, nh_gadgets[g].range, 0.8f);
        if (s >= 0 && h->drill_t <= 0.0f) {
            h->drill_t = 18.0f;
            h->drill_target = s;
            heist_status(h, "Drilling... (18 s, loud)");
            used = true;
        }
        break;
    }
    case GD_DIALER: {
        int ph = aimed_entity(h, ENT_PHONE, nh_gadgets[g].range, 0.9f);
        if (ph >= 0) {
            ai_distraction(&h->ai, h->plan.ent[ph].pos, 12.0f, NH_NOISE_PHONE);
            audio_play(SFX_KEYPAD_OK, 0.4f, 0.0f, 1.2f);
            h->phone_t = 6.0f;
            h->phone_pos = h->plan.ent[ph].pos;
            heist_status(h, "Ring ring...");
            used = true;
        }
        break;
    }
    default:
        break;
    }
    if (used) {
        if (j->charges[g] != 255)
            j->charges[g]--;
    } else {
        audio_play(SFX_UI_DENY, 0.5f, 0.0f, 1.0f);
        heist_status(h, "No target");
    }
    return true;
}

static void billboard(const NhMat34 *view, GfxTexture tex, NhVec3 p, float s, NhColor col, uint16_t flags)
{
    NhVec3 right = nh_v3(view->m[0][0], view->m[0][1], view->m[0][2]);
    NhVec3 up = nh_v3(view->m[1][0], view->m[1][1], view->m[1][2]);
    NhVec3 r = nh_v3_scale(right, s), u = nh_v3_scale(up, s);
    NhVec3 c[4] = { nh_v3_sub(nh_v3_sub(p, r), u), nh_v3_sub(nh_v3_add(p, r), u), nh_v3_add(nh_v3_add(p, r), u),
                    nh_v3_add(nh_v3_sub(p, r), u) };
    gfx_draw_quad3d(tex, c, 0, 0, 1, 1, col, flags);
}

void gadgets_render(Heist *h, const NhMat34 *view)
{
    static GfxTexture glow;
    if (!glow)
        glow = res_texture("glow");
    float time = (float)h->tick / 60.0f;
    /* Projectiles */
    for (int i = 0; i < (int)NH_ARRAY_LEN(h->proj); i++) {
        const Projectile *pr = &h->proj[i];
        if (!pr->active)
            continue;
        NhColor c = pr->gadget == GD_NOISEMAKER ? nh_rgba(255, 60, 40, (uint8_t)(((int)(time * 4) & 1) ? 230 : 40))
                    : pr->gadget == GD_EMP      ? nh_rgba(120, 180, 255, 200)
                                                : nh_rgba(220, 220, 200, 160);
        billboard(view, glow, pr->pos, 0.08f, c, GFX_MAT_ADD);
    }
    /* Smoke clouds */
    for (int i = 0; i < (int)NH_ARRAY_LEN(h->ai.smoke); i++) {
        if (h->ai.smoke[i].t <= 0.0f)
            continue;
        float life = NH_MIN(1.0f, h->ai.smoke[i].t / 3.0f);
        float r = h->ai.smoke[i].radius;
        for (int k = 0; k < 10; k++) {
            float a = (float)k * 2.39996f + time * 0.15f;
            float rr = r * 0.55f * sqrtf((float)(k + 1) / 10.0f);
            NhVec3 p = nh_v3_add(h->ai.smoke[i].pos, nh_v3(cosf(a) * rr, sinf(a * 1.7f) * 0.5f, sinf(a) * rr));
            billboard(view, glow, p, r * 0.8f, nh_rgba(120, 122, 128, (uint8_t)(170 * life)), GFX_MAT_BLEND);
        }
    }
    /* Laser beams: thin camera-facing ribbons. */
    NhVec3 eye = nh_player_eye(&h->player, &h->world.L, h->alpha);
    for (int i = 0; i < h->sec.nlas; i++) {
        if (!sec_laser_live(&h->sec, &h->world.L, i))
            continue;
        const SecLaser *l = &h->sec.las[i];
        NhVec3 ab = nh_v3_sub(l->b, l->a);
        NhVec3 side = nh_v3_norm(nh_v3_cross(ab, nh_v3_sub(nh_v3_lerp(l->a, l->b, 0.5f), eye)));
        side = nh_v3_scale(side, 0.012f);
        NhVec3 c[4] = { nh_v3_sub(l->a, side), nh_v3_sub(l->b, side), nh_v3_add(l->b, side), nh_v3_add(l->a, side) };
        gfx_draw_quad3d(glow, c, 0.5f, 0.0f, 0.5f, 1.0f, nh_rgba(255, 30, 20, 230), GFX_MAT_ADD | GFX_MAT_NOCULL);
        billboard(view, glow, l->a, 0.06f, nh_rgba(255, 60, 40, 255), GFX_MAT_ADD);
    }
    /* Drill sparks */
    if (h->drill_t > 0.0f) {
        const NhEntity *e = &h->plan.ent[h->drill_target];
        billboard(view, glow, nh_v3_add(e->pos, nh_v3(0, 0.4f, 0)), 0.1f + 0.05f * sinf(time * 40.0f),
                  nh_rgba(255, 200, 120, 220), GFX_MAT_ADD);
    }
}
