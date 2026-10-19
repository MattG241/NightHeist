#include "interact.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "core/nh_fixedstep.h"
#include "core/nh_proptab.h"

#define REACH 2.3f

const char *interact_prop_name(int type)
{
    switch (type) {
    case PROP_GOLD_BARS: return "the gold";
    case PROP_JEWELRY_BOX: return "the jewellery";
    case PROP_HARD_DRIVE: return "the hard drive";
    case PROP_DOCUMENTS: return "the documents";
    case PROP_PAINTING: return "the painting";
    case PROP_STATUE: return "the statue";
    case PROP_CRATE: return "the crate";
    case PROP_CASH_STACK: return "the cash";
    case PROP_WATCH_CASE: return "the watches";
    case PROP_LAPTOP: return "the laptop";
    case PROP_LOCKER: return "the locker";
    case PROP_WARDROBE: return "the wardrobe";
    case PROP_DESK: return "the desk";
    case PROP_DUMPSTER: return "the dumpster";
    case PROP_TOILET_STALL: return "the stall";
    default: return "it";
    }
}

static NhVec3 eye_pos(const Heist *h) { return nh_player_eye(&h->player, &h->world.L, 1.0f); }

static NhVec3 aim(const Heist *h) { return nh_player_aim_dir(&h->player, gfx_display_aspect(), HEIST_FOVY); }

/* How well the player is aiming at p (cosine), or -1 when out of reach or
 * hidden behind something. */
static float aim_score(const Heist *h, NhVec3 p, float reach)
{
    NhVec3 eye = eye_pos(h);
    NhVec3 to = nh_v3_sub(p, eye);
    float d = nh_v3_len(to);
    if (d > reach || d < 0.05f)
        return -1.0f;
    float c = nh_v3_dot(nh_v3_scale(to, 1.0f / d), aim(h));
    if (c < 0.82f)
        return -1.0f;
    /* Line of sight to just in front of it. */
    NhVec3 q = nh_v3_sub(p, nh_v3_scale(to, 0.35f / d));
    float t;
    if (d > 0.5f && lv_raycast(&h->world.L, eye, q, &t))
        return -1.0f;
    return c - d * 0.04f;
}

static NhVec3 door_center(const LvDoor *d)
{
    return d->axis == 0 ? nh_v3((float)d->x * LV_CELL, lv_floor_y(d->floor) + 1.1f, (float)d->z * LV_CELL + 1.0f)
                        : nh_v3((float)d->x * LV_CELL + 1.0f, lv_floor_y(d->floor) + 1.1f, (float)d->z * LV_CELL);
}

static NhVec3 prop_center(const LvProp *p)
{
    float hy = nh_prop_info[p->type].half.y;
    return nh_v3(p->x, p->y + NH_MIN(hy, 0.6f), p->z);
}

static void set_act(Action *a, int kind, int target, float hold, bool blocked, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));
static void set_act(Action *a, int kind, int target, float hold, bool blocked, const char *fmt, ...)
{
    a->kind = (uint8_t)kind;
    a->target = (int16_t)target;
    a->hold = hold;
    a->blocked = blocked;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->label, sizeof(a->label), fmt, ap);
    va_end(ap);
}

static bool has_objective(const Heist *h, int type, int ent)
{
    for (int i = 0; i < h->state.nobj; i++)
        if (h->state.obj[i].type == type && h->state.obj[i].ent == ent && !h->state.obj[i].done)
            return true;
    return false;
}

static void door_action(Heist *h, int di, Action *a)
{
    const LvDoor *d = &h->world.L.doors[di];
    const NhJob *j = &h->state;
    if (!d->locked) {
        set_act(a, ACT_DOOR, di, 0, false, d->target > 0.5f ? "Close" : "Open");
        return;
    }
    switch (d->lock) {
    case LV_LOCK_PICK:
        set_act(a, ACT_DOOR, di, 0, false, "Pick the lock (level %d)", d->lock_level);
        break;
    case LV_LOCK_KEYCARD:
        if ((j->keycards >> d->key_id) & 1u)
            set_act(a, ACT_DOOR, di, 0.5f, false, "Swipe keycard %d", d->key_id);
        else
            set_act(a, ACT_DOOR, di, 0, true, "Keycard %d needed", d->key_id);
        break;
    case LV_LOCK_CODE:
        if ((j->codes >> d->key_id) & 1u)
            set_act(a, ACT_DOOR, di, 1.0f, false, "Enter code %04u", (unsigned)h->plan.codes[d->key_id % GEN_CODES]);
        else
            set_act(a, ACT_DOOR, di, 0, true, "Keypad: find the code");
        break;
    case LV_LOCK_POWERED:
        set_act(a, ACT_DOOR, di, 0, true, "Maglock: cut the security power");
        break;
    case LV_LOCK_VAULT: {
        bool card = (j->keycards >> d->key_id) & 1u, code = (j->codes >> d->key_id) & 1u;
        if (card && code)
            set_act(a, ACT_DOOR, di, 4.0f, false, "Open the vault");
        else
            set_act(a, ACT_DOOR, di, 0, true, "Vault: needs %s%s%s", card ? "" : "the manager's card",
                    !card && !code ? " + " : "", code ? "" : "the code");
        break;
    }
    default:
        set_act(a, ACT_DOOR, di, 0, true, "Locked");
        break;
    }
}

