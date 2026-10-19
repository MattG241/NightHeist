#include "nh_bake.h"

#include <math.h>

void nh_bake_default_params(NhBakeParams *p)
{
    p->ambient_indoor = nh_rgba(14, 15, 21, 255);
    p->ambient_outdoor = nh_rgba(34, 40, 60, 255);
    p->ao_strength = 0.45f;
}

void nh_bake_light(const LvLight *l, NhLight *out)
{
    nh_light_init(out, l->pos, l->color);
    if (l->cone > 0.0f) {
        float deg = NH_RAD2DEG(acosf(NH_CLAMP(l->cone, -1.0f, 1.0f)));
        nh_light_set_spot(out, l->dir, deg, NH_SPOT_COS2);
    }
    /* Half brightness at 30% of the radius, smooth medium falloff. */
    nh_light_set_dist(out, l->radius * 0.3f, 0.5f, NH_DIST_MEDIUM);
}

static bool light_on(const Level *L, const LvLight *l)
{
    return l->on && (l->emergency || L->power[l->circuit & 15]);
}

int nh_bake_room_lights(const Level *L, int room, int *out, int max)
{
    const LvRoom *R = &L->rooms[room];
    float x0 = (float)R->x0 * LV_CELL, x1 = (float)(R->x1 + 1) * LV_CELL;
    float z0 = (float)R->z0 * LV_CELL, z1 = (float)(R->z1 + 1) * LV_CELL;
    float y0 = lv_floor_y(R->floor) - 0.5f, y1 = lv_floor_y(R->floor) + LV_STOREY + 0.5f;
    if (room == LV_ROOM_OUTSIDE)
        y1 = lv_floor_y(L->floors) + 2.0f;
    int n = 0;
    for (int i = 0; i < L->nlights && n < max; i++) {
        const LvLight *l = &L->lights[i];
        if (!light_on(L, l))
            continue;
        float dx = fmaxf(fmaxf(x0 - l->pos.x, l->pos.x - x1), 0.0f);
        float dz = fmaxf(fmaxf(z0 - l->pos.z, l->pos.z - z1), 0.0f);
        float dy = fmaxf(fmaxf(y0 - l->pos.y, l->pos.y - y1), 0.0f);
        if (dx * dx + dy * dy + dz * dz < l->radius * l->radius)
            out[n++] = i;
    }
    return n;
}

static float ao_axis(float d, float r)
{
    return 1.0f - expf(-d / r);
}

/* Ambient occlusion from the distance to the room's bounding surfaces,
 * ignoring the axis the surface itself faces. */
static float room_ao(const Level *L, int room, NhVec3 p, NhVec3 n)
{
    if (room < LV_ROOM_FIRST || (L->rooms[room].flags & LV_R_OUTDOOR))
        return 1.0f;
    const LvRoom *R = &L->rooms[room];
    float base = lv_floor_y(R->floor);
    float dx = fminf(p.x - (float)R->x0 * LV_CELL, (float)(R->x1 + 1) * LV_CELL - p.x) - 0.08f;
    float dz = fminf(p.z - (float)R->z0 * LV_CELL, (float)(R->z1 + 1) * LV_CELL - p.z) - 0.08f;
    float dyf = p.y - base, dyc = base + LV_CEIL - p.y;
    float ax = fabsf(n.x) > 0.7f ? 1.0f : ao_axis(fmaxf(dx, 0.0f), 0.35f);
    float az = fabsf(n.z) > 0.7f ? 1.0f : ao_axis(fmaxf(dz, 0.0f), 0.35f);
    float ay = fabsf(n.y) > 0.7f ? 1.0f : fminf(ao_axis(fmaxf(dyf, 0.0f), 0.25f) * 0.6f + 0.4f,
                                                ao_axis(fmaxf(dyc, 0.0f), 0.3f) * 0.3f + 0.7f);
    return ax * az * ay;
}

