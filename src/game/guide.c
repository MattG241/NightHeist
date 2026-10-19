#include "guide.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/nh_fixedstep.h"
#include "text.h"

static const NhColor FG = { 230, 230, 230, 255 };
static const NhColor DIM = { 150, 150, 160, 255 };
static const NhColor HI = { 255, 200, 90, 255 };
static const NhColor MARK = { 255, 214, 80, 255 };

static const char *const k_tips[TIP_COUNT] = {
    [TIP_MARKER] = "Follow the yellow marker. GO (top left) always says what to do next.",
    [TIP_GUARD] = "A guard. Light gives you away: keep to the dark and hold Z to sneak.",
    [TIP_LOCK] = "Locked. Hold C to pick it, twist the remote until it rumbles, then A.",
    [TIP_NOTICED] = "Someone noticed you! Back off into the dark until the marker fades.",
    [TIP_CAMERA] = "A camera. Wait for it to sweep away, or cut the power at a fuse box.",
    [TIP_GOT_IT] = "Got it! Now follow the marker back to the van.",
    [TIP_LIGHT] = "You're standing in light. The LIGHT bar shows how easy you are to see.",
    [TIP_HIDE] = "Lockers and wardrobes: C to hide when a guard comes your way.",
};

static const NhObjective *main_obj(const Heist *h)
{
    return h->state.nobj > 0 ? &h->state.obj[0] : NULL;
}

/* Is the target already in someone's hands (heavy loot counts at the van)? */
static bool target_in_hand(const Heist *h)
{
    const NhObjective *o = main_obj(h);
    if (!o || o->ent < 0)
        return false;
    for (int i = 0; i < h->nplayers; i++)
        if (h->pl[i].holding == o->ent)
            return true;
    return job_taken(&h->state, o->ent) && o->type == OBJ_STEAL;
}

static bool heading_out(const Heist *h)
{
    const NhObjective *o = main_obj(h);
    return !o || o->done || target_in_hand(h);
}

const char *guide_goal(const Heist *h, char *buf, int len)
{
    const NhObjective *o = main_obj(h);
    if (!h->generated || !o) {
        snprintf(buf, (size_t)len, "Look around");
        return buf;
    }
    const HeistPlayer *mate = h->nplayers > 1 ? &h->pl[1 - h->me->index] : NULL;
    if (mate && mate->cuffed)
        snprintf(buf, (size_t)len, "Free your partner (hold C next to them)");
    else if (heading_out(h))
        snprintf(buf, (size_t)len, "Get back to the van");
    else
        snprintf(buf, (size_t)len, "%s", o->text);
    return buf;
}

bool guide_target(const Heist *h, NhVec3 *pos)
{
    if (!h->generated)
        return false;
    const HeistPlayer *mate = h->nplayers > 1 ? &h->pl[1 - h->me->index] : NULL;
    if (mate && mate->cuffed) {
        *pos = mate->player.pos;
        return true;
    }
    const NhObjective *o = main_obj(h);
    if (heading_out(h) || o->ent < 0) {
        if (h->plan.nextract <= 0)
            return false;
        *pos = h->plan.extract[0];
        return true;
    }
    *pos = h->plan.ent[o->ent].pos;
    return true;
}

void guide_tip(Heist *h, GuideTip tip)
{
    if (tip < 0 || tip >= TIP_COUNT || (h->tips_seen >> tip) & 1u)
        return;
    h->tips_seen |= 1u << tip;
    h->me->tip = (int)tip + 1;
    h->me->tip_t = 7.0f;
}

