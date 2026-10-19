#include "heist.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/nh_fixedstep.h"
#include "hud.h"
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
    testlevel_guards(&h->ai);
    guards_vis_init(&h->gv, &h->ai);
    h->takedown = -1;
}

bool heist_start_test(Heist *h, NhArena *level_arena, NhArena *scratch)
{
    memset(h, 0, sizeof(*h));
    testlevel_build(&h->world.L);
    /* Convenience for Phase 1/2 testing: a couple of doors start open. */
    h->world.L.doors[0].open = h->world.L.doors[0].target = 1.0f;
    h->world.L.doors[0].locked = 0;
    h->world.L.doors[1].open = h->world.L.doors[1].target = 1.0f;
    if (!world_build(&h->world, level_arena, scratch))
        return false;
    float yaw;
    NhVec3 start = testlevel_start(&yaw);
    nh_player_init(&h->player, start, yaw);
    float dx, dy, dz, dyaw, dpitch = 0.0f;
    if (plat_debug_start(&dx, &dy, &dz, &dyaw, &dpitch)) {
        nh_player_init(&h->player, nh_v3(dx, dy, dz), dyaw);
        h->player.pitch = h->player.prev_pitch = dpitch;
    }
    h->torch_on = true;
    h->battery = 1.0f;
    h->torch_dir = nh_dir_from_yaw_pitch(yaw, 0);
    s_cookie = res_texture("cookie");
    h->nav = nh_arena_alloc(level_arena, sizeof(NhNav));
    if (!h->nav)
        return false;
    nh_nav_build(h->nav, &h->world.L, 0.3f);
    spawn_guards(h);
    return true;
}

void heist_end(Heist *h)
{
    world_free(&h->world);
}