static bool entity_available(const Heist *h, const NhEntity *e, int i)
{
    if (job_taken(&h->state, i))
        return false;
    if (e->prop >= 0 && (h->world.L.props[e->prop].flags & LV_P_HIDDEN))
        return false;
    /* Loot inside a safe needs the safe open. */
    if (e->kind == ENT_LOOT && e->door >= 0 && e->door < h->plan.nent && h->plan.ent[e->door].kind == ENT_SAFE &&
        !job_taken(&h->state, e->door))
        return false;
    return true;
}

static void entity_action(Heist *h, int i, Action *a)
{
    const NhEntity *e = &h->plan.ent[i];
    const LvProp *p = e->prop >= 0 ? &h->world.L.props[e->prop] : NULL;
    const NhJob *j = &h->state;
    const Level *L = &h->world.L;
    switch (e->kind) {
    case ENT_LOOT: {
        if (has_objective(h, OBJ_PHOTO, i)) {
            set_act(a, ACT_PHOTO, i, 1.5f, false, "Photograph %s", p ? interact_prop_name(p->type) : "it");
            return;
        }
        bool heavy = (e->flags & ENTF_HEAVY) != 0;
        if (heavy && (j->holding >= 0 || j->carrying >= 0)) {
            set_act(a, ACT_LOOT, i, 0, true, "Hands full");
            return;
        }
        if (!heavy && j->bag_items >= j->bag_cap) {
            set_act(a, ACT_LOOT, i, 0, true, "Bag full");
            return;
        }
        set_act(a, ACT_LOOT, i, heavy ? 1.2f : 0.0f, false, "%s %s ($%u)%s", heavy ? "Carry" : "Take",
                p ? interact_prop_name(p->type) : "it", (unsigned)e->value * 10u,
                (e->flags & ENTF_SECURED) && L->power[h->sec.security_circuit] ? "  [alarmed case]" : "");
        return;
    }
    case ENT_KEYCARD:
        set_act(a, ACT_ITEM, i, 0, false, "Take keycard %d", e->key_id);
        return;
    case ENT_CODE:
        set_act(a, ACT_ITEM, i, 0, false, "Read the note");
        return;
    case ENT_SAFE:
        set_act(a, ACT_SAFE, i, 0, false, "Crack the safe");
        return;
    case ENT_TERMINAL:
        set_act(a, ACT_TERMINAL, i, 0, false, has_objective(h, OBJ_WIPE, i) ? "Hack: wipe the logs" : "Hack the terminal");
        return;
    case ENT_CONSOLE:
        set_act(a, ACT_CONSOLE, i, 2.0f, false, h->cams_looped > 0 ? "Feeds looped (%.0fs)" : "Loop the camera feeds",
                (double)h->cams_looped);
        if (h->cams_looped > 0)
            a->blocked = true;
        return;
    case ENT_FUSE_BOX: {
        bool on = L->power[e->key_id & 15];
        bool sec = e->key_id == h->plan.security_circuit;
        set_act(a, ACT_FUSE, i, 2.0f, false, "%s %s", on ? "Cut" : "Restore", sec ? "the security power" : "the lights");
        return;
    }
    case ENT_ALARM_PANEL:
        set_act(a, ACT_PANEL, i, 3.0f, h->sec.disarmed, h->sec.disarmed ? "Alarm disarmed" : "Disarm the alarm");
        return;
    case ENT_PHONE:
        if (has_objective(h, OBJ_BUG, i))
            set_act(a, ACT_PHONE, i, 2.0f, false, "Plant the bug");
        return;
    default:
        return;
    }
}