void guide_tick(Heist *h)
{
    HeistPlayer *me = h->me;
    const float dt = NH_TICK_DT;
    const NhPlayer *p = &me->player;
    if (me->tip_t > 0.0f && (me->tip_t -= dt) <= 0.0f)
        me->tip = 0;
    /* Leaving the van's patch of street once unlocks "leave". */
    if (h->generated && h->plan.nextract > 0 && nh_v3_dist(p->pos, h->plan.extract[0]) > 7.0f)
        me->left_start = true;
    /* The route, twice a second. */
    me->guide_t -= dt;
    if (me->guide_t <= 0.0f) {
        me->guide_t = 0.5f;
        me->nguide = 0;
        NhVec3 goal;
        if (guide_target(h, &goal)) {
            me->guide_floor = lv_floor_of(goal.y + 0.3f);
            NhPath path;
            if (h->nav && nh_nav_path(h->nav, p->pos, goal, true, &path) && path.n > 0) {
                float len = 0.0f;
                NhVec3 prev = p->pos;
                for (int i = 0; i < path.n; i++) {
                    len += nh_v3_dist(prev, path.p[i]);
                    prev = path.p[i];
                    /* Skip corners right under our feet. */
                    if (me->nguide < 3 && (me->nguide > 0 || nh_v3_dist_xz(path.p[i], p->pos) > 1.2f ||
                                           i == path.n - 1))
                        me->guide[me->nguide++] = path.p[i];
                }
                me->guide_len = len;
            } else {
                me->guide[0] = goal;
                me->nguide = 1;
                me->guide_len = nh_v3_dist(p->pos, goal);
            }
        }
    }
    /* First-time tips */
    if (!h->generated || h->screen != SCR_PLAY || me->cuffed)
        return;
    if (me->tip_t <= 0.0f) {
        float lv = NH_MAX(me->light_level, h->ai.view_light[me->index]);
        me->lit_t = lv > 0.55f ? me->lit_t + dt : 0.0f;
        if (h->tick > 30)
            guide_tip(h, TIP_MARKER);
        for (int i = 0; i < h->ai.n; i++) {
            const AiGuard *g = &h->ai.g[i];
            if (g->active && g->arch != ARCH_DOG && g->state != AS_KO && g->state != AS_CARRIED &&
                nh_v3_dist(g->pos, p->pos) < 11.0f && g->floor == p->floor) {
                guide_tip(h, TIP_GUARD);
                break;
            }
        }
        if (me->act.kind == ACT_DOOR && h->world.L.doors[me->act.target].locked &&
            h->world.L.doors[me->act.target].lock == LV_LOCK_PICK)
            guide_tip(h, TIP_LOCK);
        if (me->act.kind == ACT_HIDE && me->hide_prop < 0)
            guide_tip(h, TIP_HIDE);
        if (h->ai.max_suspicion > 0.3f)
            guide_tip(h, TIP_NOTICED);
        if (h->sec.max_sus > 0.05f)
            guide_tip(h, TIP_CAMERA);
        if (heading_out(h) && h->state.nobj > 0)
            guide_tip(h, TIP_GOT_IT);
        if (me->lit_t > 2.0f)
            guide_tip(h, TIP_LIGHT);
    }
}

/* World point to the 640x480 HUD; false if behind the camera. */
static bool project(const NhMat34 *view, float aspect, NhVec3 p, float *sx, float *sy)
{
    NhVec3 v = nh_m34_mul_point(view, p);
    if (v.z > -0.2f)
        return false;
    float t = tanf(NH_DEG2RAD(HEIST_FOVY * 0.5f));
    *sx = 320.0f + (v.x / -v.z) / (t * aspect) * 320.0f;
    *sy = 240.0f - (v.y / -v.z) / t * 240.0f;
    return true;
}

static void centered(float y, float scale, NhColor c, const char *s)
{
    text_draw(320 - text_width(s, scale) * 0.5f, y, scale, c, s);
}

