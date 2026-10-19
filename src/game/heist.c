/* Night Heist - the heist loop: player, guards, security, interactions,
 * gadgets, the police and the getaway. */
#include "heist.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "core/nh_fixedstep.h"
#include "core/nh_proptab.h"
#include "gadgets.h"
#include "guide.h"
#include "hud.h"
#include "interact.h"
#include "res.h"
#include "testlevel.h"
#include "text.h"

static GfxTexture s_cookie;

static const char *const k_barks[BARK_COUNT] = {
    [BARK_HUH] = "Huh?",
    [BARK_WHO] = "Who's there?",
    [BARK_THERE] = "There! Stop right there!",
    [BARK_LOST] = "Lost them...",
    [BARK_NOTHING] = "Must've been nothing.",
    [BARK_RADIO_SPOTTED] = "Control, intruder on site!",
    [BARK_MAN_DOWN] = "Man down! Man down!",
    [BARK_CHECKIN] = "All units, check in.",
    [BARK_CHECKIN_OK] = "All clear here.",
    [BARK_DOOR] = "Who left this open?",
    [BARK_LOOT] = "Something's missing...",
    [BARK_LIGHTS] = "Why are the lights off?",
    [BARK_CAMERA] = "Camera's down. Checking it.",
    [BARK_NOISE] = "What was that?",
    [BARK_CHAT] = "...and then he says...",
    [BARK_FOUND] = "Gotcha!",
    [BARK_GIVEUP] = "Nobody here. Back to it.",
    [BARK_DOG] = "*barking*",
    [BARK_SCREAM] = "Help! Security!",
    [BARK_POLICE] = "Police! Freeze!",
};

static void spawn_guards(Heist *h)
{
    ai_init(&h->ai, &h->world.L, h->nav, h->world.L.seed ^ 0x6A4Du);
    if (!h->generated) {
        testlevel_guards(&h->ai);
    } else {
        int map[GEN_GUARD_MAX];
        for (int i = 0; i < h->plan.nguards; i++) {
            const NhGuardPlan *g = &h->plan.guards[i];
            map[i] = ai_add_guard(&h->ai, g->arch, g->pos, g->yaw, g->route, g->nroute);
        }
        for (int i = 0; i < h->plan.nguards; i++) {
            int p = h->plan.guards[i].partner;
            if (map[i] >= 0 && p >= 0 && map[p] >= 0) {
                h->ai.g[map[i]].partner = (int8_t)map[p];
                if (h->plan.guards[i].arch == ARCH_DOG)
                    h->ai.g[map[i]].state = AS_PATROL;
            }
        }
    }
    guards_vis_init(&h->gv, &h->ai);
    h->me->takedown = -1;
}

static NhVec3 start_pos(const Heist *h, float *yaw)
{
    if (!h->generated)
        return testlevel_start(yaw);
    *yaw = h->plan.entry_yaw[0];
    return h->plan.entry[0];
}

static void set_perks(Heist *h, const NhCampaign *c, const NhJobOffer *o)
{
    HeistPerks *k = &h->perks;
    memset(k, 0, sizeof(*k));
    k->carry_speed = 0.6f;
    k->torch_life = 1.0f;
    k->torch_fov = 48.0f;
    k->thermal_life = 1.0f;
    k->dart_time = 3.5f;
    k->noise_scale = 1.0f;
    if (!c || !o)
        return;
    k->pick_bonus = c->upgrade[UP_PICKS] + (camp_crew_on(c, CR_LOCKSMITH) ? 1 : 0);
    k->hack_bonus = camp_crew_on(c, CR_HACKER) ? 1 : 0;
    k->silent_takedowns = camp_crew_on(c, CR_MUSCLE);
    k->carry_speed = camp_crew_on(c, CR_MUSCLE) ? 0.85f : 0.6f;
    k->map_guards = camp_crew_on(c, CR_LOOKOUT);
    k->map_objectives = c->upgrade[UP_SCANNER] > 0;
    k->noisy_slips = camp_crew_on(c, CR_LOCKSMITH) && !camp_crew_perk2(c, CR_LOCKSMITH);
    k->torch_life = 1.0f + 0.5f * (float)c->upgrade[UP_TORCH];
    k->torch_fov = 48.0f + 5.0f * (float)c->upgrade[UP_TORCH];
    k->thermal_life = c->upgrade[UP_THERMAL] ? 2.0f : 1.0f;
    k->dart_time = c->upgrade[UP_DARTS] ? 1.5f : 3.5f;
    k->noise_scale = 1.0f - 0.2f * (float)c->upgrade[UP_SHOES];
    if (o->mods & (1u << MOD_STORM))
        k->noise_scale *= 0.6f;
}

bool heist_start(Heist *h, const NhGenParams *job, const NhCampaign *camp, const NhJobOffer *offer, int difficulty,
                 int players, NhArena *level_arena, NhArena *scratch)
{
    memset(h, 0, sizeof(*h));
    h->nplayers = NH_CLAMP(players, 1, NH_MAX_PLAYERS);
    h->me = &h->pl[0];
    set_perks(h, camp, offer);
    if (offer) {
        h->campaign = true;
        h->offer = *offer;
        h->screen = SCR_BRIEF; /* the job card first */
    }
    if (camp)
        h->tips_seen = camp->tips_seen;
    if (job) {
        h->generated = true;
        h->job = *job;
        if (!nh_gen_building(&h->world.L, job, &h->plan))
            return false;
        snprintf(h->job_name, sizeof(h->job_name), "%s %s #%u", nh_size_names[job->size],
                 nh_building_names[job->type], (unsigned)job->seed);
    } else {
        testlevel_build(&h->world.L);
        h->world.L.doors[0].open = h->world.L.doors[0].target = 1.0f;
        h->world.L.doors[0].locked = 0;
        h->world.L.doors[1].open = h->world.L.doors[1].target = 1.0f;
        h->plan.objective = -1;
        snprintf(h->job_name, sizeof(h->job_name), "test building");
    }
    if (!world_build(&h->world, level_arena, scratch))
        return false;
    float yaw;
    NhVec3 start = start_pos(h, &yaw);
    float dx, dy, dz, dyaw, dpitch = 0.0f;
    bool debug_start = plat_debug_start(&dx, &dy, &dz, &dyaw, &dpitch);
    for (int i = 0; i < h->nplayers; i++) {
        HeistPlayer *me = &h->pl[i];
        me->index = i;
        /* Player 2 starts a step to the right of player 1. */
        NhVec3 right = nh_v3(cosf(yaw), 0.0f, -sinf(yaw));
        NhVec3 at = nh_v3_add(debug_start ? nh_v3(dx, dy, dz) : start, nh_v3_scale(right, 0.9f * (float)i));
        nh_player_init(&me->player, at, debug_start ? dyaw : yaw);
        if (debug_start)
            me->player.pitch = me->player.prev_pitch = dpitch;
        lv_collide_cylinder(&h->world.L, &me->player.pos, 0.3f, me->player.pos.y + 0.4f, me->player.pos.y + 1.6f);
        me->player.prev_pos = me->player.pos;
        me->torch_on = true;
        me->battery = 1.0f;
        me->thermal_battery = 1.0f;
        me->was_torch = true;
        me->torch_dir = nh_dir_from_yaw_pitch(me->player.yaw, 0);
        me->hide_prop = -1;
        me->holding = me->carrying = -1;
        me->takedown = -1;
        me->aspect = gfx_display_aspect() * (h->nplayers > 1 ? 2.0f : 1.0f);
    }
    h->snd_alarm = h->snd_siren = h->snd_drill = h->snd_phone = -1;
    for (int i = 0; i < (int)NH_ARRAY_LEN(h->snd_proj); i++)
        h->snd_proj[i] = -1;
    s_cookie = res_texture("cookie");
    h->nav = nh_arena_alloc(level_arena, sizeof(NhNav));
    if (!h->nav)
        return false;
    nh_nav_build(h->nav, &h->world.L, 0.3f);
    spawn_guards(h);
    nh_rng_seed(&h->rng, h->world.L.seed, 0x1A7u);
    /* Guards' eyes: forgiving by default, the old values on hard. */
    h->difficulty = NH_CLAMP(difficulty, 0, 2);
    h->ai.difficulty = difficulty == 0 ? 0.45f : difficulty == 2 ? 1.0f : 0.65f;
    for (int i = 0; i < h->nplayers; i++)
        h->pl[i].escapes_left = difficulty == 0 ? 3 : difficulty == 2 ? 1 : 2;
    if (h->generated) {
        job_init(&h->state, &h->world.L, &h->plan, h->world.L.seed);
        h->state.police_delay = 100.0f - 12.0f * (float)h->job.security;
        if (camp && offer)
            camp_prepare_job(camp, offer, &h->state);
        if (offer)
            snprintf(h->job_name, sizeof(h->job_name), "%s", offer->name);
    } else {
        memset(&h->state, 0, sizeof(h->state));
        h->state.police_eta = -1.0f;
        h->state.bag_cap = 12;
        for (int g = 0; g < GD_COUNT; g++)
            h->state.charges[g] = nh_gadgets[g].charges;
    }
    for (int i = 0; i < h->nplayers; i++)
        if (!h->state.charges[GD_THERMAL])
            h->pl[i].thermal_battery = 0.0f; /* goggles not in the kit yet */
    if (h->nplayers > 1)
        h->state.bag_cap = h->state.bag_cap * 3 / 2; /* two bags */
    sec_init(&h->sec, &h->world.L, &h->plan);
    return true;
}

