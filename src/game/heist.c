/* Night Heist - the heist loop: player, guards, security, interactions,
 * gadgets, the police and the getaway. */
#include "heist.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/nh_fixedstep.h"
#include "core/nh_proptab.h"
#include "gadgets.h"
#include "hud.h"
#include "interact.h"
#include "res.h"
#include "testlevel.h"

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
    h->takedown = -1;
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
                 NhArena *level_arena, NhArena *scratch)
{
    memset(h, 0, sizeof(*h));
    set_perks(h, camp, offer);
    if (offer) {
        h->campaign = true;
        h->offer = *offer;
    }
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
    nh_player_init(&h->player, start, yaw);
    float dx, dy, dz, dyaw, dpitch = 0.0f;
    if (plat_debug_start(&dx, &dy, &dz, &dyaw, &dpitch)) {
        nh_player_init(&h->player, nh_v3(dx, dy, dz), dyaw);
        h->player.pitch = h->player.prev_pitch = dpitch;
    }
    h->torch_on = true;
    h->battery = 1.0f;
    h->thermal_battery = 1.0f;
    h->torch_dir = nh_dir_from_yaw_pitch(yaw, 0);
    h->hide_prop = -1;
    s_cookie = res_texture("cookie");
    h->nav = nh_arena_alloc(level_arena, sizeof(NhNav));
    if (!h->nav)
        return false;
    nh_nav_build(h->nav, &h->world.L, 0.3f);
    spawn_guards(h);
    nh_rng_seed(&h->rng, h->world.L.seed, 0x1A7u);
    h->ai.difficulty = difficulty == 0 ? 0.75f : difficulty == 2 ? 1.3f : 1.0f;
    if (h->generated) {
        job_init(&h->state, &h->world.L, &h->plan, h->world.L.seed);
        h->state.police_delay = 100.0f - 12.0f * (float)h->job.security;
        if (camp && offer)
            camp_prepare_job(camp, offer, &h->state);
        if (offer)
            snprintf(h->job_name, sizeof(h->job_name), "%s", offer->name);
    } else {
        memset(&h->state, 0, sizeof(h->state));
        h->state.holding = h->state.carrying = -1;
        h->state.police_eta = -1.0f;
        h->state.bag_cap = 12;
        for (int g = 0; g < GD_COUNT; g++)
            h->state.charges[g] = nh_gadgets[g].charges;
    }
    if (!h->state.charges[GD_THERMAL])
        h->thermal_battery = 0.0f; /* goggles not in the kit yet */
    sec_init(&h->sec, &h->world.L, &h->plan);
    return true;
}

void heist_end(Heist *h)
{
    world_free(&h->world);
}

void heist_status(Heist *h, const char *msg)
{
    snprintf(h->status, sizeof(h->status), "%s", msg);
    h->status_ticks = 150;
}

void heist_noise(Heist *h, NhVec3 pos, float radius, int kind)
{
    NhPlayer *p = &h->player;
    if (p->nnoise < (int)NH_ARRAY_LEN(p->noises))
        p->noises[p->nnoise++] = (NhNoise){ pos, radius, (uint8_t)kind, 0, 1 };
}