void guide_draw(const Heist *h, const NhMat34 *view, float aspect)
{
    const HeistPlayer *me = h->me;
    const NhPlayer *p = &me->player;
    /* The goal line, always on screen. */
    char goal[64];
    guide_goal(h, goal, sizeof(goal));
    float x0 = h->nplayers > 1 ? 44.0f : 12.0f;
    char where[48] = "";
    if (me->nguide > 0) {
        if (me->guide_floor != p->floor)
            snprintf(where, sizeof(where), "%s to floor %d, %d m", me->guide_floor > p->floor ? "up" : "down",
                     me->guide_floor, (int)me->guide_len);
        else
            snprintf(where, sizeof(where), "%d m", (int)me->guide_len);
    }
    float w = NH_MAX(text_width(goal, 2), text_width(where, 2)) + 76;
    text_rect(x0 - 4, 6, w, where[0] ? 42 : 24, nh_rgba(0, 0, 0, 150));
    text_rect(x0 - 4, 6, 3, where[0] ? 42 : 24, HI);
    text_draw(x0 + 4, 10, 2, HI, "GO:");
    text_draw(x0 + 52, 10, 2, FG, goal);
    if (where[0])
        text_draw(x0 + 52, 28, 2, MARK, where);
    /* The marker: a chevron over the next corner of the route, the distance
     * under it; off screen, an arrow at the edge pointing the way. */
    if (me->nguide > 0 && me->view == SCR_PLAY && me->mg == MG_KIND_NONE) {
        NhVec3 at = me->guide[0];
        at.y += 1.3f;
        float sx, sy;
        bool front = project(view, aspect, at, &sx, &sy);
        float pulse = 0.75f + 0.25f * sinf((float)h->tick * 0.12f);
        NhColor c = nh_rgba(MARK.r, MARK.g, MARK.b, (uint8_t)(230 * pulse));
        if (front && sx > 24 && sx < 616 && sy > 48 && sy < 420) {
            NhColor shadow = nh_rgba(0, 0, 0, (uint8_t)(160 * pulse));
            text_draw(sx - 12 + 2, sy - 24 + 2, 4, shadow, "v");
            text_draw(sx - 12, sy - 24, 4, c, "v");
            char d[16];
            snprintf(d, sizeof(d), "%d m", (int)me->guide_len);
            float dw = text_width(d, 2);
            text_rect(sx - dw * 0.5f - 3, sy + 8, dw + 6, 18, nh_rgba(0, 0, 0, 120));
            text_draw(sx - dw * 0.5f, sy + 10, 2, c, d);
        } else {
            /* Direction relative to where we're facing. */
            NhVec3 to = nh_v3_sub(at, p->pos);
            float ang = atan2f(to.x, -to.z); /* 0 = -Z */
            float rel = nh_wrap_angle(ang - (-p->yaw));
            const char *arrow;
            float ax, ay;
            if (fabsf(rel) < NH_PI * 0.25f && !front) {
                arrow = "v"; /* behind and below the view: turn round */
                ax = 312, ay = 400;
            } else if (rel > 0.0f) {
                arrow = ">";
                ax = 606, ay = 226;
            } else {
                arrow = "<";
                ax = 12, ay = 230;
            }
            if (fabsf(rel) > NH_PI * 0.75f) {
                arrow = "TURN AROUND";
                ax = 320 - text_width(arrow, 2) * 0.5f, ay = 400;
                text_draw(ax, ay, 2, c, arrow);
            } else {
                text_draw(ax + 2, ay + 2, 4, nh_rgba(0, 0, 0, 160), arrow);
                text_draw(ax, ay, 4, c, arrow);
            }
        }
    }
    /* A first-time tip, in big type, wrapped onto two lines. */
    if (me->tip > 0 && me->tip_t > 0.0f) {
        const char *t = k_tips[me->tip - 1];
        char l1[64], l2[64];
        int n = (int)strlen(t), cut = n;
        if (n > 46) {
            cut = 46;
            while (cut > 0 && t[cut] != ' ')
                cut--;
        }
        snprintf(l1, sizeof(l1), "%.*s", cut, t);
        snprintf(l2, sizeof(l2), "%s", cut < n ? t + cut + 1 : "");
        float tw = NH_MAX(text_width(l1, 2), text_width(l2, 2)) + 24;
        float th = l2[0] ? 44.0f : 24.0f;
        uint8_t a = (uint8_t)(225 * NH_MIN(1.0f, me->tip_t));
        text_rect(320 - tw * 0.5f, 362 - th, tw, th, nh_rgba(24, 18, 4, a));
        text_rect(320 - tw * 0.5f, 362 - th, 4, th, nh_rgba(255, 200, 90, a));
        NhColor tc = nh_rgba(255, 236, 190, a);
        text_draw(320 - text_width(l1, 2) * 0.5f, 362 - th + 5, 2, tc, l1);
        if (l2[0])
            text_draw(320 - text_width(l2, 2) * 0.5f, 362 - th + 25, 2, tc, l2);
    }
}

void guide_draw_briefing(const Heist *h, const GameSettings *gs)
{
    text_rect(0, 0, 640, 480, nh_rgba(4, 6, 12, 235));
    centered(30, 3, HI, h->job_name);
    centered(62, 1, DIM, h->campaign ? nh_districts[h->offer.district].name : "");
    float y = 92;
    text_draw(60, y, 2, HI, "THE JOB");
    y += 24;
    for (int i = 0; i < h->state.nobj; i++) {
        const NhObjective *o = &h->state.obj[i];
        text_printf(76, y, 2, i == 0 ? FG : DIM, "%s %s", i == 0 ? "*" : "-", o->text);
        if (o->optional || i > 0)
            text_draw(76 + text_width(o->text, 2) + 30, y + 4, 1, DIM, "(bonus)");
        y += 20;
    }
    y += 14;
    text_draw(60, y, 2, HI, "HOW");
    y += 24;
    static const char *const steps[] = {
        "1. Follow the yellow marker. GO (top left) always says what to do next.",
        "2. Stay in the dark. Hold Z to sneak. Keep your torch (B) off near guards.",
        "3. C uses things: doors, loot, safes. Watch the prompt in the middle.",
        "4. Got it? The marker takes you back to the van. Hold C there to leave.",
    };
    for (int i = 0; i < 4; i++, y += 18)
        text_draw(76, y, 1, FG, steps[i]);
    y += 14;
    text_draw(60, y, 2, HI, "CONTROLS");
    y += 24;
    if (gs->controls.scheme == NH_CONTROLS_SIMPLE) {
        text_draw(76, y, 1, FG, "Nunchuk stick: up/down walk, left/right turn.   Point at the screen: aim the torch.");
        text_draw(76, y + 16, 1, FG, "Z: sneak   C: use   B: torch   A: gadget   D-pad: gadgets (hold down to run)");
    } else {
        text_draw(76, y, 1, FG, "Nunchuk stick: walk and step sideways.   Point at the screen edges to turn.");
        text_draw(76, y + 16, 1, FG, "Z: sneak   C: use   B: torch   A: gadget   D-pad: gadgets (hold down to run)");
    }
    if (h->nplayers > 1)
        text_draw(76, y + 34, 1, HI, "Co-op: stick together. If one of you is caught, the other can free them.");
    if ((h->tick / 30) & 1)
        centered(440, 3, HI, "A  START");
}