UiMenu heist_pause_menu(const Heist *h)
{
    static const char *const items[] = { "RESUME", "CONTROLS", "ABANDON THE JOB" };
    (void)h;
    return (UiMenu){ 200, 200, 240, 34, 2, 3, items, NULL };
}

static void stop_loops(Heist *h)
{
    audio_stop_effects();
    h->snd_alarm = h->snd_siren = h->snd_drill = h->snd_phone = -1;
    for (int i = 0; i < (int)NH_ARRAY_LEN(h->snd_proj); i++)
        h->snd_proj[i] = -1;
}

void heist_end(Heist *h)
{
    stop_loops(h);
    world_free(&h->world);
}

void heist_status(Heist *h, const char *msg)
{
    snprintf(h->me->status, sizeof(h->me->status), "%s", msg);
    h->me->status_ticks = 150;
}

void heist_status_all(Heist *h, const char *msg)
{
    for (int i = 0; i < h->nplayers; i++) {
        snprintf(h->pl[i].status, sizeof(h->pl[i].status), "%s", msg);
        h->pl[i].status_ticks = 150;
    }
}

void heist_noise(Heist *h, NhVec3 pos, float radius, int kind)
{
    NhPlayer *p = &h->me->player;
    if (p->nnoise < (int)NH_ARRAY_LEN(p->noises))
        p->noises[p->nnoise++] = (NhNoise){ pos, radius, (uint8_t)kind, 0, 1 };
}

void heist_alarm(Heist *h, NhVec3 where, const char *why)
{
    if (!h->ai.alarm)
        ai_raise_alarm(&h->ai, where);
    if (!h->state.alarm) {
        job_event(&h->state, &h->plan, JE_ALARM, -1, 0);
        heist_status_all(h, why);
        for (int i = 0; i < h->nplayers; i++)
            h->pl[i].rumble_ticks = 30;
    } else if (h->nplayers > 1) {
        heist_status_all(h, why);
    }
}

void heist_set_power(Heist *h, int circuit, bool on)
{
    Level *L = &h->world.L;
    circuit &= 15;
    if ((L->power[circuit] != 0) == on)
        return;
    L->power[circuit] = on;
    /* Re-light the rooms on that circuit over the next frames. */
    for (int i = 0; i < L->nlights; i++)
        if ((L->lights[i].circuit & 15) == circuit && L->lights[i].room)
            h->rebake[L->lights[i].room] = 1;
    if (circuit == h->plan.security_circuit) {
        for (int i = 0; i < L->ndoors; i++)
            if (L->doors[i].lock == LV_LOCK_POWERED)
                L->doors[i].locked = on;
        heist_status(h, on ? "Security power back on" : "Security power cut: cameras, lasers and maglocks down");
    } else {
        heist_status(h, on ? "Lights back on" : "Lights out");
    }
    h->fuse_restore_t[circuit] = 0.0f;
}

void heist_camera(const Heist *h, float alpha, NhMat34 *view, NhVec3 *eye)
{
    const NhPlayer *p = &h->me->player;
    *eye = nh_player_eye(p, &h->world.L, alpha);
    float yaw = p->prev_yaw + nh_wrap_angle(p->yaw - p->prev_yaw) * alpha;
    float pitch = nh_lerpf(p->prev_pitch, p->pitch, alpha);
    float lean = nh_lerpf(p->prev_lean, p->lean, alpha);
    NhMat34 cam, rz;
    nh_m34_from_yaw_pitch(&cam, *eye, yaw, pitch);
    nh_m34_rot_z(&rz, -lean * NH_DEG2RAD(9.0f));
    nh_m34_mul(&cam, &cam, &rz);
    nh_m34_inverse(view, &cam);
}

static void subtitle(Heist *h, const char *msg)
{
    if (!h->subtitles)
        return;
    snprintf(h->subtitle, sizeof(h->subtitle), "%s", msg);
    h->subtitle_ticks = 150;
}

static void fill_player_view(Heist *h)
{
    NhPlayer *p = &h->me->player;
    const Level *L = &h->world.L;
    AiPlayerView *pv = &h->me->pv;
    pv->pos = p->pos;
    pv->head = nh_player_eye(p, L, 1.0f);
    pv->height = nh_player_eye_height(p) + 0.1f;
    pv->speed = p->speed;
    /* A lit torch makes the player much easier to spot. */
    pv->light = NH_MAX(h->me->light_level, h->me->torch_on ? 0.4f : 0.0f);
    pv->crouched = p->crouch > 0.5f;
    pv->torch_on = h->me->torch_on;
    pv->in_vent = p->mode == NH_MOVE_VENT;
    pv->disguised = false;
    pv->hiding_prop = h->me->hide_prop;
    pv->cuffed = h->me->cuffed || h->me->caught_t > 0.0f || h->me->grab_t > 0.0f;
    pv->beam_valid = false;
    if (h->me->torch_on && !pv->in_vent && h->me->hide_prop < 0) {
        NhVec3 a = pv->head, b = nh_v3_add(a, nh_v3_scale(h->me->torch_dir, 20.0f));
        float t;
        if (lv_raycast(L, a, b, &t)) {
            pv->beam_valid = true;
            pv->beam_hit = nh_v3_lerp(a, b, t);
        }
    }
    pv->noises = p->noises;
    pv->nnoise = p->nnoise;
}