void heist_alarm(Heist *h, NhVec3 where, const char *why)
{
    if (!h->ai.alarm)
        ai_raise_alarm(&h->ai, where);
    if (!h->state.alarm) {
        job_event(&h->state, &h->plan, JE_ALARM, -1, 0);
        heist_status(h, why);
        h->rumble_ticks = 30;
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
    const NhPlayer *p = &h->player;
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
    snprintf(h->subtitle, sizeof(h->subtitle), "%s", msg);
    h->subtitle_ticks = 150;
}

static void fill_player_view(Heist *h)
{
    NhPlayer *p = &h->player;
    const Level *L = &h->world.L;
    AiPlayerView *pv = &h->pv;
    pv->pos = p->pos;
    pv->head = nh_player_eye(p, L, 1.0f);
    pv->height = nh_player_eye_height(p) + 0.1f;
    pv->speed = p->speed;
    /* A lit torch makes the player much easier to spot. */
    pv->light = NH_MAX(h->light_level, h->torch_on ? 0.7f : 0.0f);
    pv->crouched = p->crouch > 0.5f;
    pv->torch_on = h->torch_on;
    pv->in_vent = p->mode == NH_MOVE_VENT;
    pv->disguised = false;
    pv->hiding_prop = h->hide_prop;
    pv->beam_valid = false;
    if (h->torch_on && !pv->in_vent && h->hide_prop < 0) {
        NhVec3 a = pv->head, b = nh_v3_add(a, nh_v3_scale(h->torch_dir, 20.0f));
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

static void update_guards(Heist *h, const NhInput *in, const GameSettings *gs)
{
    NhPlayer *p = &h->player;
    AiWorld *ai = &h->ai;
    /* Takedown: nearest eligible guard roughly in front of the player. */
    h->takedown = -1;
    if (h->caught_t <= 0.0f && p->mode == NH_MOVE_WALK && !interact_busy(h) && h->state.carrying < 0 &&
        h->state.holding < 0) {
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
            h->takedown = i;
        }
        if (h->takedown >= 0 && (in->pressed & NH_BTN_A)) {
            ai_knockout(ai, h->takedown);
            if (!h->perks.silent_takedowns)
                heist_noise(h, ai->g[h->takedown].pos, 4.5f, NH_NOISE_TAKEDOWN);
            h->rumble_ticks = 18;
            h->takedown = -1;
        }
    }
    interact_carry_tick(h);
    fill_player_view(h);
    guards_vis_pre_tick(&h->gv, ai);
    ai_tick(ai, &h->pv);
    guards_vis_tick(&h->gv, ai, NH_TICK_DT);
    for (int i = 0; i < ai->nev; i++) {
        const AiEvent *e = &ai->ev[i];
        float d = nh_v3_dist(e->pos, p->pos);
        switch (e->type) {
        case EV_BARK:
            if (d < 16.0f && e->arg < BARK_COUNT && k_barks[e->arg])
                subtitle(h, k_barks[e->arg]);
            if (e->arg == BARK_MAN_DOWN)
                job_event(&h->state, &h->plan, JE_BODY_FOUND, -1, 0);
            break;
        case EV_RADIO:
            subtitle(h, e->arg == BARK_CAMERA ? "Control: camera has someone. Moving in." : k_barks[BARK_RADIO_SPOTTED]);
            break;
        case EV_SPOTTED:
            heist_status(h, "Spotted!");
            job_event(&h->state, &h->plan, JE_SPOTTED, -1, 0);
            h->rumble_ticks = 12;
            break;
        case EV_ALARM:
            heist_alarm(h, e->pos, "ALARM RAISED");
            break;
        case EV_TAKEDOWN: {
            const AiGuard *g = &ai->g[e->guard];
            ai_add_anomaly(ai, AN_BODY, nh_v3_add(g->pos, nh_v3(0, 0.3f, 0)), e->guard);
            job_event(&h->state, &h->plan, JE_TAKEDOWN, -1, 0);
            heist_status(h, "Takedown");
            break;
        }
        case EV_FOUND_HIDING:
            interact_unhide(h);
            heist_status(h, "Found!");
            break;
        case EV_CHECKIN_MISSED:
            subtitle(h, "Control: no answer from your partner. Go and look.");
            break;
        case EV_CAUGHT:
            if (h->caught_t <= 0.0f) {
                h->caught_t = 2.5f;
                heist_status(h, "CAUGHT");
                h->rumble_ticks = 40;
            }
            break;
        default:
            break;
        }
    }
    if (h->caught_t > 0.0f) {
        h->caught_t -= NH_TICK_DT;
        if (h->caught_t <= 0.0f)
            busted(h);
    }
    /* Heartbeat rumble while someone is getting suspicious. */
    float sus = NH_MAX(ai->max_suspicion, h->sec.max_sus);
    if (sus > 0.45f && sus < 1.0f && h->caught_t <= 0.0f) {
        h->heartbeat_t -= NH_TICK_DT;
        if (h->heartbeat_t <= 0.0f) {
            h->heartbeat_t = nh_lerpf(1.0f, 0.42f, (sus - 0.45f) / 0.55f);
            h->rumble_ticks = NH_MAX(h->rumble_ticks, 4);
        }
    } else {
        h->heartbeat_t = 0.0f;
    }
    plat_rumble(gs->rumble && h->rumble_ticks > 0);
    if (h->rumble_ticks > 0)
        h->rumble_ticks--;
    if (h->subtitle_ticks > 0)
        h->subtitle_ticks--;
}

/* Cameras, lasers, alarmed doors; guards turning the power back on; police. */
static void update_security(Heist *h)
{
    const float dt = NH_TICK_DT;
    Level *L = &h->world.L;
    NhVec3 where = h->player.pos;
    if (h->cams_looped > 0.0f)
        h->cams_looped -= dt;
    uint32_t ev = sec_tick(&h->sec, L, &h->ai, &h->pv, dt, &where);
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
            heist_status(h, "Spotted on camera!");
        } else if (h->job.security >= 3) {
            heist_alarm(h, where, "Camera alarm!");
        } else {
            heist_status(h, "A camera saw you (recorded)");
        }
        h->rumble_ticks = NH_MAX(h->rumble_ticks, 10);
    }
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
                int gi = ai_spawn_police(&h->ai, at, h->player.pos);
                if (gi >= 0) {
                    h->gv.g[gi].prev_pos = at;
                    h->gv.g[gi].clip = h->gv.g[gi].prev_clip = h->ai.g[gi].anim;
                    h->gv.g[gi].blend = 1.0f;
                }
            }
            if (!h->police_spawned)
                heist_status(h, "POLICE ON SITE");
            h->police_spawned = true;
            h->police_wave_t = 45.0f;
        }
    }
}