void interact_find(Heist *h, const GameSettings *gs)
{
    (void)gs;
    Action *a = &h->act;
    memset(a, 0, sizeof(*a));
    a->target = -1;
    NhPlayer *p = &h->player;
    const Level *L = &h->world.L;
    NhJob *j = &h->state;
    if (h->mg != MG_KIND_NONE || h->screen != SCR_PLAY || h->caught_t > 0.0f)
        return;
    if (h->hide_prop >= 0) {
        set_act(a, ACT_HIDE, h->hide_prop, 0, false, "Step out");
        return;
    }
    if (p->mode == NH_MOVE_VENT) {
        int cx = lv_cell_of(p->pos.x), cz = lv_cell_of(p->pos.z);
        const LvCell *c = lv_cell(L, p->floor, cx, cz);
        if (c && (c->flags & LV_C_GRATE))
            set_act(a, ACT_VENT, -1, 0, false, "Drop down");
        return;
    }
    const LvCell *here = lv_cell(L, p->floor, lv_cell_of(p->pos.x), lv_cell_of(p->pos.z));
    float best = -1.0f;
    /* Getaway */
    if (here && (here->flags & LV_C_EXTRACT) && p->floor == 0) {
        set_act(a, ACT_EXTRACT, -1, 1.0f, false, job_main_done(j) ? "Leave in the van" : "Abort: leave without the target");
        return;
    }
    /* Carrying a body: put it down, or hide it. */
    if (j->carrying >= 0) {
        set_act(a, ACT_BODY_DROP, j->carrying, 0, false, "Put the body down");
        for (int i = 0; i < L->nprops; i++) {
            const LvProp *pr = &L->props[i];
            if (!(pr->flags & LV_P_HIDE) || pr->type == PROP_DESK || pr->floor != p->floor)
                continue;
            float s = aim_score(h, prop_center(pr), REACH);
            if (s > best) {
                best = s;
                set_act(a, ACT_BODY_STASH, i, 1.0f, false, "Hide the body in %s", interact_prop_name(pr->type));
            }
        }
        return;
    }
    if (j->holding >= 0)
        set_act(a, ACT_DROP_HEAVY, j->holding, 0, false, "Put it down");
    /* Doors */
    for (int i = 0; i < L->ndoors; i++) {
        const LvDoor *d = &L->doors[i];
        if (d->floor != p->floor)
            continue;
        float s = aim_score(h, door_center(d), REACH + 0.3f);
        if (s > best) {
            best = s;
            door_action(h, i, a);
        }
    }
    /* Entities */
    for (int i = 0; i < h->plan.nent; i++) {
        const NhEntity *e = &h->plan.ent[i];
        if (e->prop < 0 || e->floor != p->floor || !entity_available(h, e, i))
            continue;
        if (e->kind != ENT_LOOT && e->kind != ENT_KEYCARD && e->kind != ENT_CODE && e->kind != ENT_SAFE &&
            e->kind != ENT_TERMINAL && e->kind != ENT_CONSOLE && e->kind != ENT_FUSE_BOX &&
            e->kind != ENT_ALARM_PANEL && e->kind != ENT_PHONE)
            continue;
        float s = aim_score(h, prop_center(&L->props[e->prop]), REACH);
        if (s < 0.0f)
            continue;
        s += 0.02f; /* small things win ties */
        if (s > best) {
            Action tmp = *a;
            memset(a, 0, sizeof(*a));
            entity_action(h, i, a);
            if (a->kind == ACT_NOTHING)
                *a = tmp;
            else
                best = s;
        }
    }
    /* Knocked-out guards */
    for (int i = 0; i < h->ai.n; i++) {
        const AiGuard *g = &h->ai.g[i];
        if (!g->active || g->state != AS_KO || g->floor != p->floor)
            continue;
        float s = aim_score(h, nh_v3_add(g->pos, nh_v3(0, 0.3f, 0)), REACH);
        if (s > best && j->holding < 0) {
            best = s;
            set_act(a, ACT_BODY_PICK, i, 0.6f, false, g->arch == ARCH_DOG ? "Drag the dog" : "Pick up the body");
        }
    }
    /* Hiding spots */
    for (int i = 0; i < L->nprops; i++) {
        const LvProp *pr = &L->props[i];
        if (!(pr->flags & LV_P_HIDE) || pr->floor != p->floor || (pr->flags & LV_P_HIDDEN))
            continue;
        if (pr->type == PROP_DESK && p->crouch < 0.5f)
            continue; /* crouch to get under a desk */
        float s = aim_score(h, prop_center(pr), REACH);
        if (s < 0.0f)
            continue;
        s -= 0.05f;
        if (s > best && j->holding < 0) {
            best = s;
            set_act(a, ACT_HIDE, i, 0.3f, false, pr->type == PROP_DESK ? "Hide under the desk" : "Hide in %s",
                    interact_prop_name(pr->type));
        }
    }
    /* Ladder, hatch, vents */
    for (int i = 0; i < h->plan.nent; i++) {
        const NhEntity *e = &h->plan.ent[i];
        if (e->kind == ENT_LADDER) {
            if (p->floor == 0 && nh_v3_dist_xz(p->pos, e->pos) < 1.6f && best < 0.9f) {
                set_act(a, ACT_LADDER_UP, i, 0.8f, j->holding >= 0, "Climb to the roof");
                best = 0.9f;
            }
            float roof_y = lv_floor_y(L->floors);
            NhVec3 top = nh_v3(e->pos.x, roof_y, e->pos.z);
            if (fabsf(p->pos.y - roof_y) < 0.5f && nh_v3_dist_xz(p->pos, top) < 2.4f && best < 0.9f) {
                set_act(a, ACT_LADDER_DOWN, i, 0.8f, j->holding >= 0, "Climb down the ladder");
                best = 0.9f;
            }
        } else if (e->kind == ENT_HATCH) {
            float roof_y = lv_floor_y(L->floors);
            if (p->floor == L->floors - 1 && nh_v3_dist_xz(p->pos, e->pos) < 1.0f && best < 0.95f) {
                set_act(a, ACT_HATCH_UP, i, 0.8f, j->holding >= 0, "Climb through the hatch");
                best = 0.95f;
            }
            if (fabsf(p->pos.y - roof_y) < 0.5f && nh_v3_dist_xz(p->pos, e->pos) < 1.0f && best < 0.95f) {
                set_act(a, ACT_HATCH_DOWN, i, 0.5f, false, "Drop through the hatch");
                best = 0.95f;
            }
        }
    }
    if (here && (here->flags & (LV_C_GRATE | LV_C_VENT)) == (LV_C_GRATE | LV_C_VENT) && best < 0.9f &&
        j->holding < 0) {
        NhVec3 ctr = lv_cell_center(p->floor, lv_cell_of(p->pos.x), lv_cell_of(p->pos.z));
        if (nh_v3_dist_xz(ctr, p->pos) < 0.9f)
            set_act(a, ACT_VENT, -1, 0.5f, false, "Climb into the vent");
    }
}