static void busted(Heist *h)
{
    job_event(&h->state, &h->plan, JE_CAUGHT, -1, 0);
    job_result(&h->state, &h->result);
    h->screen = SCR_SUMMARY;
}

/* Takedowns from behind, for the current player. */
static void player_takedown(Heist *h, const NhInput *in)
{
    HeistPlayer *me = h->me;
    NhPlayer *p = &me->player;
    AiWorld *ai = &h->ai;
    me->takedown = -1;
    if (me->caught_t > 0.0f || me->cuffed || p->mode != NH_MOVE_WALK || interact_busy(h) || me->carrying >= 0 ||
        me->holding >= 0)
        return;
    NhVec3 fwd = nh_dir_from_yaw_pitch(p->yaw, 0);
    float best = 9.0f;
    for (int i = 0; i < ai->n; i++) {
        float d;
        if (!ai_can_takedown(ai, i, p->pos, &d))
            continue;
        NhVec3 to = nh_v3_norm(nh_v3(ai->g[i].pos.x - p->pos.x, 0, ai->g[i].pos.z - p->pos.z));
        if (nh_v3_dot(to, fwd) < 0.5f || d >= best)
            continue;
        best = d;
        me->takedown = i;
    }
    if (me->takedown >= 0 && (in->pressed & NH_BTN_A)) {
        ai_knockout(ai, me->takedown);
        if (!h->perks.silent_takedowns)
            heist_noise(h, ai->g[me->takedown].pos, 4.5f, NH_NOISE_TAKEDOWN);
        me->rumble_ticks = 18;
        me->takedown = -1;
    }
}

static HeistPlayer *partner_of(Heist *h, const HeistPlayer *me)
{
    return h->nplayers > 1 ? &h->pl[1 - me->index] : NULL;
}

/* The guards think, then the game reacts to what they did. */
static void update_guards(Heist *h)
{
    AiWorld *ai = &h->ai;
    AiPlayerView views[NH_MAX_PLAYERS];
    for (int i = 0; i < h->nplayers; i++)
        views[i] = h->pl[i].pv;
    guards_vis_pre_tick(&h->gv, ai);
    ai_tick_views(ai, views, h->nplayers);
    guards_vis_tick(&h->gv, ai, NH_TICK_DT);
    HeistPlayer *was = h->me;
    for (int i = 0; i < ai->nev; i++) {
        const AiEvent *e = &ai->ev[i];
        float d = 1e9f;
        for (int k = 0; k < h->nplayers; k++)
            d = NH_MIN(d, nh_v3_dist(e->pos, h->pl[k].player.pos));
        /* Who it concerns: the player in the event, or the guard's quarry. */
        int who = e->type == EV_CAUGHT || e->type == EV_FOUND_HIDING ? e->arg
                  : (int)e->guard < ai->n                     ? ai->g[e->guard].target
                                                              : 0;
        h->me = &h->pl[NH_CLAMP(who, 0, h->nplayers - 1)];
        switch (e->type) {
        case EV_BARK: {
            int b = e->arg;
            int sfx = b == BARK_DOG ? SFX_DOG_BARK
                    : b == BARK_THERE || b == BARK_FOUND || b == BARK_MAN_DOWN || b == BARK_SCREAM || b == BARK_POLICE
                        ? SFX_GUARD_ALERT
                    : b == BARK_CHAT || b == BARK_GIVEUP || b == BARK_CHECKIN || b == BARK_CHECKIN_OK ? SFX_GUARD_MUTTER
                    : b == BARK_RADIO_SPOTTED                                                         ? SFX_RADIO
                                                                                                      : SFX_GUARD_HUH;
            audio_at(sfx, e->pos, 0.9f, 22.0f);
            if (d < 16.0f && e->arg < BARK_COUNT && k_barks[e->arg])
                subtitle(h, k_barks[e->arg]);
            if (e->arg == BARK_MAN_DOWN)
                job_event(&h->state, &h->plan, JE_BODY_FOUND, -1, 0);
            break;
        }
        case EV_RADIO:
            audio_play(SFX_RADIO, 0.3f, 0.0f, 1.0f);
            subtitle(h, e->arg == BARK_CAMERA ? "Control: camera has someone. Moving in." : k_barks[BARK_RADIO_SPOTTED]);
            break;
        case EV_SPOTTED:
            audio_play(SFX_STINGER, 0.8f, 0.0f, 1.0f);
            heist_status(h, "Spotted!");
            job_event(&h->state, &h->plan, JE_SPOTTED, -1, 0);
            h->me->rumble_ticks = 12;
            break;
        case EV_ALARM:
            heist_alarm(h, e->pos, "ALARM RAISED");
            break;
        case EV_TAKEDOWN: {
            const AiGuard *g = &ai->g[e->guard];
            ai_add_anomaly(ai, AN_BODY, nh_v3_add(g->pos, nh_v3(0, 0.3f, 0)), e->guard);
            audio_at(SFX_TAKEDOWN, g->pos, 1.0f, 20.0f);
            job_event(&h->state, &h->plan, JE_TAKEDOWN, -1, 0);
            heist_status(h, "Takedown");
            break;
        }
        case EV_FOUND_HIDING:
            audio_play(SFX_GUARD_ALERT, 0.9f, 0.0f, 1.0f);
            interact_unhide(h);
            heist_status(h, "Found!");
            break;
        case EV_CHECKIN_MISSED:
            audio_play(SFX_RADIO, 0.3f, 0.0f, 1.0f);
            subtitle(h, "Control: no answer from your partner. Go and look.");
            break;
        case EV_CAUGHT:
            if (h->me->caught_t <= 0.0f && !h->me->cuffed && h->me->grab_t <= 0.0f && h->me->escapes_left > 0) {
                /* Grabbed: a few seconds to wriggle free. */
                h->me->grab_t = 3.0f;
                h->me->grab_presses = 0;
                h->me->grab_guard = e->guard;
                interact_drop_all(h);
                audio_play(SFX_GUARD_ALERT, 0.9f, 0.0f, 1.0f);
                h->me->rumble_ticks = 30;
            } else if (h->me->caught_t <= 0.0f && !h->me->cuffed && h->me->grab_t <= 0.0f) {
                h->me->caught_t = 2.5f;
                audio_play(SFX_CAUGHT, 0.9f, 0.0f, 1.0f);
                heist_status(h, "CAUGHT");
                h->me->rumble_ticks = 40;
                HeistPlayer *mate = partner_of(h, h->me);
                if (mate && !mate->cuffed) {
                    snprintf(mate->status, sizeof(mate->status), "Your partner's been caught!");
                    mate->status_ticks = 180;
                    mate->rumble_ticks = NH_MAX(mate->rumble_ticks, 20);
                }
            }
            break;
        default:
            break;
        }
    }
    h->me = was;
    if (h->subtitle_ticks > 0)
        h->subtitle_ticks--;
}