static void update_player(Heist *h, const NhInput *in, GameSettings *gs)
{
    World *w = &h->world;
    NhPlayerInput pi = { 0 };
    pi.ir_valid = in->ir_valid;
    pi.ir_x = in->ir_x;
    pi.ir_y = in->ir_y;
    float slow = (h->state.carrying >= 0 || h->state.holding >= 0) ? h->perks.carry_speed : 1.0f;
    h->player.noise_scale = h->perks.noise_scale;
    pi.move_x = in->stick_x * slow;
    pi.move_y = in->stick_y * slow;
    pi.lean = NH_CLAMP(in->nunchuk_roll / NH_DEG2RAD(30.0f), -1.0f, 1.0f);
    pi.crouch_held = (in->held & NH_BTN_Z) != 0;
    pi.crouch_pressed = (in->pressed & NH_BTN_Z) != 0;
    pi.frozen = h->caught_t > 0.0f || h->mg != MG_KIND_NONE;
    if (h->hide_prop >= 0) {
        /* Hidden: look around, don't move. */
        pi.move_x = pi.move_y = 0.0f;
        pi.crouch_held = pi.crouch_pressed = false;
    }
    nh_player_tick(&h->player, &w->L, &pi, &gs->controls, gfx_display_aspect(), HEIST_FOVY);
}