bool interact_busy(const Heist *h) { return h->mg != MG_KIND_NONE || h->hide_prop >= 0; }

void interact_unhide(Heist *h)
{
    if (h->hide_prop < 0)
        return;
    nh_player_init(&h->player, h->hide_return, h->player.yaw);
    h->hide_prop = -1;
}

void interact_carry_tick(Heist *h)
{
    int gi = h->state.carrying;
    if (gi < 0)
        return;
    AiGuard *g = &h->ai.g[gi];
    NhVec3 back = nh_dir_from_yaw_pitch(h->player.yaw, 0);
    g->pos = nh_v3(h->player.pos.x - back.x * 0.55f, h->player.pos.y, h->player.pos.z - back.z * 0.55f);
    g->yaw = h->player.yaw;
    g->floor = h->player.floor;
    for (int i = 0; i < h->ai.nan; i++)
        if (h->ai.an[i].active && h->ai.an[i].kind == AN_BODY && h->ai.an[i].id == gi)
            h->ai.an[i].pos = nh_v3_add(g->pos, nh_v3(0, 0.3f, 0));
}

/* ---- performing actions ---------------------------------------------------------- */

static void open_door(Heist *h, int di)
{
    LvDoor *d = &h->world.L.doors[di];
    NhVec3 c = door_center(d);
    if (d->locked) {
        int s = d->lock == LV_LOCK_KEYCARD ? SFX_SWIPE : d->lock == LV_LOCK_CODE ? SFX_KEYPAD_OK : SFX_UNLOCK;
        audio_at(s, c, 0.8f, 12.0f);
    }
    d->locked = 0;
    d->target = d->target > 0.5f ? 0.0f : 1.0f;
    if (d->kind == LV_DOOR_VAULT)
        audio_at(SFX_DOOR_VAULT, c, 1.0f, 30.0f);
    else
        audio_at(d->target > 0.5f ? SFX_DOOR_OPEN : SFX_DOOR_CLOSE, c, 0.8f, 18.0f);
    if (d->target > 0.5f) {
        ai_add_anomaly(&h->ai, AN_DOOR_OPEN, c, di);
        if (d->flags & LV_DOOR_F_EXTERIOR)
            sec_door_opened(&h->sec, &h->world.L, di);
        if (h->sec.entry_door == di)
            heist_status(h, "Door alarm: disarm the panel in 15 s!");
    } else {
        ai_clear_anomaly(&h->ai, AN_DOOR_OPEN, di);
    }
    heist_noise(h, c, d->kind == LV_DOOR_METAL || d->kind == LV_DOOR_VAULT ? 6.0f : 4.0f, NH_NOISE_DOOR);
}