/* After the world has moved: being caught, the heartbeat, rumble, the torch
 * and how lit the current player is. */
static void player_after(Heist *h, const GameSettings *gs)
{
    HeistPlayer *me = h->me;
    World *w = &h->world;
    if (me->grab_t > 0.0f) {
        me->grab_t -= NH_TICK_DT;
        if (me->grab_t <= 0.0f) {
            /* Didn't make it: caught for real. */
            me->escapes_left = 0;
            me->caught_t = 2.5f;
            audio_play(SFX_CAUGHT, 0.9f, 0.0f, 1.0f);
            heist_status(h, "CAUGHT");
            me->rumble_ticks = 40;
        }
    }
    if (me->caught_t > 0.0f) {
        me->caught_t -= NH_TICK_DT;
        if (me->caught_t <= 0.0f) {
            HeistPlayer *mate = partner_of(h, me);
            if (!mate || mate->cuffed) {
                busted(h);
            } else {
                /* Co-op: cuffed where they stand until the partner frees them;
                 * the guards radio it in. */
                interact_drop_all(h);
                me->cuffed = true;
                me->cuffed_t = 0.0f;
                heist_alarm(h, me->player.pos, "Your partner was caught: free them!");
            }
        }
    }
    if (me->cuffed) {
        me->cuffed_t += NH_TICK_DT;
        if (me->cuffed_t > 90.0f) {
            heist_status_all(h, "The police took your partner away");
            busted(h);
        }
    }
    /* Heartbeat rumble while someone is getting suspicious. */
    float sus = NH_MAX(h->ai.max_suspicion, h->sec.max_sus);
    if (sus > 0.45f && sus < 1.0f && me->caught_t <= 0.0f && !me->cuffed) {
        me->heartbeat_t -= NH_TICK_DT;
        if (me->heartbeat_t <= 0.0f) {
            me->heartbeat_t = nh_lerpf(1.0f, 0.42f, (sus - 0.45f) / 0.55f);
            if (me->index == 0)
                audio_play(SFX_HEARTBEAT, 0.25f + 0.35f * sus, 0.0f, 1.0f);
            me->rumble_ticks = NH_MAX(me->rumble_ticks, 4);
        }
    } else {
        me->heartbeat_t = 0.0f;
    }
    plat_rumble(me->index, gs->rumble && me->rumble_ticks > 0);
    if (me->rumble_ticks > 0)
        me->rumble_ticks--;
    /* Torch: battery drains while on; aim follows the cursor smoothly. */
    if (me->torch_on) {
        me->battery = fmaxf(0.0f, me->battery - NH_TICK_DT / (900.0f * h->perks.torch_life));
        if (me->battery <= 0.0f)
            me->torch_on = false;
    }
    NhVec3 aim = nh_player_aim_dir(&me->player, me->aspect, HEIST_FOVY);
    me->torch_dir = nh_v3_norm(nh_v3_lerp(me->torch_dir, aim, 0.35f));
    /* Player light level: static light at the body plus the torch's spill. */
    NhVec3 body = nh_v3_add(me->player.pos, nh_v3(0, 1.0f, 0));
    me->light_level = nh_bake_light_level(&w->L, body, &w->bake) + (me->torch_on ? 0.08f : 0.0f);
    me->light_level = NH_CLAMP(me->light_level, 0.0f, 1.0f);
    if (me->status_ticks > 0)
        me->status_ticks--;
    guide_tick(h);
}

/* Cameras, lasers, alarmed doors; guards turning the power back on; police. */
static void update_security(Heist *h)
{
    const float dt = NH_TICK_DT;
    Level *L = &h->world.L;
    NhVec3 where = h->pl[0].player.pos;
    if (h->cams_looped > 0.0f)
        h->cams_looped -= dt;
    AiPlayerView views[NH_MAX_PLAYERS];
    for (int i = 0; i < h->nplayers; i++)
        views[i] = h->pl[i].pv;
    uint32_t ev = sec_tick_views(&h->sec, L, &h->ai, views, h->nplayers, dt, &where);
    for (int i = 0; i < h->sec.ncam; i++) {
        const SecCamera *c = &h->sec.cam[i];
        const NhEntity *e = &h->plan.ent[c->ent];
        if (e->prop >= 0)
            L->props[e->prop].yaw = c->yaw;
    }
    if ((ev & SEC_EV_CAMERA) && h->cams_looped <= 0.0f) {
        bool operator_on = false;
        for (int i = 0; i < h->ai.n; i++) {
            const AiGuard *g = &h->ai.g[i];
            operator_on |= g->active && g->arch == ARCH_CAMOP && g->state == AS_STATION;
        }
        job_event(&h->state, &h->plan, JE_CAMERA_SPOTTED, -1, 0);
        if (operator_on) {
            ai_report(&h->ai, where);
            heist_status_all(h, "Spotted on camera!");
        } else if (h->job.security >= 3) {
            heist_alarm(h, where, "Camera alarm!");
        } else {
            heist_status_all(h, "A camera saw you (recorded)");
        }
        for (int i = 0; i < h->nplayers; i++)
            h->pl[i].rumble_ticks = NH_MAX(h->pl[i].rumble_ticks, 10);
    }
    if (ev & SEC_EV_CAMERA)
        audio_at(SFX_CAMERA_BEEP, where, 0.6f, 14.0f);
    if (ev & SEC_EV_LASER)
        audio_play(SFX_LASER_TRIP, 0.7f, 0.0f, 1.0f);
    if (ev & SEC_EV_LASER)
        heist_alarm(h, where, "Laser tripped!");
    if (ev & SEC_EV_ENTRY_TIMEOUT)
        heist_alarm(h, where, "Door alarm!");
    /* A guard who reaches a cut fuse box puts the power back. */
    for (int i = 0; i < h->plan.nent; i++) {
        const NhEntity *e = &h->plan.ent[i];
        if (e->kind != ENT_FUSE_BOX || L->power[e->key_id & 15])
            continue;
        bool fixer = false;
        for (int g = 0; g < h->ai.n; g++) {
            const AiGuard *gd = &h->ai.g[g];
            fixer |= gd->active && gd->state != AS_KO && gd->state != AS_CARRIED && gd->arch != ARCH_DOG &&
                     !nh_ai_arch[gd->arch].civilian && nh_v3_dist(gd->pos, e->pos) < 1.8f;
        }
        float *t = &h->fuse_restore_t[e->key_id & 15];
        *t = fixer ? *t + dt : 0.0f;
        if (*t > 2.5f) {
            heist_set_power(h, e->key_id, true);
            ai_clear_anomaly(&h->ai, AN_LIGHTS_OFF, e->key_id);
        }
    }
    /* Police */
    NhJob *j = &h->state;
    if (j->police_here) {
        h->police_wave_t -= dt;
        if (!h->police_spawned || (h->police_wave_t <= 0.0f && h->ai.n < AI_MAX_GUARDS - 1)) {
            int n = h->police_spawned ? 2 : 3;
            for (int k = 0; k < n; k++) {
                NhVec3 at = nh_v3_add(h->plan.entry[0], nh_v3((float)k * 1.2f, 0, 0));
                int gi = ai_spawn_police(&h->ai, at, h->pl[(int)(h->tick + (uint64_t)k) % h->nplayers].player.pos);
                if (gi >= 0) {
                    h->gv.g[gi].prev_pos = at;
                    h->gv.g[gi].clip = h->gv.g[gi].prev_clip = h->ai.g[gi].anim;
                    h->gv.g[gi].blend = 1.0f;
                }
            }
            if (!h->police_spawned)
                heist_status_all(h, "POLICE ON SITE");
            h->police_spawned = true;
            h->police_wave_t = 45.0f;
        }
    }
}

