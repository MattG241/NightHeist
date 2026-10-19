#include "nh_security.h"

#include <math.h>
#include <string.h>

void sec_init(NhSecurity *s, const Level *L, const NhPlan *P)
{
    memset(s, 0, sizeof(*s));
    s->entry_door = -1;
    s->security_circuit = P->security_circuit;
    for (int i = 0; i < P->nent; i++) {
        const NhEntity *e = &P->ent[i];
        if (e->kind == ENT_CAMERA && s->ncam < SEC_CAM_MAX) {
            SecCamera *c = &s->cam[s->ncam++];
            c->ent = (int16_t)i;
            c->pos = e->pos;
            c->base_yaw = c->yaw = e->yaw;
            c->circuit = e->circuit;
            c->room = e->room;
            c->t = (float)(i % 7) * 1.3f; /* out of step with each other */
        } else if (e->kind == ENT_LASER && s->nlas < SEC_LASER_MAX) {
            SecLaser *l = &s->las[s->nlas++];
            l->ent = (int16_t)i;
            l->a = e->pos;
            NhVec3 d = nh_dir_from_yaw_pitch(e->yaw, 0.0f);
            l->b = nh_v3_add(e->pos, nh_v3_scale(d, (float)e->value / 100.0f));
            l->circuit = e->circuit;
            l->room = e->room;
        }
    }
    (void)L;
}

static bool emp_hits(const NhSecurity *s, NhVec3 p)
{
    return s->emp_t > 0.0f && nh_v3_dist(p, s->emp_pos) < s->emp_r;
}

bool sec_camera_live(const NhSecurity *s, const Level *L, int i)
{
    const SecCamera *c = &s->cam[i];
    return L->power[c->circuit & 15] && c->jam_t <= 0.0f && !emp_hits(s, c->pos);
}

bool sec_laser_live(const NhSecurity *s, const Level *L, int i)
{
    const SecLaser *l = &s->las[i];
    return L->power[l->circuit & 15] && !l->mirrored && !emp_hits(s, l->a);
}

float sec_segment_capsule(NhVec3 a, NhVec3 b, NhVec3 feet, float height)
{
    /* Closest point of the segment to the player's axis in the ground plane
     * (exact for the level beams lasers use), then the vertical gap to the
     * capsule's span. */
    float abx = b.x - a.x, abz = b.z - a.z;
    float len2 = abx * abx + abz * abz;
    float t = len2 > 1e-8f ? ((feet.x - a.x) * abx + (feet.z - a.z) * abz) / len2 : 0.0f;
    t = NH_CLAMP(t, 0.0f, 1.0f);
    NhVec3 p = nh_v3_lerp(a, b, t);
    float y = NH_CLAMP(p.y, feet.y, feet.y + height);
    float dx = p.x - feet.x, dz = p.z - feet.z, dy = p.y - y;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

uint32_t sec_tick(NhSecurity *s, const Level *L, const AiWorld *ai, const AiPlayerView *pv, float dt, NhVec3 *where)
{
    uint32_t ev = 0;
    if (s->emp_t > 0.0f)
        s->emp_t -= dt;
    s->max_sus = 0.0f;
    for (int i = 0; i < s->ncam; i++) {
        SecCamera *c = &s->cam[i];
        if (c->jam_t > 0.0f)
            c->jam_t -= dt;
        if (!sec_camera_live(s, L, i)) {
            c->sus = fmaxf(0.0f, c->sus - dt * 0.5f);
            c->vis = 0.0f;
            continue;
        }
        /* Sweep +-40 degrees with a pause at each end (period 12 s). */
        c->t += dt;
        float ph = fmodf(c->t, 12.0f) / 12.0f;
        float s01 = ph < 0.4f ? ph / 0.4f : ph < 0.5f ? 1.0f : ph < 0.9f ? 1.0f - (ph - 0.5f) / 0.4f : 0.0f;
        c->yaw = c->base_yaw + NH_DEG2RAD(-40.0f + 80.0f * nh_smoothstep(0.0f, 1.0f, s01));
        NhVec3 fwd = nh_dir_from_yaw_pitch(c->yaw, NH_DEG2RAD(-25.0f));
        float d;
        float vis = ai_point_visibility(ai, c->pos, fwd, 55.0f, 15.0f, pv, &d);
        c->vis = vis;
        if (vis > 0.0f) {
            float range = 15.0f * (0.22f + 0.78f * NH_CLAMP(pv->light, 0.0f, 1.0f));
            float df = fmaxf(0.1f, 1.0f - (d / range) * (d / range));
            float motion = pv->speed < 0.3f ? 0.5f : pv->speed < 2.5f ? 1.0f : 1.5f;
            float stance = pv->crouched ? 0.7f : 1.0f;
            c->sus += 1.3f * vis * df * motion * stance * (0.4f + 0.6f * pv->light) * dt * ai->difficulty;
        } else {
            c->sus = fmaxf(0.0f, c->sus - dt * 0.15f);
        }
        if (c->sus >= 1.0f) {
            c->sus = 0.0f;
            c->t += 6.0f; /* the operator pans away */
            ev |= SEC_EV_CAMERA;
            if (where)
                *where = pv->pos;
        }
        s->max_sus = fmaxf(s->max_sus, c->sus);
    }
    if (!pv->in_vent && pv->hiding_prop < 0) {
        for (int i = 0; i < s->nlas; i++) {
            if (!sec_laser_live(s, L, i))
                continue;
            SecLaser *l = &s->las[i];
            if (fabsf(l->a.y - pv->pos.y) > 3.0f)
                continue;
            if (sec_segment_capsule(l->a, l->b, pv->pos, pv->height) < 0.28f) {
                ev |= SEC_EV_LASER;
                if (where)
                    *where = pv->pos;
            }
        }
    }
    if (s->entry_door >= 0) {
        s->entry_t -= dt;
        if (s->disarmed) {
            s->entry_door = -1;
        } else if (s->entry_t <= 0.0f) {
            ev |= SEC_EV_ENTRY_TIMEOUT;
            if (where)
                *where = lv_cell_center(L->doors[s->entry_door].floor, L->doors[s->entry_door].x,
                                        L->doors[s->entry_door].z);
            s->entry_door = -1;
        }
    }
    return ev;
}

void sec_door_opened(NhSecurity *s, const Level *L, int door)
{
    const LvDoor *d = &L->doors[door];
    if (!(d->flags & LV_DOOR_F_ALARMED) || s->disarmed || s->entry_door >= 0 || !L->power[s->security_circuit & 15])
        return;
    s->entry_door = door;
    s->entry_t = 15.0f;
}

void sec_disarm(NhSecurity *s)
{
    s->disarmed = true;
    s->entry_door = -1;
}

void sec_emp(NhSecurity *s, NhVec3 pos, float radius, float seconds)
{
    s->emp_pos = pos;
    s->emp_r = radius;
    s->emp_t = seconds;
}