static void take_loot(Heist *h, int i)
{
    NhEntity *e = &h->plan.ent[i];
    LvProp *p = e->prop >= 0 ? &h->world.L.props[e->prop] : NULL;
    if (p)
        p->flags |= LV_P_HIDDEN;
    ai_add_anomaly(&h->ai, AN_LOOT_MISSING, e->pos, i);
    if ((e->flags & ENTF_SECURED) && h->world.L.power[h->sec.security_circuit & 15])
        heist_alarm(h, e->pos, "Display case alarm!");
    audio_play(e->flags & ENTF_HEAVY ? SFX_PICKUP_HEAVY : SFX_PICKUP, 0.6f, 0.0f, 1.0f);
    if (e->flags & ENTF_HEAVY) {
        h->state.holding = (int16_t)i;
        job_mark_taken(&h->state, i);
        heist_status(h, "Carrying it: get it to the van");
        return;
    }
    job_event(&h->state, &h->plan, JE_LOOT, i, e->value);
    char buf[64];
    snprintf(buf, sizeof(buf), "+$%u", (unsigned)e->value * 10u);
    heist_status(h, buf);
}

static void open_safe(Heist *h, int i)
{
    job_mark_taken(&h->state, i);
    job_event(&h->state, &h->plan, JE_SAFE_OPEN, i, 0);
    ai_add_anomaly(&h->ai, AN_SAFE_OPEN, h->plan.ent[i].pos, i);
    audio_at(SFX_SAFE_OPEN, h->plan.ent[i].pos, 0.9f, 15.0f);
    heist_status(h, "Safe open");
}