/* Footsteps, loops that follow the world's state, and the music mix. */
static void update_audio(Heist *h)
{
    const float dt = NH_TICK_DT;
    const Level *L = &h->world.L;
    NhVec3 lpos[NH_MAX_PLAYERS];
    float lyaw[NH_MAX_PLAYERS];
    int lfloor[NH_MAX_PLAYERS];
    for (int k = 0; k < h->nplayers; k++) {
        HeistPlayer *me = &h->pl[k];
        NhPlayer *p = &me->player;
        lpos[k] = nh_player_eye(p, L, 1.0f);
        lyaw[k] = p->yaw;
        lfloor[k] = p->floor;
        /* Each player's own steps (both are heard in co-op: one TV). */
        for (int i = 0; i < p->nnoise; i++) {
            const NhNoise *n = &p->noises[i];
            if (n->kind != NH_NOISE_FOOTSTEP || !n->from_player)
                continue;
            float vol = NH_CLAMP(0.15f + n->radius / 14.0f, 0.15f, 0.9f) * (h->nplayers > 1 ? 0.7f : 1.0f);
            audio_play(SFX_STEP_CONCRETE + NH_MIN(n->surface, LV_SURF_COUNT - 1), vol, 0.0f,
                       0.9f + 0.03f * (float)((h->tick + (uint64_t)k * 3) % 7));
        }
        /* Torch and thermal clicks */
        if (me->torch_on != me->was_torch)
            audio_play(SFX_TORCH, 0.5f, 0.3f, 1.0f);
        if (me->thermal != me->was_thermal)
            audio_play(me->thermal ? SFX_THERMAL_ON : SFX_THERMAL_OFF, 0.5f, 0.0f, 1.0f);
        me->was_torch = me->torch_on;
        me->was_thermal = me->thermal;
    }
    audio_listeners(lpos, lyaw, lfloor, h->nplayers, L);
    /* Guards' steps: you hear them before you see them. */
    for (int i = 0; i < h->ai.n && i < AI_MAX_GUARDS; i++) {
        const AiGuard *g = &h->ai.g[i];
        if (!g->active || g->state == AS_KO || g->state == AS_CARRIED) {
            h->guard_prev[i] = g->pos;
            continue;
        }
        float moved = nh_v3_dist(g->pos, h->guard_prev[i]);
        h->guard_prev[i] = g->pos;
        if (moved > 1.0f) /* teleported (spawned) */
            continue;
        h->guard_step[i] += moved;
        float stride = g->arch == ARCH_DOG ? 0.45f : 0.75f;
        if (h->guard_step[i] >= stride) {
            h->guard_step[i] -= stride;
            const LvCell *c = lv_cell(L, lv_floor_of(g->pos.y + 0.3f), lv_cell_of(g->pos.x), lv_cell_of(g->pos.z));
            int surf = g->arch == ARCH_DOG ? LV_SURF_CARPET : c ? c->surface : LV_SURF_CONCRETE;
            float run = moved / dt > 2.5f ? 1.0f : 0.0f;
            audio_at(SFX_STEP_CONCRETE + surf, g->pos, 0.45f + 0.3f * run, 14.0f);
        }
    }
    /* Alarm bell and sirens */
    bool alarm = h->ai.alarm || h->state.alarm;
    if (alarm && h->snd_alarm < 0)
        h->snd_alarm = audio_loop(SFX_ALARM, 0.0f);
    if (h->snd_alarm >= 0)
        audio_loop_set(h->snd_alarm, h->screen == SCR_PLAY ? 0.35f : 0.12f, 0.0f);
    float eta = h->state.police_eta;
    bool sirens = h->state.police_here || (eta >= 0.0f && eta < 30.0f);
    if (sirens && h->snd_siren < 0)
        h->snd_siren = audio_loop(SFX_SIREN, 0.0f);
    if (h->snd_siren >= 0)
        audio_loop_set(h->snd_siren, h->state.police_here ? 0.45f : 0.45f * (1.0f - eta / 30.0f), 0.3f);
    /* The door alarm counting down beeps every second. */
    if (h->sec.entry_door >= 0) {
        h->entry_beep_t -= dt;
        if (h->entry_beep_t <= 0.0f) {
            h->entry_beep_t = h->sec.entry_t < 5.0f ? 0.5f : 1.0f;
            audio_play(SFX_KEYPAD_BEEP, 0.45f, 0.0f, 1.0f);
        }
    }
    /* A camera that has you in frame chirps as it makes up its mind. */
    h->cam_beep_t -= dt;
    if (h->sec.max_sus > 0.15f && h->sec.max_sus < 1.0f && h->cam_beep_t <= 0.0f) {
        h->cam_beep_t = nh_lerpf(1.2f, 0.35f, h->sec.max_sus);
        audio_play(SFX_CAMERA_BEEP, 0.3f, 0.0f, 1.0f);
    }
    /* Drill, noisemakers, a ringing phone */
    if (h->drill_t > 0.0f) {
        if (h->snd_drill < 0)
            h->snd_drill = audio_loop(SFX_DRILL, 0.0f);
        audio_loop_at(h->snd_drill, h->plan.ent[h->drill_target].pos, 0.8f, 30.0f);
    } else if (h->snd_drill >= 0) {
        audio_loop_stop(&h->snd_drill);
        audio_at(SFX_SAFE_OPEN, h->plan.ent[h->drill_target].pos, 0.9f, 20.0f);
    }
    for (int i = 0; i < (int)NH_ARRAY_LEN(h->proj); i++) {
        const Projectile *pr = &h->proj[i];
        bool buzzing = pr->active && pr->landed && pr->gadget == GD_NOISEMAKER && pr->t > 0.0f;
        if (buzzing) {
            if (h->snd_proj[i] < 0)
                h->snd_proj[i] = audio_loop(SFX_NOISEMAKER, 0.0f);
            audio_loop_at(h->snd_proj[i], pr->pos, 0.7f, 26.0f);
        } else if (h->snd_proj[i] >= 0) {
            audio_loop_stop(&h->snd_proj[i]);
        }
    }
    if (h->phone_t > 0.0f) {
        h->phone_t -= dt;
        if (h->snd_phone < 0)
            h->snd_phone = audio_loop(SFX_PHONE, 0.0f);
        audio_loop_at(h->snd_phone, h->phone_pos, 0.7f, 24.0f);
    } else if (h->snd_phone >= 0) {
        audio_loop_stop(&h->snd_phone);
    }
    /* Music: ambience always; the pulse once anyone's suspicious; tension
     * while they're looking for you; the action layer when it's all gone wrong. */
    float sus = NH_MAX(h->ai.max_suspicion, h->sec.max_sus);
    bool hunting = false;
    for (int i = 0; i < h->ai.n; i++) {
        const AiGuard *g = &h->ai.g[i];
        hunting |= g->active && (g->state == AS_ALERT || g->state == AS_SEARCH || g->state == AS_INVESTIGATE);
    }
    float mix[NH_MUSIC_LAYERS] = { 1.0f, 0.0f, 0.0f, 0.0f };
    bool minigame = false, caught = false;
    for (int k = 0; k < h->nplayers; k++) {
        minigame |= h->pl[k].mg != MG_KIND_NONE;
        caught |= h->pl[k].caught_t > 0.0f || h->pl[k].cuffed;
    }
    mix[1] = sus > 0.2f || hunting || minigame ? 0.8f : 0.0f;
    mix[2] = hunting || sus > 0.6f ? 0.8f : 0.0f;
    mix[3] = alarm || caught ? 1.0f : 0.0f;
    if (mix[3] > 0.0f)
        mix[0] = 0.4f;
    if (h->screen == SCR_SUMMARY)
        mix[1] = mix[2] = mix[3] = 0.0f;
    audio_music_mix(mix);
}