void heist_tick(Heist *h, const NhInput *in, GameSettings *gs)
{
    World *w = &h->world;
    if (in->pressed & NH_BTN_1)
        gs->show_debug = !gs->show_debug;
    /* Screens that pause the game. */
    if (h->screen == SCR_SUMMARY) {
        plat_rumble(false);
        if (h->campaign) {
            if (in->pressed & NH_BTN_A)
                h->done = true;
            return;
        }
        if (in->pressed & (NH_BTN_A | NH_BTN_B)) {
            if (in->pressed & NH_BTN_A)
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
    if (h->screen == SCR_MAP || h->screen == SCR_OBJECTIVES) {
        plat_rumble(false);
        if (in->pressed & (NH_BTN_PLUS | NH_BTN_MINUS | NH_BTN_B))
            h->screen = (in->pressed & NH_BTN_PLUS) && h->screen != SCR_MAP   ? SCR_MAP
                        : (in->pressed & NH_BTN_MINUS) && h->screen != SCR_OBJECTIVES ? SCR_OBJECTIVES
                                                                                       : SCR_PLAY;
        return;
    }
    h->tick++;
    if (in->pressed & NH_BTN_2)
        h->debug_view = (h->debug_view + 1) % 3;
    if (gs->show_debug && (in->pressed & (NH_BTN_PLUS | NH_BTN_MINUS))) {
        /* Debug: + new seed, - next building type. */
        if (!h->generated) {
            h->job.type = BT_OFFICE;
            h->job.size = BS_MEDIUM;
            h->job.security = 2;
        }
        if (in->pressed & NH_BTN_PLUS)
            h->job.seed = h->job.seed * 1103515245u + 12345u;
        else
            h->job.type = (uint8_t)((h->job.type + 1) % BT_COUNT);
        h->want_rebuild = true;
        return;
    }
    if (!gs->show_debug && h->mg == MG_KIND_NONE) {
        if (in->pressed & NH_BTN_PLUS) {
            h->screen = SCR_MAP;
            return;
        }
        if (in->pressed & NH_BTN_MINUS) {
            h->screen = SCR_OBJECTIVES;
            return;
        }
    }
    update_player(h, in, gs);
    if ((in->pressed & NH_BTN_B) && h->mg == MG_KIND_NONE && h->hide_prop < 0)
        h->torch_on = !h->torch_on;
    if (h->caught_t <= 0.0f) {
        interact_find(h, gs);
        interact_update(h, in, gs);
    }
    bool a_free = h->takedown < 0 && h->mg == MG_KIND_NONE && h->hide_prop < 0 && h->caught_t <= 0.0f;
    gadgets_update(h, in, a_free);
    update_guards(h, in, gs);
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

    /* Torch: battery drains while on; aim follows the cursor smoothly. */
    if (h->torch_on) {
        h->battery = fmaxf(0.0f, h->battery - NH_TICK_DT / (900.0f * h->perks.torch_life));
        if (h->battery <= 0.0f)
            h->torch_on = false;
    }
    NhVec3 aim = nh_player_aim_dir(&h->player, gfx_display_aspect(), HEIST_FOVY);
    h->torch_dir = nh_v3_norm(nh_v3_lerp(h->torch_dir, aim, 0.35f));

    /* Player light level: static light at the body plus the torch's spill. */
    NhVec3 body = nh_v3_add(h->player.pos, nh_v3(0, 1.0f, 0));
    h->light_level = nh_bake_light_level(&w->L, body, &w->bake) + (h->torch_on ? 0.18f : 0.0f);
    h->light_level = NH_CLAMP(h->light_level, 0.0f, 1.0f);
    if (h->status_ticks > 0)
        h->status_ticks--;
}

static void torch_projector(const Heist *h, NhVec3 eye, const NhMat34 *view, GfxProjector *pr)
{
    NhVec3 right = nh_v3(view->m[0][0], view->m[0][1], view->m[0][2]);
    NhVec3 up = nh_v3(view->m[1][0], view->m[1][1], view->m[1][2]);
    NhVec3 hand = nh_v3_add(eye, nh_v3_add(nh_v3_scale(right, 0.16f), nh_v3_scale(up, -0.14f)));
    memset(pr, 0, sizeof(*pr));
    pr->enabled = true;
    pr->pos = hand;
    pr->dir = h->torch_dir;
    pr->fov_deg = h->perks.torch_fov;
    pr->cookie = s_cookie;
    float b = h->battery < 0.15f ? 0.5f + h->battery * 3.3f : 1.0f;
    nh_light_init(&pr->light, hand, nh_rgba((uint8_t)(255 * b), (uint8_t)(244 * b), (uint8_t)(222 * b), 255));
    nh_light_set_spot(&pr->light, h->torch_dir, h->perks.torch_fov * 0.58f, NH_SPOT_COS);
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
    NhVec3 dir = h->torch_dir;
    float reach = 9.0f, t;
    if (lv_raycast(&h->world.L, hand, nh_v3_add(hand, nh_v3_scale(dir, reach)), &t))
        reach *= t;
    NhVec3 a = nh_v3_norm(nh_v3_cross(dir, nh_v3(0, 1, 0)));
    if (nh_v3_len(a) < 0.1f)
        a = nh_v3(1, 0, 0);
    NhVec3 b = nh_v3_cross(a, dir);
    float time = ((float)h->tick + alpha) / 60.0f;
    float bat = h->battery < 0.15f ? 0.5f + h->battery * 3.3f : 1.0f;
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
    NhVec3 eye = nh_player_eye(&h->player, &h->world.L, h->alpha);
    guards_draw(&h->gv, &h->ai, &h->world, h->alpha, h->thermal, eye);
}

void heist_render(Heist *h, float alpha, const GameSettings *gs, float fps)
{
    World *w = &h->world;
    gfx_begin_frame(nh_rgba(6, 8, 14, 255));
    NhMat34 view;
    NhVec3 eye;
    heist_camera(h, alpha, &view, &eye);
    gfx_set_camera(&view, HEIST_FOVY, 0.08f, 90.0f);
    GfxFog fog = { true, 18.0f, 70.0f, nh_rgba(6, 8, 14, 255) };
    gfx_set_fog(&fog);
    GfxProjector torch, gtorch;
    NhLight glights[GFX_MAX_DYN_LIGHTS];
    int ngl = guards_lights(&h->gv, &h->ai, w, eye, alpha, &gtorch, glights, 4);
    gfx_set_lights(glights, ngl);
    bool torch_vis = h->torch_on && h->hide_prop < 0;
    if (torch_vis)
        torch_projector(h, eye, &view, &torch);
    gfx_set_projectors(torch_vis ? &torch : NULL, gtorch.enabled ? &gtorch : NULL);
    gfx_set_mode(h->debug_view == 2 || h->thermal ? GFX_MODE_THERMAL : GFX_MODE_NORMAL);
    h->alpha = alpha;

    WorldView wv = { view, eye, HEIST_FOVY, gfx_display_aspect(), 0.08f, 90.0f,
                     h->player.mode == NH_MOVE_VENT, h->player.floor, draw_guards, h };
    world_draw(w, &wv);
    for (int r = 0; r < w->L.nrooms; r++)
        if (w->visible[r])
            h->explored[r] = 1;
    if (h->player.mode == NH_MOVE_WALK) {
        guards_draw_glows(&h->gv, &h->ai, w, &view, alpha);
        gadgets_render(h, &view);
        if (h->debug_view)
            guards_draw_debug(&h->ai, w);
    }
    if (torch_vis && h->player.mode == NH_MOVE_WALK)
        draw_dust(h, eye, &view, alpha);

    gfx_set_mode(GFX_MODE_NORMAL);
    hud_draw_heist(h, gs, &view);
    if (gs->show_debug)
        hud_draw_debug(h, fps);
    gfx_end_frame();
}