static void perform(Heist *h, const Action *a)
{
    Level *L = &h->world.L;
    NhJob *j = &h->state;
    NhPlayer *p = &h->player;
    switch (a->kind) {
    case ACT_DOOR: {
        LvDoor *d = &L->doors[a->target];
        if (d->locked && d->lock == LV_LOCK_PICK) {
            h->mg = MG_KIND_LOCK;
            h->mg_target = a->target;
            lp_init(&h->lp, d->lock_level, &h->rng);
            h->lp.npins = NH_MAX(2, h->lp.npins - h->perks.pick_bonus); /* picks upgrade, locksmith */
            return;
        }
        open_door(h, a->target);
        break;
    }
    case ACT_LOOT:
        take_loot(h, a->target);
        break;
    case ACT_ITEM: {
        NhEntity *e = &h->plan.ent[a->target];
        job_mark_taken(j, a->target);
        if (e->prop >= 0)
            L->props[e->prop].flags |= LV_P_HIDDEN;
        audio_play(SFX_PICKUP, 0.4f, 0.0f, 1.3f);
        char buf[64];
        if (e->kind == ENT_KEYCARD) {
            j->keycards |= 1u << (e->key_id & 31);
            snprintf(buf, sizeof(buf), "Got keycard %d", e->key_id);
        } else {
            j->codes |= 1u << (e->key_id & 31);
            snprintf(buf, sizeof(buf), "Code noted: %04u", (unsigned)h->plan.codes[e->key_id % GEN_CODES]);
        }
        heist_status(h, buf);
        break;
    }
    case ACT_SAFE:
        h->mg = MG_KIND_SAFE;
        h->mg_target = a->target;
        sc_init(&h->sc, h->plan.ent[a->target].tier, &h->rng);
        break;
    case ACT_TERMINAL:
        h->mg = MG_KIND_HACK;
        h->mg_target = a->target;
        hk_init(&h->hk, NH_MAX(1, h->plan.ent[a->target].tier), &h->rng);
        h->hk.layers = NH_MAX(1, h->hk.layers - h->perks.hack_bonus);
        break;
    case ACT_CONSOLE:
        h->cams_looped = 120.0f;
        audio_play(SFX_HACK_OK, 0.5f, 0.0f, 1.0f);
        heist_status(h, "Camera feeds looped for 2 minutes");
        break;
    case ACT_FUSE: {
        const NhEntity *e = &h->plan.ent[a->target];
        bool on = L->power[e->key_id & 15];
        heist_set_power(h, e->key_id, !on);
        audio_at(on ? SFX_POWER_DOWN : SFX_POWER_UP, e->pos, 0.9f, 25.0f);
        if (on)
            ai_add_anomaly(&h->ai, AN_LIGHTS_OFF, e->pos, e->key_id);
        else
            ai_clear_anomaly(&h->ai, AN_LIGHTS_OFF, e->key_id);
        heist_noise(h, e->pos, 3.0f, NH_NOISE_GADGET);
        break;
    }
    case ACT_PANEL:
        sec_disarm(&h->sec);
        audio_play(SFX_KEYPAD_OK, 0.6f, 0.0f, 1.0f);
        heist_status(h, "Alarm panel disarmed");
        break;
    case ACT_PHONE:
        job_event(j, &h->plan, JE_BUG, a->target, 0);
        audio_play(SFX_PICK_SET, 0.5f, 0.0f, 0.8f);
        heist_status(h, "Bug planted");
        break;
    case ACT_PHOTO:
        job_event(j, &h->plan, JE_PHOTO, a->target, 0);
        audio_play(SFX_TORCH, 0.6f, 0.0f, 0.7f);
        heist_status(h, "Photographed");
        break;
    case ACT_HIDE:
        audio_play(SFX_HIDE, 0.5f, 0.0f, 1.0f);
        if (h->hide_prop >= 0) {
            interact_unhide(h);
        } else {
            const LvProp *pr = &L->props[a->target];
            h->hide_prop = a->target;
            h->hide_return = p->pos;
            p->pos = p->prev_pos = nh_v3(pr->x, lv_floor_y(pr->floor), pr->z);
            p->yaw = p->prev_yaw = pr->yaw;
            p->pitch = p->prev_pitch = 0.0f;
            heist_status(h, "Hidden. C to step out");
        }
        break;
    case ACT_BODY_PICK:
        j->carrying = a->target;
        ai_carry(&h->ai, a->target, true);
        audio_play(SFX_PICKUP_HEAVY, 0.5f, 0.0f, 0.9f);
        break;
    case ACT_BODY_DROP:
        ai_carry(&h->ai, j->carrying, false);
        j->carrying = -1;
        audio_play(SFX_BODY_THUD, 0.6f, 0.0f, 1.0f);
        break;
    case ACT_BODY_STASH: {
        int gi = j->carrying;
        ai_carry(&h->ai, gi, false);
        h->ai.g[gi].active = 0;
        ai_clear_anomaly(&h->ai, AN_BODY, gi);
        j->carrying = -1;
        audio_play(SFX_HIDE, 0.5f, 0.0f, 0.8f);
        heist_status(h, "Body hidden");
        break;
    }
    case ACT_DROP_HEAVY: {
        NhEntity *e = &h->plan.ent[j->holding];
        NhVec3 f = nh_dir_from_yaw_pitch(p->yaw, 0);
        if (e->prop >= 0) {
            LvProp *pr = &L->props[e->prop];
            pr->x = p->pos.x + f.x * 0.8f;
            pr->z = p->pos.z + f.z * 0.8f;
            pr->y = p->pos.y;
            pr->floor = (uint8_t)p->floor;
            pr->room = (uint8_t)lv_room_at(L, p->floor, lv_cell_of(pr->x), lv_cell_of(pr->z));
            pr->flags &= (uint8_t)~LV_P_HIDDEN;
            e->pos = nh_v3(pr->x, pr->y, pr->z);
            e->floor = (uint8_t)p->floor;
            e->room = pr->room;
        }
        j->taken[j->holding >> 3] &= (uint8_t)~(1u << (j->holding & 7));
        j->holding = -1;
        e->flags &= (uint8_t)~ENTF_SECURED;
        audio_play(SFX_BODY_THUD, 0.6f, 0.0f, 1.2f);
        break;
    }
    case ACT_LADDER_UP: {
        const NhEntity *e = &h->plan.ent[a->target];
        NhVec3 in = nh_dir_from_yaw_pitch(e->yaw, 0); /* ladder faces out from the wall */
        NhVec3 top = nh_v3(e->pos.x - in.x * 1.2f, lv_floor_y(L->floors), e->pos.z - in.z * 1.2f);
        nh_player_init(p, top, p->yaw);
        audio_play(SFX_STEP_METAL, 0.5f, 0.0f, 1.0f);
        heist_status(h, "On the roof");
        break;
    }
    case ACT_LADDER_DOWN: {
        const NhEntity *e = &h->plan.ent[a->target];
        NhVec3 out = nh_dir_from_yaw_pitch(e->yaw, 0);
        nh_player_init(p, nh_v3(e->pos.x + out.x * 0.7f, 0.0f, e->pos.z + out.z * 0.7f), p->yaw);
        break;
    }
    case ACT_HATCH_UP: {
        const NhEntity *e = &h->plan.ent[a->target];
        nh_player_init(p, nh_v3(e->pos.x + 0.9f, lv_floor_y(L->floors), e->pos.z), p->yaw);
        audio_play(SFX_DOOR_CLOSE, 0.5f, 0.0f, 1.3f);
        heist_status(h, "On the roof");
        break;
    }
    case ACT_HATCH_DOWN: {
        const NhEntity *e = &h->plan.ent[a->target];
        nh_player_init(p, nh_v3(e->pos.x, lv_floor_y(L->floors - 1), e->pos.z), p->yaw);
        audio_play(SFX_LAND, 0.6f, 0.0f, 1.0f);
        p->crouch = 1.0f;
        break;
    }
    case ACT_VENT: {
        int cx = lv_cell_of(p->pos.x), cz = lv_cell_of(p->pos.z);
        NhVec3 ctr = lv_cell_center(p->floor, cx, cz);
        if (p->mode == NH_MOVE_WALK) {
            p->mode = NH_MOVE_VENT;
            audio_play(SFX_STEP_METAL, 0.6f, 0.0f, 0.8f);
            p->pos = p->prev_pos = nh_v3(ctr.x, lv_floor_y(p->floor) + LV_CEIL + 0.05f, ctr.z);
            heist_status(h, "In the vents");
        } else {
            p->mode = NH_MOVE_WALK;
            p->pos = p->prev_pos = nh_v3(ctr.x, lv_floor_y(p->floor) + 0.4f, ctr.z);
            p->grounded = false;
            p->crouch = 1.0f;
            audio_play(SFX_LAND, 0.6f, 0.0f, 1.0f);
            heist_noise(h, ctr, 3.0f, NH_NOISE_GADGET);
        }
        break;
    }
    case ACT_EXTRACT:
        if (j->holding >= 0) {
            const NhEntity *e = &h->plan.ent[j->holding];
            job_event(j, &h->plan, JE_LOOT, j->holding, e->value);
            j->holding = -1;
        }
        job_event(j, &h->plan, JE_EXTRACT, -1, 0);
        audio_play(SFX_VAN, 0.8f, 0.0f, 1.0f);
        job_result(j, &h->result);
        h->screen = SCR_SUMMARY;
        break;
    default:
        break;
    }
}