static void update_player(Heist *h, const NhInput *in, GameSettings *gs)
{
    World *w = &h->world;
    HeistPlayer *me = h->me;
    NhPlayerInput pi = { 0 };
    pi.ir_valid = in->ir_valid;
    pi.ir_x = in->ir_x;
    pi.ir_y = in->ir_y;
    float slow = (me->carrying >= 0 || me->holding >= 0) ? h->perks.carry_speed : 1.0f;
    me->player.noise_scale = h->perks.noise_scale;
    if (gs->controls.scheme == NH_CONTROLS_SIMPLE) {
        pi.move_y = in->stick_y * slow;
        pi.turn = in->stick_x;
    } else {
        pi.move_x = in->stick_x * slow;
        pi.move_y = in->stick_y * slow;
    }
    pi.run_held = (in->held & NH_BTN_DOWN) != 0;
    pi.lean = NH_CLAMP(in->nunchuk_roll / NH_DEG2RAD(30.0f), -1.0f, 1.0f);
    pi.crouch_held = (in->held & NH_BTN_Z) != 0;
    pi.crouch_pressed = (in->pressed & NH_BTN_Z) != 0;
    pi.frozen = me->caught_t > 0.0f || me->cuffed || me->mg != MG_KIND_NONE || me->grab_t > 0.0f;
    if (me->hide_prop >= 0) {
        /* Hidden: look around, don't move. */
        pi.move_x = pi.move_y = 0.0f;
        pi.crouch_held = pi.crouch_pressed = false;
    }
    nh_player_tick(&me->player, &w->L, &pi, &gs->controls, me->aspect, HEIST_FOVY);
}

/* One player's input: their map, moving, the torch, interactions, gadgets
 * and takedowns. Returns false when it has paused the whole game. */
static bool tick_player(Heist *h, const NhInput *in, GameSettings *gs)
{
    HeistPlayer *me = h->me;
    if (me->view == SCR_MAP || me->view == SCR_OBJECTIVES) {
        if (in->pressed & (NH_BTN_PLUS | NH_BTN_MINUS | NH_BTN_B))
            me->view = (in->pressed & NH_BTN_PLUS) && me->view != SCR_MAP              ? SCR_MAP
                       : (in->pressed & NH_BTN_MINUS) && me->view != SCR_OBJECTIVES ? SCR_OBJECTIVES
                                                                                      : SCR_PLAY;
        if (h->nplayers == 1) {
            plat_rumble(0, false);
            return false;
        }
        /* Co-op: the world goes on; this player stands still. */
        NhInput none = { 0 };
        update_player(h, &none, gs);
        me->takedown = -1;
        interact_carry_tick(h);
        return true;
    }
    if (!gs->show_debug && me->mg == MG_KIND_NONE && !me->cuffed) {
        if (in->pressed & (NH_BTN_PLUS | NH_BTN_MINUS)) {
            me->view = (in->pressed & NH_BTN_PLUS) ? SCR_MAP : SCR_OBJECTIVES;
            return h->nplayers > 1;
        }
    }
    update_player(h, in, gs);
    if (me->grab_t > 0.0f) {
        if (in->pressed & (NH_BTN_A | NH_BTN_B | NH_BTN_C | NH_BTN_Z)) {
            me->grab_presses++;
            me->rumble_ticks = NH_MAX(me->rumble_ticks, 4);
        }
        if (me->grab_presses >= 6) {
            ai_shove(&h->ai, me->grab_guard, me->index, 2.5f);
            me->grab_t = 0.0f;
            me->escapes_left--;
            audio_play(SFX_TAKEDOWN, 0.7f, 0.0f, 1.3f);
            heist_status(h, me->escapes_left > 0 ? "Broke free! Get out of sight!" : "Broke free! Next time they'll hold on.");
        }
        me->act.kind = ACT_NOTHING;
        me->takedown = -1;
        return true;
    }
    bool free = me->caught_t <= 0.0f && !me->cuffed;
    if (free && (in->pressed & NH_BTN_B) && me->mg == MG_KIND_NONE && me->hide_prop < 0)
        me->torch_on = !me->torch_on;
    if (free) {
        interact_find(h, gs);
        interact_update(h, in, gs);
        bool a_free = me->takedown < 0 && me->mg == MG_KIND_NONE && me->hide_prop < 0;
        gadgets_update(h, in, a_free);
        player_takedown(h, in);
    } else {
        me->act.kind = ACT_NOTHING;
        me->takedown = -1;
    }
    interact_carry_tick(h);
    return true;
}

