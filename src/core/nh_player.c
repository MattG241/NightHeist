#include "nh_player.h"

#include <math.h>
#include <string.h>

#include "nh_fixedstep.h"

#define WALK_SPEED 2.4f
#define RUN_SPEED 4.2f
#define CROUCH_SPEED 1.25f
#define VENT_SPEED 1.0f
#define GRAVITY 18.0f
#define LEAN_DIST 0.45f
#define PITCH_LIMIT NH_DEG2RAD(75.0f)
#define STEP_LEN_WALK 0.75f

void nh_controls_default(NhControlSettings *s)
{
    s->ir_smoothing = 0.35f;
    s->box_w = 0.55f;
    s->box_h = 0.5f;
    s->turn_speed = 2.6f;
    s->crouch_toggle = false;
    s->invert_y = false;
}

void nh_player_init(NhPlayer *p, NhVec3 feet, float yaw)
{
    memset(p, 0, sizeof(*p));
    p->pos = p->prev_pos = feet;
    p->yaw = p->prev_yaw = yaw;
    p->cursor_x = 320.0f;
    p->cursor_y = 240.0f;
    p->grounded = true;
    p->floor = lv_floor_of(feet.y);
}

float nh_player_eye_height(const NhPlayer *p)
{
    if (p->mode == NH_MOVE_VENT)
        return 0.35f;
    return nh_lerpf(1.62f, 0.98f, p->crouch);
}

void nh_player_turn_amount(const NhPlayer *p, const NhControlSettings *cs, float *tx, float *ty)
{
    float hw = 320.0f * cs->box_w, hh = 240.0f * cs->box_h;
    float dx = p->cursor_x - 320.0f, dy = p->cursor_y - 240.0f;
    float ex = fabsf(dx) - hw, ey = fabsf(dy) - hh;
    /* Ramp from 0 at the box edge to 1 at the screen edge, eased in. */
    *tx = ex > 0 ? (dx > 0 ? 1.0f : -1.0f) * nh_smoothstep(0.0f, 320.0f - hw, ex) : 0.0f;
    *ty = ey > 0 ? (dy > 0 ? 1.0f : -1.0f) * nh_smoothstep(0.0f, 240.0f - hh, ey) : 0.0f;
}

static void emit_noise(NhPlayer *p, NhVec3 pos, float radius, int kind, int surface)
{
    if (p->nnoise >= (int)NH_ARRAY_LEN(p->noises))
        return;
    NhNoise *n = &p->noises[p->nnoise++];
    n->pos = pos;
    n->radius = radius * (p->noise_scale > 0.0f ? p->noise_scale : 1.0f);
    n->kind = (uint8_t)kind;
    n->surface = (uint8_t)surface;
    n->from_player = 1;
}

/* Surfaces differ in how loud a footstep is (carpet quiet, metal loud). */
static const float k_surface_loudness[LV_SURF_COUNT] = { 1.0f, 0.45f, 1.15f, 1.0f, 1.25f, 1.5f, 0.9f, 0.5f };

static bool vent_cell(const Level *L, int f, float x, float z)
{
    const LvCell *c = lv_cell(L, f, lv_cell_of(x), lv_cell_of(z));
    return c && (c->flags & LV_C_VENT);
}