static void status(Heist *h, const char *msg)
{
    snprintf(h->status, sizeof(h->status), "%s", msg);
    h->status_ticks = 150;
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

/* The door the player is looking at within reach, or -1. */
static int door_in_front(const Heist *h)
{
    const Level *L = &h->world.L;
    NhVec3 eye = nh_player_eye(&h->player, L, 1.0f);
    NhVec3 dir = nh_dir_from_yaw_pitch(h->player.yaw, h->player.pitch);
    int best = -1;
    float best_d = 2.2f;
    for (int i = 0; i < L->ndoors; i++) {
        const LvDoor *d = &L->doors[i];
        NhVec3 c = d->axis == 0 ? nh_v3((float)d->x * LV_CELL, lv_floor_y(d->floor) + 1.1f, (float)d->z * LV_CELL + 1.0f)
                                : nh_v3((float)d->x * LV_CELL + 1.0f, lv_floor_y(d->floor) + 1.1f, (float)d->z * LV_CELL);
        NhVec3 to = nh_v3_sub(c, eye);
        float dist = nh_v3_len(to);
        if (dist > best_d || dist < 0.01f)
            continue;
        if (nh_v3_dot(nh_v3_scale(to, 1.0f / dist), dir) < 0.6f)
            continue;
        best = i;
        best_d = dist;
    }
    return best;
}

/* Climb into the duct above a grate, or drop out of it. */
static bool try_vent(Heist *h)
{
    NhPlayer *p = &h->player;
    const Level *L = &h->world.L;
    int cx = lv_cell_of(p->pos.x), cz = lv_cell_of(p->pos.z);
    const LvCell *c = lv_cell(L, p->floor, cx, cz);
    if (!c || (c->flags & (LV_C_GRATE | LV_C_VENT)) != (LV_C_GRATE | LV_C_VENT))
        return false;
    NhVec3 ctr = lv_cell_center(p->floor, cx, cz);
    if (nh_v3_dist_xz(ctr, p->pos) > 0.9f)
        return false;
    if (p->mode == NH_MOVE_WALK) {
        p->mode = NH_MOVE_VENT;
        p->pos = p->prev_pos = nh_v3(ctr.x, lv_floor_y(p->floor) + LV_CEIL + 0.05f, ctr.z);
        status(h, "In the vents");
    } else {
        p->mode = NH_MOVE_WALK;
        p->pos = p->prev_pos = nh_v3(ctr.x, lv_floor_y(p->floor) + 0.4f, ctr.z);
        p->grounded = false;
        p->crouch = 1.0f;
        status(h, "Dropped down");
    }
    return true;
}

static void subtitle(Heist *h, const char *msg)
{
    snprintf(h->subtitle, sizeof(h->subtitle), "%s", msg);
    h->subtitle_ticks = 150;
}

static void reset_after_caught(Heist *h)
{
    float yaw;
    NhVec3 start = testlevel_start(&yaw);
    nh_player_init(&h->player, start, yaw);
    h->torch_dir = nh_dir_from_yaw_pitch(yaw, 0);
    spawn_guards(h);
    status(h, "Back to the start");
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
    pv->hiding_prop = -1;
    pv->beam_valid = false;
    if (h->torch_on && !pv->in_vent) {
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

static void update_guards(Heist *h, const NhInput *in, const GameSettings *gs)
{
    NhPlayer *p = &h->player;
    AiWorld *ai = &h->ai;
    /* Takedown: nearest eligible guard roughly in front of the player. */
    h->takedown = -1;
    if (h->caught_t <= 0.0f && p->mode == NH_MOVE_WALK) {
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
            int g = h->takedown;
            ai_knockout(ai, g);
            ai_add_anomaly(ai, AN_BODY, nh_v3_add(ai->g[g].pos, nh_v3(0, 0.3f, 0)), g);
            if (p->nnoise < (int)NH_ARRAY_LEN(p->noises))
                p->noises[p->nnoise++] = (NhNoise){ ai->g[g].pos, 4.5f, NH_NOISE_TAKEDOWN, 0, 1 };
            h->rumble_ticks = 18;
            status(h, "Takedown");
            h->takedown = -1;
        }
    }
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
            break;
        case EV_RADIO:
            subtitle(h, k_barks[BARK_RADIO_SPOTTED]);
            break;
        case EV_SPOTTED:
            status(h, "Spotted!");
            h->rumble_ticks = 12;
            break;
        case EV_ALARM:
            status(h, "ALARM RAISED");
            h->rumble_ticks = 30;
            break;
        case EV_CHECKIN_MISSED:
            subtitle(h, "Control: no answer from your partner. Go and look.");
            break;
        case EV_CAUGHT:
            if (h->caught_t <= 0.0f) {
                h->caught_t = 2.5f;
                status(h, "CAUGHT");
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
            reset_after_caught(h);
    }
    /* Heartbeat rumble while someone is getting suspicious. */
    float sus = ai->max_suspicion;
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

void heist_tick(Heist *h, const NhInput *in, GameSettings *gs)
{
    h->tick++;
    World *w = &h->world;
    NhPlayerInput pi = { 0 };
    pi.ir_valid = in->ir_valid;
    pi.ir_x = in->ir_x;
    pi.ir_y = in->ir_y;
    pi.move_x = in->stick_x;
    pi.move_y = in->stick_y;
    pi.lean = NH_CLAMP(in->nunchuk_roll / NH_DEG2RAD(30.0f), -1.0f, 1.0f);
    pi.crouch_held = (in->held & NH_BTN_Z) != 0;
    pi.crouch_pressed = (in->pressed & NH_BTN_Z) != 0;
    pi.frozen = h->caught_t > 0.0f;
    nh_player_tick(&h->player, &w->L, &pi, &gs->controls, gfx_display_aspect(), HEIST_FOVY);

    if (in->pressed & NH_BTN_B)
        h->torch_on = !h->torch_on;
    if (in->pressed & NH_BTN_1)
        gs->show_debug = !gs->show_debug;
    if (in->pressed & NH_BTN_2)
        h->debug_view = (h->debug_view + 1) % 3;


    if (in->pressed & NH_BTN_C) {
        int d = h->player.mode == NH_MOVE_WALK ? door_in_front(h) : -1;
        if (d < 0)
            try_vent(h);
        else if (d >= 0) {
            LvDoor *door = &w->L.doors[d];
            if (door->locked)
                status(h, "Locked");
            else {
                door->target = door->target > 0.5f ? 0.0f : 1.0f;
                NhVec3 c = lv_cell_center(door->floor, door->x, door->z);
                if (door->target > 0.5f)
                    ai_add_anomaly(&h->ai, AN_DOOR_OPEN, c, d);
                else
                    ai_clear_anomaly(&h->ai, AN_DOOR_OPEN, d);
                if (h->player.nnoise < (int)NH_ARRAY_LEN(h->player.noises))
                    h->player.noises[h->player.nnoise++] = (NhNoise){ c, 4.0f, NH_NOISE_DOOR, 0, 1 };
            }
        }
    }
    update_guards(h, in, gs);
    world_update(w, NH_TICK_DT);

    /* Torch: battery drains while on; aim follows the cursor smoothly. */
    if (h->torch_on) {
        h->battery = fmaxf(0.0f, h->battery - NH_TICK_DT / 900.0f);
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
    pr->fov_deg = 48.0f;
    pr->cookie = s_cookie;
    float b = h->battery < 0.15f ? 0.5f + h->battery * 3.3f : 1.0f;
    nh_light_init(&pr->light, hand, nh_rgba((uint8_t)(255 * b), (uint8_t)(244 * b), (uint8_t)(222 * b), 255));
    nh_light_set_spot(&pr->light, h->torch_dir, 28.0f, NH_SPOT_COS);
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
    guards_draw(&h->gv, &h->ai, &h->world, h->alpha);
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
    if (h->torch_on)
        torch_projector(h, eye, &view, &torch);
    gfx_set_projectors(h->torch_on ? &torch : NULL, gtorch.enabled ? &gtorch : NULL);
    gfx_set_mode(h->debug_view == 2 ? GFX_MODE_THERMAL : GFX_MODE_NORMAL);
    h->alpha = alpha;

    WorldView wv = { view, eye, HEIST_FOVY, gfx_display_aspect(), 0.08f, 90.0f,
                     h->player.mode == NH_MOVE_VENT, h->player.floor, draw_guards, h };
    world_draw(w, &wv);
    if (h->player.mode == NH_MOVE_WALK) {
        guards_draw_glows(&h->gv, &h->ai, w, &view, alpha);
        if (h->debug_view)
            guards_draw_debug(&h->ai, w);
    }
    if (h->torch_on && h->player.mode == NH_MOVE_WALK)
        draw_dust(h, eye, &view, alpha);

    gfx_set_mode(GFX_MODE_NORMAL);
    hud_draw_heist(h, gs, &view);
    if (gs->show_debug)
        hud_draw_debug(h, fps);
    gfx_end_frame();
}
