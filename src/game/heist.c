#include "heist.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/nh_fixedstep.h"
#include "hud.h"
#include "res.h"
#include "testlevel.h"

static GfxTexture s_cookie;

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
    h->torch_on = true;
    h->battery = 1.0f;
    h->torch_dir = nh_dir_from_yaw_pitch(yaw, 0);
    s_cookie = res_texture("cookie");
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
            else
                door->target = door->target > 0.5f ? 0.0f : 1.0f;
        }
    }
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
    gfx_set_lights(NULL, 0);
    GfxProjector torch;
    if (h->torch_on)
        torch_projector(h, eye, &view, &torch);
    gfx_set_projectors(h->torch_on ? &torch : NULL, NULL);

    WorldView wv = { view, eye, HEIST_FOVY, gfx_display_aspect(), 0.08f, 90.0f,
                     h->player.mode == NH_MOVE_VENT, h->player.floor };
    world_draw(w, &wv);

    hud_draw_heist(h, gs);
    if (gs->show_debug)
        hud_draw_debug(h, fps);
    gfx_end_frame();
}