void nh_player_tick(NhPlayer *p, const Level *L, const NhPlayerInput *in, const NhControlSettings *cs,
                    float aspect, float fovy_deg)
{
    (void)aspect;
    (void)fovy_deg;
    p->prev_pos = p->pos;
    p->prev_yaw = p->yaw;
    p->prev_pitch = p->pitch;
    p->prev_lean = p->lean;
    p->nnoise = 0;
    const float dt = NH_TICK_DT;

    /* ---- look ---- */
    if (in->ir_valid && !in->frozen) {
        float k = 1.0f - NH_CLAMP(cs->ir_smoothing, 0.0f, 0.95f);
        p->cursor_x = nh_lerpf(p->cursor_x, NH_CLAMP(in->ir_x, 0.0f, 640.0f), k);
        p->cursor_y = nh_lerpf(p->cursor_y, NH_CLAMP(in->ir_y, 0.0f, 480.0f), k);
        float tx, ty;
        nh_player_turn_amount(p, cs, &tx, &ty);
        float pitch_sign = cs->invert_y ? 1.0f : -1.0f;
        p->yaw = nh_wrap_angle(p->yaw - tx * cs->turn_speed * dt);
        p->pitch = NH_CLAMP(p->pitch + pitch_sign * ty * cs->turn_speed * 0.7f * dt, -PITCH_LIMIT, PITCH_LIMIT);
    }

    /* ---- stance ---- */
    if (cs->crouch_toggle) {
        if (in->crouch_pressed)
            p->crouch_latched = !p->crouch_latched;
    } else {
        p->crouch_latched = in->crouch_held;
    }
    float crouch_target = p->crouch_latched ? 1.0f : 0.0f;
    /* Can't stand up under something low (vent exits, under desks). */
    if (crouch_target < p->crouch && p->mode == NH_MOVE_WALK) {
        float probe_top = p->pos.y + NH_PLAYER_HEIGHT;
        NhVec3 test = p->pos;
        if (lv_collide_cylinder(L, &test, NH_PLAYER_RADIUS * 0.9f, p->pos.y + NH_PLAYER_CROUCH_HEIGHT, probe_top))
            crouch_target = p->crouch;
    }
    p->crouch = nh_approach(p->crouch, crouch_target, dt * 5.0f);

    /* ---- move ---- */
    float mx = in->frozen ? 0.0f : in->move_x, my = in->frozen ? 0.0f : in->move_y;
    float mag = sqrtf(mx * mx + my * my);
    if (mag > 1.0f) {
        mx /= mag;
        my /= mag;
        mag = 1.0f;
    }
    float speed;
    if (p->mode == NH_MOVE_VENT)
        speed = VENT_SPEED;
    else if (p->crouch > 0.5f)
        speed = CROUCH_SPEED;
    else
        speed = mag > 0.9f ? RUN_SPEED : WALK_SPEED;
    /* Forward is -Z rotated by yaw; right is +X rotated by yaw. */
    float s = sinf(p->yaw), c = cosf(p->yaw);
    float vx = (mx * c - my * s) * speed, vz = (-mx * s - my * c) * speed;

    float height = p->mode == NH_MOVE_VENT ? NH_VENT_HEIGHT
                                           : nh_lerpf(NH_PLAYER_HEIGHT, NH_PLAYER_CROUCH_HEIGHT, p->crouch);
    NhVec3 old = p->pos;
    NhVec3 np = p->pos;

    if (p->mode == NH_MOVE_VENT) {
        /* Ducts: move one axis at a time and refuse to leave vent cells. */
        float nx = np.x + vx * dt;
        if (vent_cell(L, p->floor, nx + (vx > 0 ? 0.25f : -0.25f), np.z))
            np.x = nx;
        float nz = np.z + vz * dt;
        if (vent_cell(L, p->floor, np.x, nz + (vz > 0 ? 0.25f : -0.25f)))
            np.z = nz;
        p->pos = np;
    } else {
        /* Move per axis so a too-high step on one axis still lets us slide. */
        float axes[2][2] = { { vx * dt, 0.0f }, { 0.0f, vz * dt } };
        for (int a = 0; a < 2; a++) {
            NhVec3 t = nh_v3(np.x + axes[a][0], np.y, np.z + axes[a][1]);
            float g = lv_ground_height(L, t.x, t.z, np.y);
            if (g > np.y + LV_STEP_UP + 0.01f)
                continue; /* ledge or stair side */
            np = t;
        }
        lv_collide_cylinder(L, &np, NH_PLAYER_RADIUS, np.y + LV_STEP_UP, np.y + height);

        /* Vertical: snap to ground when walking, fall otherwise. */
        float ground = lv_ground_height(L, np.x, np.z, np.y);
        if (np.y <= ground + 0.05f || (p->grounded && np.y - ground < LV_STEP_UP + 0.05f && p->vel_y <= 0.0f)) {
            np.y = ground;
            p->vel_y = 0.0f;
            p->grounded = true;
        } else {
            p->vel_y -= GRAVITY * dt;
            np.y += p->vel_y * dt;
            p->grounded = false;
            if (np.y < ground) {
                /* Landing makes noise proportional to the fall. */
                if (p->vel_y < -6.0f)
                    emit_noise(p, np, 6.0f + -p->vel_y, NH_NOISE_FOOTSTEP, LV_SURF_CONCRETE);
                np.y = ground;
                p->vel_y = 0.0f;
                p->grounded = true;
            }
        }
        if (np.y < -50.0f)
            np = old; /* never fall out of the world */
        p->pos = np;
        p->floor = lv_floor_of(np.y + 0.3f);
    }

    float moved = nh_v3_dist_xz(p->pos, old);
    p->speed = moved / dt;

    /* ---- footsteps ---- */
    if (p->grounded && p->mode == NH_MOVE_WALK && moved > 0.001f) {
        float step_len = STEP_LEN_WALK * (p->speed > WALK_SPEED + 0.2f ? 1.35f : 1.0f);
        p->step_phase += moved;
        if (p->step_phase >= step_len) {
            p->step_phase -= step_len;
            const LvCell *cell = lv_cell(L, p->floor, lv_cell_of(p->pos.x), lv_cell_of(p->pos.z));
            int surf = cell ? cell->surface : LV_SURF_CONCRETE;
            float base = p->speed > WALK_SPEED + 0.2f ? 11.0f : (p->crouch > 0.5f ? 1.5f : 5.0f);
            emit_noise(p, p->pos, base * k_surface_loudness[surf], NH_NOISE_FOOTSTEP, surf);
        }
    } else if (p->mode == NH_MOVE_VENT && moved > 0.001f) {
        p->step_phase += moved;
        if (p->step_phase >= 1.2f) {
            p->step_phase = 0.0f;
            emit_noise(p, p->pos, 2.5f, NH_NOISE_FOOTSTEP, LV_SURF_METAL);
        }
    }

    /* ---- lean: only as far as the wall allows ---- */
    float lean_target = (in->frozen || p->mode == NH_MOVE_VENT) ? 0.0f : NH_CLAMP(in->lean, -1.0f, 1.0f);
    if (fabsf(lean_target) > 0.01f) {
        NhVec3 eye = nh_v3(p->pos.x, p->pos.y + nh_player_eye_height(p), p->pos.z);
        NhVec3 side = nh_v3(c * lean_target * (LEAN_DIST + 0.2f), 0.0f, -s * lean_target * (LEAN_DIST + 0.2f));
        float t;
        if (lv_raycast(L, eye, nh_v3_add(eye, side), &t))
            lean_target *= fmaxf(0.0f, t * (LEAN_DIST + 0.2f) - 0.2f) / LEAN_DIST;
    }
    p->lean = nh_approach(p->lean, lean_target, dt * 4.0f);
}