/* ---- per tick ------------------------------------------------------------------------ */

static void finish_minigame(Heist *h, bool ok)
{
    NhJob *j = &h->state;
    switch (h->mg) {
    case MG_KIND_LOCK:
        if (ok) {
            j->locks_picked++;
            open_door(h, h->mg_target);
        }
        break;
    case MG_KIND_SAFE:
        if (ok)
            open_safe(h, h->mg_target);
        break;
    case MG_KIND_HACK: {
        const NhEntity *e = &h->plan.ent[h->mg_target];
        if (ok) {
            job_mark_taken(j, h->mg_target);
            bool wipe = false;
            for (int i = 0; i < j->nobj; i++)
                wipe |= j->obj[i].type == OBJ_WIPE && j->obj[i].ent == h->mg_target;
            job_event(j, &h->plan, wipe ? JE_WIPED : JE_HACKED, h->mg_target, 0);
            /* The building's door codes come with it. */
            for (int c = 0; c < GEN_CODES; c++)
                if (h->plan.codes[c])
                    j->codes |= 1u << c;
            heist_status(h, wipe ? "Logs wiped. Door codes downloaded" : "Files downloaded. Door codes too");
        } else {
            heist_status(h, "Trace! Security is on the way");
            ai_report(&h->ai, e->pos);
        }
        break;
    }
    default:
        break;
    }
    h->mg = MG_KIND_NONE;
}