void heist_tick(Heist *h, const NhInput in[NH_MAX_PLAYERS], GameSettings *gs)
{
    World *w = &h->world;
    h->subtitles = gs->subtitles;
    h->simple = gs->controls.scheme == NH_CONTROLS_SIMPLE;
    h->me = &h->pl[0];
    uint32_t any_pressed = 0;
    for (int i = 0; i < h->nplayers; i++)
        any_pressed |= in[i].pressed;
    if (in[0].pressed & NH_BTN_1)
        gs->show_debug = !gs->show_debug;
    /* Screens that pause the game. */
    if (h->screen == SCR_SUMMARY) {
        for (int i = 0; i < NH_MAX_PLAYERS; i++)
            plat_rumble(i, false);
        if (!h->summary_audio) {
            stop_loops(h);
            h->summary_audio = true;
            bool good = h->result.outcome == JOB_SUCCESS || h->result.outcome == JOB_PARTIAL;
            audio_play(good ? SFX_SUCCESS : SFX_FAIL, 0.7f, 0.0f, 1.0f);
            static const float calm[NH_MUSIC_LAYERS] = { 0.6f, 0.0f, 0.0f, 0.0f };
            audio_music_mix(calm);
        }
        if (h->campaign) {
            if (any_pressed & NH_BTN_A)
                h->done = true;
            return;
        }
        if (in[0].pressed & (NH_BTN_A | NH_BTN_B)) {
            if (in[0].pressed & NH_BTN_A)
                h->job.seed = h->job.seed * 1103515245u + 12345u; /* next job */
            if (!h->generated) {
                h->job.type = BT_OFFICE;
                h->job.size = BS_MEDIUM;
                h->job.security = 2;
            }
            h->want_rebuild = true;
        }
        return;
    }
    if (h->screen == SCR_BRIEF) {
        h->tick++;
        if (any_pressed & NH_BTN_A) {
            audio_play(SFX_UI_SELECT, 0.5f, 0.0f, 1.0f);
            h->screen = SCR_PLAY;
        }
        return;
    }
    if (h->screen == SCR_PAUSE) {
        for (int i = 0; i < NH_MAX_PLAYERS; i++)
            plat_rumble(i, false);
        const NhInput *pin = &in[NH_CLAMP(h->pause_owner, 0, h->nplayers - 1)];
        if (h->pause_page == 1) {
            if (pin->pressed & (NH_BTN_A | NH_BTN_B | NH_BTN_HOME))
                h->pause_page = 0;
            return;
        }
        if (h->pause_page == 2) {
            if (pin->pressed & NH_BTN_A) {
                job_event(&h->state, &h->plan, JE_ABORT, -1, 0);
                job_result(&h->state, &h->result);
                h->screen = SCR_SUMMARY;
            } else if (pin->pressed & (NH_BTN_B | NH_BTN_HOME)) {
                h->pause_page = 0;
            }
            return;
        }
        if (pin->pressed & (NH_BTN_B | NH_BTN_HOME)) {
            audio_play(SFX_UI_BACK, 0.5f, 0.0f, 1.0f);
            h->screen = SCR_PLAY;
            return;
        }
        UiMenu m = heist_pause_menu(h);
        int pick = ui_menu_tick(&m, &h->pause_sel, pin);
        if (pick == 0)
            h->screen = SCR_PLAY;
        else if (pick > 0)
            h->pause_page = pick;
        return;
    }
    for (int i = 0; i < h->nplayers; i++)
        if ((in[i].pressed & NH_BTN_HOME) && h->pl[i].caught_t <= 0.0f) {
            audio_play(SFX_UI_SELECT, 0.5f, 0.0f, 1.0f);
            h->screen = SCR_PAUSE;
            h->pause_sel = h->pause_page = 0;
            h->pause_owner = i;
            return;
        }
    if (h->nplayers == 1 && h->pl[0].view != SCR_PLAY) {
        tick_player(h, &in[0], gs);
        return;
    }
    if (in[0].pressed & NH_BTN_2)
        h->debug_view = (h->debug_view + 1) % 3;
    if (gs->show_debug && (in[0].pressed & (NH_BTN_PLUS | NH_BTN_MINUS))) {
        /* Debug: + new seed, - next building type. */
        if (!h->generated) {
            h->job.type = BT_OFFICE;
            h->job.size = BS_MEDIUM;
            h->job.security = 2;
        }
        if (in[0].pressed & NH_BTN_PLUS)
            h->job.seed = h->job.seed * 1103515245u + 12345u;
        else
            h->job.type = (uint8_t)((h->job.type + 1) % BT_COUNT);
        h->want_rebuild = true;
        return;
    }
    /* Each player's own business... */
    for (int i = 0; i < h->nplayers; i++) {
        h->me = &h->pl[i];
        if (!tick_player(h, &in[i], gs)) {
            h->me = &h->pl[0];
            return;
        }
    }
    h->me = &h->pl[0];
    if (h->screen != SCR_PLAY)
        return; /* the job ended on someone's action (extraction) */
    h->tick++;
    /* ...then the world's, once. */
    gadgets_world_tick(h);
    for (int i = 0; i < h->nplayers; i++) {
        h->me = &h->pl[i];
        fill_player_view(h);
    }
    h->me = &h->pl[0];
    update_guards(h);
    if (h->screen != SCR_PLAY)
        return;
    update_security(h);
    job_tick(&h->state, NH_TICK_DT);
    world_update(w, NH_TICK_DT);
    /* One re-lit room per tick after a power change. */
    for (int r = LV_ROOM_FIRST; r < w->L.nrooms; r++)
        if (h->rebake[r]) {
            h->rebake[r] = 0;
            world_rebake_room(w, r);
            break;
        }
    for (int i = 0; i < h->nplayers && h->screen == SCR_PLAY; i++) {
        h->me = &h->pl[i];
        player_after(h, gs);
    }
    h->me = &h->pl[0];
    update_audio(h);
}

static void torch_projector(const Heist *h, NhVec3 eye, const NhMat34 *view, GfxProjector *pr)
{
    NhVec3 right = nh_v3(view->m[0][0], view->m[0][1], view->m[0][2]);
    NhVec3 up = nh_v3(view->m[1][0], view->m[1][1], view->m[1][2]);
    NhVec3 hand = nh_v3_add(eye, nh_v3_add(nh_v3_scale(right, 0.16f), nh_v3_scale(up, -0.14f)));
    memset(pr, 0, sizeof(*pr));
    pr->enabled = true;
    pr->pos = hand;
    pr->dir = h->me->torch_dir;
    pr->fov_deg = h->perks.torch_fov;
    pr->cookie = s_cookie;
    float b = h->me->battery < 0.15f ? 0.5f + h->me->battery * 3.3f : 1.0f;
    nh_light_init(&pr->light, hand, nh_rgba((uint8_t)(255 * b), (uint8_t)(244 * b), (uint8_t)(222 * b), 255));
    nh_light_set_spot(&pr->light, h->me->torch_dir, h->perks.torch_fov * 0.58f, NH_SPOT_COS);
    nh_light_set_dist(&pr->light, 11.0f, 0.5f, NH_DIST_MEDIUM);
}