NhVec3 nh_player_eye(const NhPlayer *p, const Level *L, float alpha)
{
    (void)L;
    NhVec3 feet = nh_v3_lerp(p->prev_pos, p->pos, alpha);
    float yaw = p->prev_yaw + nh_wrap_angle(p->yaw - p->prev_yaw) * alpha;
    float lean = nh_lerpf(p->prev_lean, p->lean, alpha);
    float s = sinf(yaw), c = cosf(yaw);
    float lean_drop = fabsf(lean) * 0.06f;
    return nh_v3(feet.x + c * lean * LEAN_DIST, feet.y + nh_player_eye_height(p) - lean_drop,
                 feet.z - s * lean * LEAN_DIST);
}

NhVec3 nh_player_aim_dir(const NhPlayer *p, float aspect, float fovy_deg)
{
    float t = tanf(NH_DEG2RAD(fovy_deg) * 0.5f);
    float vx = (p->cursor_x / 320.0f - 1.0f) * t * aspect;
    float vy = (1.0f - p->cursor_y / 240.0f) * t;
    NhMat34 rot;
    nh_m34_from_yaw_pitch(&rot, nh_v3(0, 0, 0), p->yaw, p->pitch);
    return nh_v3_norm(nh_m34_mul_dir(&rot, nh_v3(vx, vy, -1.0f)));
}