void interact_update(Heist *h, const NhInput *in, const GameSettings *gs)
{
    const float dt = NH_TICK_DT;
    bool c_held = (in->held & NH_BTN_C) != 0, c_pressed = (in->pressed & NH_BTN_C) != 0;
    /* Minigames */
    if (h->mg != MG_KIND_NONE) {
        if (in->pressed & NH_BTN_B) {
            h->mg = MG_KIND_NONE;
            return;
        }
        bool a = (in->pressed & NH_BTN_A) != 0;
        MgEvent ev = MG_NONE;
        if (h->mg == MG_KIND_LOCK) {
            ev = lp_tick(&h->lp, in->remote_roll, a, dt);
            if (ev == MG_SLIP)
                heist_noise(h, h->player.pos, h->perks.noisy_slips ? 5.0f : 3.0f, NH_NOISE_GADGET);
            if (ev == MG_TICK || ev == MG_SET)
                h->rumble_ticks = NH_MAX(h->rumble_ticks, ev == MG_SET ? 6 : 2);
        } else if (h->mg == MG_KIND_SAFE) {
            ev = sc_tick(&h->sc, in->remote_roll, a, dt);
            if (ev == MG_TICK || ev == MG_SET)
                h->rumble_ticks = NH_MAX(h->rumble_ticks, ev == MG_SET ? 8 : 4);
        } else {
            float px = (in->ir_x - 170.0f) / 300.0f, py = (in->ir_y - 120.0f) / 240.0f;
            ev = hk_tick(&h->hk, NH_CLAMP(px, 0.0f, 1.0f), NH_CLAMP(py, 0.0f, 1.0f), in->ir_valid, dt);
        }
        static const int k_mg_sfx[3][6] = {
            /* NONE, TICK, SET, SLIP, DONE, FAIL */
            [0] = { -1, SFX_PICK_TICK, SFX_PICK_SET, SFX_PICK_SLIP, -1, SFX_PICK_SLIP },
            [1] = { -1, SFX_SAFE_CLICK, SFX_SAFE_SET, SFX_SAFE_SET, -1, SFX_UI_DENY },
            [2] = { -1, SFX_HACK_BEEP, SFX_HACK_OK, SFX_UI_DENY, SFX_HACK_DONE, SFX_HACK_FAIL },
        };
        int mk = h->mg == MG_KIND_LOCK ? 0 : h->mg == MG_KIND_SAFE ? 1 : 2;
        if (ev > MG_NONE && ev <= MG_FAIL && k_mg_sfx[mk][ev] >= 0)
            audio_play(k_mg_sfx[mk][ev], ev == MG_TICK ? 0.35f : 0.7f, 0.0f, 1.0f);
        if (ev == MG_DONE)
            finish_minigame(h, true);
        else if (ev == MG_FAIL)
            finish_minigame(h, false);
        return;
    }
    (void)gs;
    const Action *a = &h->act;
    if (!c_held)
        h->c_latch = false;
    if (h->c_latch)
        return;
    if (a->kind == ACT_NOTHING || a->blocked) {
        if (a->blocked && c_pressed)
            audio_play(a->kind == ACT_DOOR ? SFX_DOOR_LOCKED : SFX_UI_DENY, 0.6f, 0.0f, 1.0f);
        h->hold_t = 0.0f;
        return;
    }
    if (a->hold <= 0.0f) {
        if (c_pressed) {
            Action copy = *a;
            h->c_latch = true;
            perform(h, &copy);
        }
        h->hold_t = 0.0f;
        return;
    }
    if (c_held) {
        /* Keypads and panels beep as the digits go in. */
        bool keys = a->kind == ACT_PANEL || (a->kind == ACT_DOOR && (h->world.L.doors[a->target].lock == LV_LOCK_CODE ||
                                                                     h->world.L.doors[a->target].lock == LV_LOCK_VAULT));
        if (keys && (int)(h->hold_t / 0.22f) != (int)((h->hold_t + dt) / 0.22f))
            audio_play(SFX_KEYPAD_BEEP, 0.4f, 0.0f, 0.9f + 0.05f * (float)((int)(h->hold_t / 0.22f) % 4));
        h->hold_t += dt;
        if (h->hold_t >= a->hold) {
            Action copy = *a;
            h->hold_t = 0.0f;
            h->c_latch = true;
            perform(h, &copy);
        }
    } else {
        h->hold_t = 0.0f;
    }
}