/* Dust motes drifting in the torch beam, up to where the beam hits something. */
static void draw_dust(const Heist *h, NhVec3 eye, const NhMat34 *view, float alpha)
{
    static GfxTexture dust;
    if (!dust)
        dust = res_texture("dust");
    NhVec3 right = nh_v3(view->m[0][0], view->m[0][1], view->m[0][2]);
    NhVec3 up = nh_v3(view->m[1][0], view->m[1][1], view->m[1][2]);
    NhVec3 hand = nh_v3_add(eye, nh_v3_add(nh_v3_scale(right, 0.16f), nh_v3_scale(up, -0.14f)));
    NhVec3 dir = h->me->torch_dir;
    float reach = 9.0f, t;
    if (lv_raycast(&h->world.L, hand, nh_v3_add(hand, nh_v3_scale(dir, reach)), &t))
        reach *= t;
    NhVec3 a = nh_v3_norm(nh_v3_cross(dir, nh_v3(0, 1, 0)));
    if (nh_v3_len(a) < 0.1f)
        a = nh_v3(1, 0, 0);
    NhVec3 b = nh_v3_cross(a, dir);
    float time = ((float)h->tick + alpha) / 60.0f;
    float bat = h->me->battery < 0.15f ? 0.5f + h->me->battery * 3.3f : 1.0f;
    for (int i = 0; i < 56; i++) {
        uint32_t k = (uint32_t)i * 2654435761u;
        float r1 = (float)(k & 1023) / 1023.0f, r2 = (float)((k >> 10) & 1023) / 1023.0f;
        float r3 = (float)((k >> 20) & 1023) / 1023.0f;
        float d = 0.6f + fmodf(r1 * 8.0f + time * (0.05f + r3 * 0.08f), 8.0f);
        if (d > reach)
            continue;
        float ang = r2 * 6.2832f + time * 0.15f * (r3 - 0.5f);
        float rad = sqrtf(r3) * 0.38f * d;
        NhVec3 p = nh_v3_add(hand, nh_v3_scale(dir, d));
        p = nh_v3_add(p, nh_v3_add(nh_v3_scale(a, cosf(ang) * rad), nh_v3_scale(b, sinf(ang) * rad)));
        p.y += sinf(time * 0.7f + r1 * 9.0f) * 0.05f;
        float edge = 1.0f - rad / (0.38f * d + 1e-3f);
        float bright = edge * edge * fminf(1.0f, 2.0f / d) * bat;
        if (bright < 0.03f)
            continue;
        float s = 0.012f + r2 * 0.012f;
        NhVec3 rr = nh_v3_scale(right, s), uu = nh_v3_scale(up, s);
        NhVec3 c[4] = { nh_v3_sub(nh_v3_sub(p, rr), uu), nh_v3_sub(nh_v3_add(p, rr), uu),
                        nh_v3_add(nh_v3_add(p, rr), uu), nh_v3_add(nh_v3_sub(p, rr), uu) };
        gfx_draw_quad3d(dust, c, 0, 0, 1, 1, nh_rgba(255, 236, 200, (uint8_t)(200 * bright)), GFX_MAT_ADD);
    }
}

static void draw_guards(void *ctx)
{
    Heist *h = ctx;
    NhVec3 eye = nh_player_eye(&h->me->player, &h->world.L, h->alpha);
    guards_draw(&h->gv, &h->ai, &h->world, h->alpha, h->me->thermal, eye);
    /* Co-op: your partner. */
    for (int i = 0; i < h->nplayers; i++) {
        const HeistPlayer *o = &h->pl[i];
        if (o == h->me)
            continue;
        const NhPlayer *op = &o->player;
        ThiefVis tv = { nh_v3_lerp(op->prev_pos, op->pos, h->alpha), op->yaw, op->speed,
                        (float)h->tick / 60.0f, o->cuffed };
        if (o->hide_prop < 0 && op->mode == NH_MOVE_WALK)
            guards_draw_thief(&h->world, &tv, h->me->thermal, eye);
    }
}

/* One player's view of the world, into the current viewport. */
static void render_view(Heist *h, float alpha, const GameSettings *gs, float fps)
{
    World *w = &h->world;
    NhMat34 view;
    NhVec3 eye;
    heist_camera(h, alpha, &view, &eye);
    gfx_set_camera(&view, HEIST_FOVY, 0.08f, 90.0f);
    GfxFog fog = { true, 18.0f, 70.0f, nh_rgba(6, 8, 14, 255) };
    gfx_set_fog(&fog);
    GfxProjector torch, gtorch;
    NhLight glights[GFX_MAX_DYN_LIGHTS];
    int ngl = guards_lights(&h->gv, &h->ai, w, eye, alpha, &gtorch, glights, 4);
    /* Co-op: the partner's torch as a spot light. */
    for (int i = 0; i < h->nplayers && ngl < GFX_MAX_DYN_LIGHTS; i++) {
        const HeistPlayer *o = &h->pl[i];
        if (o == h->me || !o->torch_on || o->hide_prop >= 0 || o->player.mode != NH_MOVE_WALK)
            continue;
        NhLight l;
        nh_light_init(&l, nh_player_eye(&o->player, &w->L, alpha), nh_rgba(235, 228, 205, 255));
        nh_light_set_spot(&l, o->torch_dir, h->perks.torch_fov * 0.58f, NH_SPOT_COS);
        nh_light_set_dist(&l, 11.0f, 0.5f, NH_DIST_MEDIUM);
        glights[ngl++] = l;
    }
    gfx_set_lights(glights, ngl);
    bool torch_vis = h->me->torch_on && h->me->hide_prop < 0;
    if (torch_vis)
        torch_projector(h, eye, &view, &torch);
    gfx_set_projectors(torch_vis ? &torch : NULL, gtorch.enabled ? &gtorch : NULL);
    gfx_set_mode(h->debug_view == 2 || h->me->thermal ? GFX_MODE_THERMAL : GFX_MODE_NORMAL);
    h->alpha = alpha;

    WorldView wv = { view, eye, HEIST_FOVY, h->me->aspect, 0.08f, 90.0f,
                     h->me->player.mode == NH_MOVE_VENT, h->me->player.floor, draw_guards, h };
    world_draw(w, &wv);
    for (int r = 0; r < w->L.nrooms; r++)
        if (w->visible[r])
            h->explored[r] = 1;
    if (h->me->player.mode == NH_MOVE_WALK) {
        guards_draw_glows(&h->gv, &h->ai, w, &view, alpha);
        gadgets_render(h, &view);
        if (h->debug_view)
            guards_draw_debug(&h->ai, w);
    }
    if (torch_vis && h->me->player.mode == NH_MOVE_WALK)
        draw_dust(h, eye, &view, alpha);

    gfx_set_mode(GFX_MODE_NORMAL);
    hud_draw_heist(h, gs, &view);
    if (gs->show_debug && h->me->index == 0)
        hud_draw_debug(h, fps);
}

void heist_render(Heist *h, float alpha, const GameSettings *gs, float fps)
{
    gfx_begin_frame(nh_rgba(6, 8, 14, 255));
    if (h->nplayers == 1) {
        h->me = &h->pl[0];
        render_view(h, alpha, gs, fps);
    } else {
        /* Split screen: player 1 on top, player 2 below. */
        for (int i = 0; i < h->nplayers; i++) {
            h->me = &h->pl[i];
            int y = i * (GFX_SCREEN_H / 2);
            gfx_set_viewport(0, y, GFX_SCREEN_W, GFX_SCREEN_H / 2);
            gfx_set_2d_region(0.0f, (float)y, 1.0f, 0.5f);
            text_set_tall(2.0f);
            render_view(h, alpha, gs, fps);
            text_set_tall(1.0f);
        }
        gfx_set_viewport(0, 0, GFX_SCREEN_W, GFX_SCREEN_H);
        gfx_set_2d_region(0.0f, 0.0f, 1.0f, 1.0f);
        text_rect(0, GFX_SCREEN_H / 2 - 1, GFX_SCREEN_W, 2, nh_rgba(0, 0, 0, 255));
    }
    h->me = &h->pl[0];
    hud_draw_overlay(h, gs);
    gfx_end_frame();
}