static NhVec3 sample_with(const Level *L, NhVec3 p, NhVec3 n, int room, const int *lights, int nl,
                          const NhBakeParams *bp, float ao)
{
    bool outdoor = room == LV_ROOM_OUTSIDE || (room >= LV_ROOM_FIRST && (L->rooms[room].flags & LV_R_OUTDOOR));
    NhColor a = outdoor ? bp->ambient_outdoor : bp->ambient_indoor;
    float k = ao * (1.0f - bp->ao_strength) + bp->ao_strength * ao * ao;
    NhVec3 sum = nh_v3(a.r / 255.0f * k, a.g / 255.0f * k, a.b / 255.0f * k);
    /* Offset off the surface so raycasts don't hit the surface itself. */
    NhVec3 probe = nh_v3_add(p, nh_v3_scale(n, 0.05f));
    for (int i = 0; i < nl; i++) {
        const LvLight *ll = &L->lights[lights[i]];
        NhLight hl;
        nh_bake_light(ll, &hl);
        float f = nh_light_factor(&hl, p, n);
        /* Cheap one-bounce indirect light: lamps in this room fill the whole
         * room (ceilings, shadowed sides) a little, ignoring orientation. */
        bool same = ll->room == room && room >= LV_ROOM_FIRST;
        float bounce = same ? nh_light_factor(&hl, p, nh_v3(0, 0, 0)) * 0.3f * ao : 0.0f;
        if (n.y < -0.7f)
            bounce *= 2.2f; /* ceilings are lit mostly by light bounced off the floor */
        if (f < 0.004f && bounce < 0.004f)
            continue;
        if (!same && lv_raycast(L, probe, ll->pos, NULL))
            continue;
        f += bounce;
        sum.x += ll->color.r / 255.0f * f;
        sum.y += ll->color.g / 255.0f * f;
        sum.z += ll->color.b / 255.0f * f;
    }
    return sum;
}

NhVec3 nh_bake_sample(const Level *L, NhVec3 p, NhVec3 n, int room, const NhBakeParams *bp)
{
    if (room <= 0)
        room = lv_room_at(L, lv_floor_of(p.y), lv_cell_of(p.x), lv_cell_of(p.z));
    if (room <= 0)
        room = LV_ROOM_OUTSIDE;
    int lights[NH_BAKE_ROOM_LIGHTS];
    int nl = nh_bake_room_lights(L, room, lights, NH_BAKE_ROOM_LIGHTS);
    float ao = (n.x != 0.0f || n.y != 0.0f || n.z != 0.0f) ? room_ao(L, room, p, n) : 1.0f;
    return sample_with(L, p, n, room, lights, nl, bp, ao);
}

float nh_bake_light_level(const Level *L, NhVec3 p, const NhBakeParams *bp)
{
    NhVec3 c = nh_bake_sample(L, p, nh_v3(0, 0, 0), 0, bp);
    float lum = c.x * 0.3f + c.y * 0.59f + c.z * 0.11f;
    return NH_CLAMP(lum, 0.0f, 1.0f);
}

static uint8_t to8(float v)
{
    v = v * 255.0f + 0.5f;
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

void nh_bake_mesh(const Level *L, int room, NhMesh *m, const NhColor *albedo, const NhBakeParams *bp)
{
    if (!m->colors)
        return;
    int lights[NH_BAKE_ROOM_LIGHTS];
    int nl = nh_bake_room_lights(L, room, lights, NH_BAKE_ROOM_LIGHTS);
    for (uint32_t i = 0; i < m->vcount; i++) {
        NhVec3 p = nh_vert_pos(&m->verts[i]), n = nh_vert_nrm(&m->verts[i]);
        NhVec3 c = sample_with(L, p, n, room, lights, nl, bp, room_ao(L, room, p, n));
        NhColor t = albedo ? albedo[i] : nh_rgba(255, 255, 255, 255);
        m->colors[i] = nh_rgba(to8(fminf(c.x, 1.0f) * t.r / 255.0f), to8(fminf(c.y, 1.0f) * t.g / 255.0f),
                               to8(fminf(c.z, 1.0f) * t.b / 255.0f), t.a);
    }
}
