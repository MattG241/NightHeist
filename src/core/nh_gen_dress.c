/* Night Heist - furniture, set-piece rooms, loot dressing and the street.
 *
 * Props snap to a 0.5 m occupancy grid so nothing overlaps, nothing blocks a
 * doorway, stair or vent grate, and tall furniture never covers a window.
 * Set pieces are hand-designed room layouts with their own gameplay hooks
 * (lasers, safes, terminals, display cases); every other room gets
 * furniture by function. */
#include <math.h>
#include <string.h>

#include "nh_gen_internal.h"

static const int k_dx[4] = { 1, 0, -1, 0 };
static const int k_dz[4] = { 0, 1, 0, -1 };

/* ---- occupancy ---------------------------------------------------------------- */

static void occ_range(float x, float z, float hx, float hz, int *ox0, int *oz0, int *ox1, int *oz1)
{
    const float res = LV_CELL / OCC_RES;
    *ox0 = (int)floorf((x - hx) / res + 0.02f);
    *ox1 = (int)floorf((x + hx) / res - 0.02f);
    *oz0 = (int)floorf((z - hz) / res + 0.02f);
    *oz1 = (int)floorf((z + hz) / res - 0.02f);
}

bool gen_occ_free(GenCtx *c, int f, float x, float z, float hx, float hz)
{
    int ox0, oz0, ox1, oz1;
    occ_range(x, z, hx, hz, &ox0, &oz0, &ox1, &oz1);
    int room = lv_room_at(c->L, f, lv_cell_of(x), lv_cell_of(z));
    for (int oz = oz0; oz <= oz1; oz++)
        for (int ox = ox0; ox <= ox1; ox++) {
            if (ox < 0 || oz < 0 || ox >= c->L->w * OCC_RES || oz >= c->L->d * OCC_RES)
                return false;
            if (c->occ[f][oz][ox])
                return false;
            if (c->L->cells[f][oz / OCC_RES][ox / OCC_RES].room != room)
                return false;
        }
    return true;
}

void gen_occ_mark(GenCtx *c, int f, float x, float z, float hx, float hz, uint8_t v)
{
    int ox0, oz0, ox1, oz1;
    occ_range(x, z, hx, hz, &ox0, &oz0, &ox1, &oz1);
    for (int oz = oz0; oz <= oz1; oz++)
        for (int ox = ox0; ox <= ox1; ox++)
            if (ox >= 0 && oz >= 0 && ox < c->L->w * OCC_RES && oz < c->L->d * OCC_RES && !c->occ[f][oz][ox])
                c->occ[f][oz][ox] = v;
}

static void rotated_half(int type, float yaw, float *hx, float *hz)
{
    NhVec3 h = nh_prop_info[type].half;
    bool swap = fabsf(sinf(yaw)) > 0.7f;
    *hx = swap ? h.z : h.x;
    *hz = swap ? h.x : h.z;
}

int gen_prop(GenCtx *c, int type, int f, float x, float y, float z, float yaw, int extra_flags)
{
    const NhPropInfo *pi = &nh_prop_info[type];
    bool on_floor = y < lv_floor_y(f) + 0.2f;
    if (on_floor) {
        float hx, hz;
        rotated_half(type, yaw, &hx, &hz);
        if (!gen_occ_free(c, f, x, z, hx, hz))
            return -1;
        gen_occ_mark(c, f, x, z, hx, hz, 1);
    }
    int i = lv_add_prop(c->L, type, f, x, y, z, yaw, pi->half, pi->flags | extra_flags);
    if (i >= 0)
        c->L->props[i].room = (uint8_t)lv_room_at(c->L, f, lv_cell_of(x), lv_cell_of(z));
    return i;
}

static bool wall_ok(const GenCtx *c, int f, int x, int z, int side, float height)
{
    const LvEdge *e = lv_edge_side_c(c->L, f, x, z, side);
    if (!e)
        return false;
    if (e->type == LV_E_WALL)
        return true;
    return e->type == LV_E_WINDOW && height < 0.85f;
}

int gen_prop_wall(GenCtx *c, int type, int f, int x, int z, int side, float along, int extra_flags)
{
    const NhPropInfo *pi = &nh_prop_info[type];
    if (!wall_ok(c, f, x, z, side, pi->half.y * 2.0f))
        return -1;
    NhVec3 ctr = lv_cell_center(f, x, z);
    float off = LV_CELL * 0.5f - pi->half.z - 0.03f;
    float px = ctr.x + (float)k_dx[side] * off + (float)k_dz[side] * along;
    float pz = ctr.z + (float)k_dz[side] * off + (float)k_dx[side] * along;
    return gen_prop(c, type, f, px, lv_floor_y(f), pz, gen_wall_yaw(side), extra_flags);
}

int gen_prop_on_wall(GenCtx *c, int type, int f, int x, int z, int side, float along, float y)
{
    const NhPropInfo *pi = &nh_prop_info[type];
    const LvEdge *e = lv_edge_side_c(c->L, f, x, z, side);
    if (!e || e->type != LV_E_WALL)
        return -1;
    NhVec3 ctr = lv_cell_center(f, x, z);
    float off = LV_CELL * 0.5f - pi->half.z - 0.01f;
    float px = ctr.x + (float)k_dx[side] * off + (float)k_dz[side] * along;
    float pz = ctr.z + (float)k_dz[side] * off + (float)k_dx[side] * along;
    return gen_prop(c, type, f, px, lv_floor_y(f) + y, pz, gen_wall_yaw(side), 0);
}

int gen_entity(GenCtx *c, int kind, int room, int prop, NhVec3 pos, float yaw)
{
    NhPlan *P = c->P;
    if (P->nent >= GEN_ENT_MAX)
        return -1;
    int i = P->nent++;
    NhEntity *e = &P->ent[i];
    memset(e, 0, sizeof(*e));
    e->kind = (uint8_t)kind;
    e->room = (uint8_t)room;
    e->floor = c->L->rooms[room].floor;
    e->prop = (int16_t)prop;
    e->door = -1;
    e->pos = pos;
    e->yaw = yaw;
    e->circuit = c->L->rooms[room].circuit;
    if (prop >= 0)
        c->L->props[prop].entity = (uint16_t)(i + 1);
    return i;
}

static const uint16_t k_tier_value[5][2] = { { 5, 20 }, { 20, 80 }, { 100, 300 }, { 300, 1000 }, { 2000, 6000 } };

int gen_loot(GenCtx *c, int room, int prop_type, float x, float y, float z, int tier, int flags)
{
    tier = NH_CLAMP(tier, 0, 4);
    int f = c->L->rooms[room].floor;
    int p = gen_prop(c, prop_type, f, x, y, z, (float)gen_ri(c, 0, 3) * NH_PI * 0.5f, LV_P_DYNAMIC | LV_P_INTERACT);
    if (p < 0)
        return -1;
    int e = gen_entity(c, ENT_LOOT, room, p, nh_v3(x, y, z), c->L->props[p].yaw);
    if (e >= 0) {
        NhEntity *en = &c->P->ent[e];
        en->tier = (uint8_t)tier;
        en->flags = (uint8_t)flags;
        en->value = (uint16_t)gen_ri(c, k_tier_value[tier][0], k_tier_value[tier][1]);
        if (prop_type == PROP_GOLD_BARS || prop_type == PROP_STATUE || prop_type == PROP_PAINTING)
            en->flags |= ENTF_HEAVY;
    }
    return e;
}

/* ---- room helpers --------------------------------------------------------------- */

typedef struct Slot {
    uint8_t x, z, side;
} Slot;

/* Wall slots of a room (cell + side facing a wall), shuffled. */
static int wall_slots(GenCtx *c, int room, Slot *out, int max)
{
    const Level *L = c->L;
    const LvRoom *r = &L->rooms[room];
    int n = 0;
    for (int z = r->z0; z <= r->z1; z++)
        for (int x = r->x0; x <= r->x1; x++) {
            if (L->cells[r->floor][z][x].room != room || L->cells[r->floor][z][x].stair)
                continue;
            for (int s = 0; s < 4; s++) {
                int nx = x + k_dx[s], nz = z + k_dz[s];
                if (lv_in_bounds(L, r->floor, nx, nz) && L->cells[r->floor][nz][nx].room == room)
                    continue;
                const LvEdge *e = lv_edge_side_c(L, r->floor, x, z, s);
                if (!e || (e->type != LV_E_WALL && e->type != LV_E_WINDOW))
                    continue;
                if (n < max)
                    out[n++] = (Slot){ (uint8_t)x, (uint8_t)z, (uint8_t)s };
            }
        }
    for (int i = n - 1; i > 0; i--) {
        int j = gen_ri(c, 0, i);
        Slot t = out[i];
        out[i] = out[j];
        out[j] = t;
    }
    return n;
}

/* Up to `count` props of a type against random walls. */
static int along_walls(GenCtx *c, int room, int type, int count)
{
    Slot s[256];
    int n = wall_slots(c, room, s, 256), placed = 0;
    int f = c->L->rooms[room].floor;
    for (int i = 0; i < n && placed < count; i++) {
        float along = (float)gen_ri(c, -2, 2) * 0.2f;
        if (gen_prop_wall(c, type, f, s[i].x, s[i].z, s[i].side, along, 0) >= 0)
            placed++;
    }
    return placed;
}

/* Fill every wall slot on chosen sides (bookshelves, lockers, racks). */
static int line_walls(GenCtx *c, int room, int type, int side_mask, float spacing)
{
    const LvRoom *r = &c->L->rooms[room];
    int f = r->floor, placed = 0;
    float w = nh_prop_info[type].half.x * 2.0f + spacing;
    int per_cell = NH_MAX(1, (int)(LV_CELL / w));
    for (int z = r->z0; z <= r->z1; z++)
        for (int x = r->x0; x <= r->x1; x++)
            for (int s = 0; s < 4; s++) {
                if (!(side_mask & (1 << s)))
                    continue;
                for (int k = 0; k < per_cell; k++) {
                    float along = ((float)k + 0.5f) * (LV_CELL / (float)per_cell) - LV_CELL * 0.5f;
                    if (gen_prop_wall(c, type, f, x, z, s, along, 0) >= 0)
                        placed++;
                }
            }
    return placed;
}

static NhVec3 room_center(const LvRoom *r)
{
    return nh_v3(((float)r->x0 + (float)r->x1 + 1.0f) * LV_CELL * 0.5f, lv_floor_y(r->floor),
                 ((float)r->z0 + (float)r->z1 + 1.0f) * LV_CELL * 0.5f);
}

static float room_w(const LvRoom *r) { return (float)gen_rw(r) * LV_CELL; }
static float room_d(const LvRoom *r) { return (float)gen_rd(r) * LV_CELL; }

/* Regular grid of a prop in the room interior with a free margin. */
static int grid(GenCtx *c, int room, int type, float sx, float sz, float margin, float yaw)
{
    const LvRoom *r = &c->L->rooms[room];
    float X0 = (float)r->x0 * LV_CELL + margin, X1 = (float)(r->x1 + 1) * LV_CELL - margin;
    float Z0 = (float)r->z0 * LV_CELL + margin, Z1 = (float)(r->z1 + 1) * LV_CELL - margin;
    if (X1 < X0 || Z1 < Z0)
        return 0;
    int nx = (int)((X1 - X0) / sx) + 1, nz = (int)((Z1 - Z0) / sz) + 1;
    float ox = X0 + ((X1 - X0) - (float)(nx - 1) * sx) * 0.5f, oz = Z0 + ((Z1 - Z0) - (float)(nz - 1) * sz) * 0.5f;
    int placed = 0;
    for (int iz = 0; iz < nz; iz++)
        for (int ix = 0; ix < nx; ix++)
            if (gen_prop(c, type, r->floor, ox + (float)ix * sx, lv_floor_y(r->floor), oz + (float)iz * sz, yaw, 0) >= 0)
                placed++;
    return placed;
}

/* Parallel rows along the room's long axis with aisles between them. */
static int rows(GenCtx *c, int room, int type, float aisle, float margin, bool face_alternate)
{
    const LvRoom *r = &c->L->rooms[room];
    bool along_x = room_w(r) >= room_d(r);
    const NhVec3 h = nh_prop_info[type].half;
    float span = along_x ? room_w(r) : room_d(r), across = along_x ? room_d(r) : room_w(r);
    int per_row = (int)((span - 2.0f * margin) / (h.x * 2.0f + 0.02f));
    int nrows = (int)((across - 2.0f * margin + aisle) / (h.z * 2.0f + aisle));
    if (per_row < 1 || nrows < 1)
        return 0;
    NhVec3 ctr = room_center(r);
    float row_pitch = h.z * 2.0f + aisle;
    float first = -((float)(nrows - 1) * row_pitch) * 0.5f;
    int placed = 0;
    for (int i = 0; i < nrows; i++) {
        float off = first + (float)i * row_pitch;
        float yaw = face_alternate && (i & 1) ? NH_PI : 0.0f;
        for (int k = 0; k < per_row; k++) {
            float a = -((float)(per_row - 1) * (h.x * 2.0f + 0.02f)) * 0.5f + (float)k * (h.x * 2.0f + 0.02f);
            float x = along_x ? ctr.x + a : ctr.x + off, z = along_x ? ctr.z + off : ctr.z + a;
            float yw = along_x ? yaw : yaw + NH_PI * 0.5f;
            if (gen_prop(c, type, r->floor, x, ctr.y, z, yw, 0) >= 0)
                placed++;
        }
    }
    return placed;
}

/* Something in the middle of the room. */
static int center_prop(GenCtx *c, int room, int type, float dx, float dz, float yaw)
{
    const LvRoom *r = &c->L->rooms[room];
    NhVec3 ctr = room_center(r);
    bool along_x = room_w(r) >= room_d(r);
    float y = along_x ? yaw : yaw + NH_PI * 0.5f;
    return gen_prop(c, type, r->floor, ctr.x + dx, ctr.y, ctr.z + dz, y, 0);
}

/* Chairs around a table centred in the room. */
static void chairs_around(GenCtx *c, int room, int table, int chair_type)
{
    (void)room;
    if (table < 0)
        return;
    const LvProp *t = &c->L->props[table];
    float hx, hz;
    rotated_half(t->type, t->yaw, &hx, &hz);
    int f = t->floor;
    int n = (int)(hx * 2.0f / 0.9f);
    for (int i = 0; i < n; i++) {
        float x = t->x - hx + 0.45f + (float)i * 0.9f;
        gen_prop(c, chair_type, f, x, lv_floor_y(f), t->z - hz - 0.4f, NH_PI, 0);
        gen_prop(c, chair_type, f, x, lv_floor_y(f), t->z + hz + 0.4f, 0.0f, 0);
    }
    if (hz > 0.6f) {
        gen_prop(c, chair_type, f, t->x - hx - 0.4f, lv_floor_y(f), t->z, -NH_PI * 0.5f, 0);
        gen_prop(c, chair_type, f, t->x + hx + 0.4f, lv_floor_y(f), t->z, NH_PI * 0.5f, 0);
    }
}

/* Small item resting on top of a piece of furniture. */
static int on_top(GenCtx *c, int room, int base, int type, int tier, int flags)
{
    if (base < 0)
        return -1;
    const LvProp *b = &c->L->props[base];
    float top = b->y + nh_prop_info[b->type].half.y * 2.0f;
    float ox = (float)gen_ri(c, -2, 2) * 0.08f, oz = (float)gen_ri(c, -1, 1) * 0.06f;
    if (tier < 0) {
        int p = gen_prop(c, type, b->floor, b->x + ox, top, b->z + oz, b->yaw, LV_P_DYNAMIC | LV_P_INTERACT);
        return p;
    }
    return gen_loot(c, room, type, b->x + ox, top, b->z + oz, tier, flags);
}

static int on_wall(GenCtx *c, int room, int type, float y, int count)
{
    Slot s[256];
    int n = wall_slots(c, room, s, 256), placed = 0;
    int f = c->L->rooms[room].floor;
    for (int i = 0; i < n && placed < count; i++)
        if (gen_prop_on_wall(c, type, f, s[i].x, s[i].z, s[i].side, 0.0f, y) >= 0)
            placed++;
    return placed;
}

/* Paintings as loot hung on walls. */
static int paintings(GenCtx *c, int room, int count, int tier)
{
    Slot s[256];
    int n = wall_slots(c, room, s, 256), placed = 0;
    int f = c->L->rooms[room].floor;
    for (int i = 0; i < n && placed < count; i++) {
        int p = gen_prop_on_wall(c, PROP_PAINTING, f, s[i].x, s[i].z, s[i].side, 0.0f, 1.3f);
        if (p < 0)
            continue;
        LvProp *pp = &c->L->props[p];
        pp->flags |= LV_P_DYNAMIC | LV_P_INTERACT;
        pp->state = (uint8_t)gen_ri(c, 0, 7); /* which canvas */
        int e = gen_entity(c, ENT_LOOT, room, p, nh_v3(pp->x, pp->y, pp->z), pp->yaw);
        if (e >= 0) {
            c->P->ent[e].tier = (uint8_t)tier;
            c->P->ent[e].flags = ENTF_HEAVY;
            c->P->ent[e].value = (uint16_t)gen_ri(c, k_tier_value[tier][0], k_tier_value[tier][1]);
        }
        placed++;
    }
    return placed;
}

/* A desk against a wall with a chair and a computer; returns the desk. */
static int desk_set(GenCtx *c, int room, bool computer)
{
    Slot s[256];
    int n = wall_slots(c, room, s, 256);
    int f = c->L->rooms[room].floor;
    for (int i = 0; i < n; i++) {
        int d = gen_prop_wall(c, PROP_DESK, f, s[i].x, s[i].z, s[i].side, 0.0f, 0);
        if (d < 0)
            continue;
        const LvProp *dp = &c->L->props[d];
        float in = -1.0f;
        float cx = dp->x + (float)k_dx[s[i].side] * in * 0.75f, cz = dp->z + (float)k_dz[s[i].side] * in * 0.75f;
        gen_prop(c, PROP_OFFICE_CHAIR, f, cx, lv_floor_y(f), cz, gen_wall_yaw(s[i].side) + NH_PI, 0);
        if (computer) {
            int p = lv_add_prop(c->L, PROP_COMPUTER, f, dp->x, dp->y + 0.76f, dp->z, dp->yaw,
                                nh_prop_info[PROP_COMPUTER].half, nh_prop_info[PROP_COMPUTER].flags);
            if (p >= 0)
                c->L->props[p].room = (uint8_t)room;
        }
        return d;
    }
    return -1;
}

static int safe_in(GenCtx *c, int room, int tier)
{
    int f = c->L->rooms[room].floor;
    Slot s[256];
    int n = wall_slots(c, room, s, 256);
    for (int i = 0; i < n; i++) {
        int p = gen_prop_wall(c, PROP_SAFE, f, s[i].x, s[i].z, s[i].side, 0.0f, LV_P_INTERACT | LV_P_DYNAMIC);
        if (p < 0)
            continue;
        const LvProp *sp = &c->L->props[p];
        int e = gen_entity(c, ENT_SAFE, room, p, nh_v3(sp->x, sp->y, sp->z), sp->yaw);
        if (e >= 0) {
            c->P->ent[e].tier = (uint8_t)NH_CLAMP(1 + tier / 2, 1, 3);
            /* The contents are loot behind the safe door. */
            int l = gen_loot(c, room, tier >= 3 ? PROP_JEWELRY_BOX : PROP_CASH_STACK, sp->x, sp->y + 0.3f, sp->z, tier, 0);
            if (l >= 0) {
                c->P->ent[l].flags |= ENTF_SECURED;
                c->P->ent[l].door = (int16_t)e; /* inside this safe */
            }
        }
        return p;
    }
    return -1;
}

static void lasers_across(GenCtx *c, int room, int count)
{
    const LvRoom *r = &c->L->rooms[room];
    int f = r->floor;
    bool along_x = room_w(r) >= room_d(r);
    NhVec3 ctr = room_center(r);
    for (int i = 0; i < count; i++) {
        float t = ((float)i + 1.0f) / ((float)count + 1.0f);
        float h = 0.35f + (float)(i % 3) * 0.45f;
        NhVec3 p;
        float yaw, len;
        if (along_x) {
            /* Beams cross the room's short (Z) direction at several X positions. */
            p = nh_v3((float)r->x0 * LV_CELL + t * room_w(r), lv_floor_y(f) + h, (float)r->z0 * LV_CELL + 0.08f);
            yaw = NH_PI; /* toward +Z */
            len = room_d(r) - 0.16f;
        } else {
            p = nh_v3((float)r->x0 * LV_CELL + 0.08f, lv_floor_y(f) + h, (float)r->z0 * LV_CELL + t * room_d(r));
            yaw = -NH_PI * 0.5f; /* toward +X */
            len = room_w(r) - 0.16f;
        }
        int prop = lv_add_prop(c->L, PROP_LASER_EMITTER, f, p.x, p.y, p.z, yaw, nh_prop_info[PROP_LASER_EMITTER].half,
                               nh_prop_info[PROP_LASER_EMITTER].flags | LV_P_DYNAMIC);
        if (prop >= 0)
            c->L->props[prop].room = (uint8_t)room;
        int e = gen_entity(c, ENT_LASER, room, prop, p, yaw);
        if (e >= 0) {
            c->P->ent[e].value = (uint16_t)(len * 100.0f);
            c->P->ent[e].circuit = c->P->security_circuit;
        }
        (void)ctr;
    }
}

/* ---- default dressing by function ----------------------------------------------- */

static void dress_default(GenCtx *c, int room)
{
    LvRoom *r = &c->L->rooms[room];
    int a = gen_area(r);
    switch (r->func) {
    case RF_OFFICE: {
        int d = desk_set(c, room, true);
        if (a >= 9)
            desk_set(c, room, true);
        along_walls(c, room, PROP_FILING_CABINET, gen_ri(c, 1, 2));
        along_walls(c, room, PROP_PLANT, gen_ri(c, 0, 1));
        if (gen_chance(c, 40))
            along_walls(c, room, PROP_BOOKSHELF, 1);
        if (d >= 0 && gen_chance(c, 45))
            on_top(c, room, d, PROP_LAPTOP, 1, 0);
        if (d >= 0 && gen_chance(c, 50)) {
            int p = on_top(c, room, d, PROP_PHONE, -1, 0);
            if (p >= 0)
                gen_entity(c, ENT_PHONE, room, p, nh_v3(c->L->props[p].x, c->L->props[p].y, c->L->props[p].z), 0);
        }
        break;
    }
    case RF_OPEN_OFFICE:
        grid(c, room, PROP_DESK, 3.0f, 2.6f, 1.6f, 0.0f);
        along_walls(c, room, PROP_FILING_CABINET, gen_ri(c, 2, 4));
        along_walls(c, room, PROP_PLANT, 2);
        along_walls(c, room, PROP_WATER_COOLER, 1);
        break;
    case RF_MEETING: {
        int t = center_prop(c, room, a >= 9 ? PROP_CONFERENCE_TABLE : PROP_COFFEE_TABLE, 0, 0, 0);
        chairs_around(c, room, t, PROP_OFFICE_CHAIR);
        on_wall(c, room, PROP_TV, 1.4f, 1);
        break;
    }
    case RF_EXEC_OFFICE: {
        int d = desk_set(c, room, true);
        along_walls(c, room, PROP_BOOKSHELF, 2);
        along_walls(c, room, PROP_ARMCHAIR, 1);
        along_walls(c, room, PROP_PLANT, 1);
        paintings(c, room, 1, 2);
        if (gen_chance(c, 60))
            safe_in(c, room, 2);
        if (d >= 0)
            on_top(c, room, d, PROP_DOCUMENTS, 2, 0);
        break;
    }
    case RF_BATHROOM:
        line_walls(c, room, PROP_TOILET_STALL, 1 << gen_ri(c, 0, 3), 0.05f);
        along_walls(c, room, PROP_SINK, gen_ri(c, 1, 3));
        break;
    case RF_KITCHEN:
        line_walls(c, room, PROP_KITCHEN_COUNTER, 1 << gen_ri(c, 0, 3), 0.0f);
        along_walls(c, room, PROP_FRIDGE, 1);
        along_walls(c, room, PROP_COFFEE_MACHINE, 1);
        break;
    case RF_BREAKROOM: {
        along_walls(c, room, PROP_KITCHEN_COUNTER, 2);
        along_walls(c, room, PROP_FRIDGE, 1);
        along_walls(c, room, PROP_COFFEE_MACHINE, 1);
        along_walls(c, room, PROP_SOFA, 1);
        if (gen_rw(r) >= 3 && gen_rd(r) >= 3)
            center_prop(c, room, PROP_COFFEE_TABLE, 0, 0, 0);
        break;
    }
    case RF_STORAGE:
    case RF_LOADING:
        if (a >= 30) {
            rows(c, room, PROP_PALLET_RACK, 2.6f, 2.0f, false);
            along_walls(c, room, PROP_CRATE_STACK, 3);
            break;
        }
        along_walls(c, room, PROP_PALLET_RACK, a >= 12 ? 2 : 1);
        along_walls(c, room, PROP_CRATE_STACK, gen_ri(c, 1, 3));
        along_walls(c, room, PROP_CRATE, gen_ri(c, 1, 3));
        along_walls(c, room, PROP_BARREL, gen_ri(c, 0, 2));
        break;
    case RF_WAREHOUSE:
        rows(c, room, PROP_PALLET_RACK, 2.4f, 2.2f, false);
        break;
    case RF_SECURITY: {
        int d = desk_set(c, room, false);
        if (d >= 0) {
            const LvProp *dp = &c->L->props[d];
            int t = lv_add_prop(c->L, PROP_TERMINAL, dp->floor, dp->x, dp->y + 0.76f, dp->z, dp->yaw,
                                nh_prop_info[PROP_TERMINAL].half, nh_prop_info[PROP_TERMINAL].flags);
            if (t >= 0)
                c->L->props[t].room = (uint8_t)room;
            gen_entity(c, ENT_CONSOLE, room, t, nh_v3(dp->x, dp->y + 0.9f, dp->z), dp->yaw);
        }
        on_wall(c, room, PROP_TV, 1.6f, 3);
        along_walls(c, room, PROP_LOCKER, 2);
        break;
    }
    case RF_SERVER:
        rows(c, room, PROP_SERVER_RACK, 1.3f, 1.0f, true);
        break;
    case RF_BANK_HALL:
    case RF_TELLER:
        line_walls(c, room, PROP_BANK_COUNTER, 1 << gen_ri(c, 0, 3), 0.0f);
        along_walls(c, room, PROP_PLANT, 2);
        along_walls(c, room, PROP_BENCH, 2);
        break;
    case RF_LIVING:
    case RF_LOUNGE:
    case RF_PENTHOUSE: {
        along_walls(c, room, PROP_SOFA, 1);
        center_prop(c, room, PROP_COFFEE_TABLE, 0, 0, 0);
        along_walls(c, room, PROP_ARMCHAIR, 2);
        on_wall(c, room, PROP_TV, 1.3f, 1);
        along_walls(c, room, PROP_LAMP_FLOOR, 1);
        paintings(c, room, gen_ri(c, 0, 2), 1);
        break;
    }
    case RF_DINING: {
        int t = center_prop(c, room, PROP_CONFERENCE_TABLE, 0, 0, 0);
        chairs_around(c, room, t, PROP_OFFICE_CHAIR);
        paintings(c, room, 1, 2);
        along_walls(c, room, PROP_WINE_RACK, 1);
        break;
    }
    case RF_BEDROOM: {
        int b = along_walls(c, room, PROP_BED, 1);
        (void)b;
        along_walls(c, room, PROP_WARDROBE, 1);
        along_walls(c, room, PROP_LAMP_FLOOR, 1);
        if (gen_chance(c, 40)) {
            int d = desk_set(c, room, false);
            if (d >= 0)
                on_top(c, room, d, PROP_JEWELRY_BOX, 1, 0);
        }
        break;
    }
    case RF_LIBRARY:
        line_walls(c, room, PROP_BOOKSHELF, 0xF, 0.05f);
        center_prop(c, room, PROP_COFFEE_TABLE, 0, 0, 0);
        along_walls(c, room, PROP_ARMCHAIR, 1);
        break;
    case RF_STUDY: {
        int d = desk_set(c, room, false);
        along_walls(c, room, PROP_BOOKSHELF, 2);
        if (d >= 0)
            on_top(c, room, d, PROP_DOCUMENTS, 2, 0);
        if (gen_chance(c, 50))
            safe_in(c, room, 2);
        break;
    }
    case RF_GALLERY:
        paintings(c, room, gen_ri(c, 2, 5), 2);
        center_prop(c, room, PROP_BENCH, 0, 0, 0);
        if (a >= 12)
            grid(c, room, PROP_PEDESTAL, 3.0f, 3.0f, 2.0f, 0.0f);
        break;
    case RF_WINE_CELLAR:
        line_walls(c, room, PROP_WINE_RACK, 0xF, 0.05f);
        along_walls(c, room, PROP_BARREL, 2);
        break;
    case RF_GARAGE:
        center_prop(c, room, PROP_CAR, 0, 0, NH_PI * 0.5f);
        along_walls(c, room, PROP_PALLET_RACK, 1);
        along_walls(c, room, PROP_BARREL, 1);
        break;
    case RF_LOCKER_ROOM:
        line_walls(c, room, PROP_LOCKER, 1 << gen_ri(c, 0, 3), 0.02f);
        center_prop(c, room, PROP_BENCH, 0, 0, 0);
        break;
    case RF_PANIC_ROOM:
        safe_in(c, room, 3);
        along_walls(c, room, PROP_LOCKER, 2);
        along_walls(c, room, PROP_CRATE, 2);
        on_wall(c, room, PROP_TV, 1.5f, 2);
        break;
    case RF_UTILITY:
        along_walls(c, room, PROP_PALLET_RACK, 1);
        along_walls(c, room, PROP_BARREL, gen_ri(c, 1, 2));
        break;
    case RF_ARCHIVE:
        rows(c, room, PROP_FILING_CABINET, 1.2f, 1.0f, true);
        break;
    case RF_SHOP:
        rows(c, room, PROP_BOOKSHELF, 1.4f, 1.2f, true);
        along_walls(c, room, PROP_RECEPTION_DESK, 1);
        break;
    case RF_LAB: {
        desk_set(c, room, false);
        desk_set(c, room, true);
        along_walls(c, room, PROP_DISPLAY_CASE, 1);
        paintings(c, room, 1, 2);
        break;
    }
    case RF_EXHIBIT:
        grid(c, room, PROP_DISPLAY_CASE, 3.0f, 3.0f, 1.8f, 0.0f);
        break;
    case RF_LOBBY:
        along_walls(c, room, PROP_RECEPTION_DESK, 1);
        along_walls(c, room, PROP_SOFA, 1);
        along_walls(c, room, PROP_PLANT, 2);
        along_walls(c, room, PROP_WATER_COOLER, 1);
        break;
    case RF_CORRIDOR:
        if (gen_chance(c, 35))
            along_walls(c, room, PROP_PLANT, 1);
        if (gen_chance(c, 20))
            along_walls(c, room, PROP_WATER_COOLER, 1);
        if (c->type == BT_MANSION || c->type == BT_MUSEUM)
            paintings(c, room, gen_ri(c, 0, 2), 1);
        break;
    case RF_VAULT:
        along_walls(c, room, PROP_PALLET_RACK, 2);
        break;
    default:
        break;
    }
}

/* ---- set pieces ------------------------------------------------------------------- */

typedef struct SetPiece {
    const char *name;
    uint8_t funcs[3];
    uint8_t types;    /* bit per building type, 0 = any */
    uint8_t min_w, min_d; /* cells (either orientation) */
    uint8_t repeat;   /* may appear more than once per building */
    void (*dress)(GenCtx *c, int room);
} SetPiece;

static void sp_gold_vault(GenCtx *c, int room)
{
    line_walls(c, room, PROP_PALLET_RACK, 0xF, 0.1f);
    int t = center_prop(c, room, PROP_CONFERENCE_TABLE, 0, 0, 0);
    on_top(c, room, t, PROP_GOLD_BARS, 4, ENTF_OBJECTIVE);
    on_top(c, room, t, PROP_GOLD_BARS, 3, 0);
    on_top(c, room, t, PROP_CASH_STACK, 3, 0);
    lasers_across(c, room, 3);
}
static void sp_deposit_vault(GenCtx *c, int room)
{
    line_walls(c, room, PROP_FILING_CABINET, 0xF, 0.0f);
    int t = center_prop(c, room, PROP_COFFEE_TABLE, 0, 0, 0);
    on_top(c, room, t, PROP_JEWELRY_BOX, 4, ENTF_OBJECTIVE);
    on_top(c, room, t, PROP_WATCH_CASE, 3, 0);
    lasers_across(c, room, 2);
}
static void sp_cash_cage(GenCtx *c, int room)
{
    int n = grid(c, room, PROP_CRATE, 2.2f, 2.2f, 1.5f, 0.0f);
    (void)n;
    const LvRoom *r = &c->L->rooms[room];
    NhVec3 ctr = room_center(r);
    gen_loot(c, room, PROP_CASH_STACK, ctr.x + 0.6f, ctr.y + 1.0f, ctr.z, 4, ENTF_OBJECTIVE);
    along_walls(c, room, PROP_CASH_STACK, 0);
    lasers_across(c, room, 4);
}
static void sp_server_farm(GenCtx *c, int room)
{
    rows(c, room, PROP_SERVER_RACK, 1.4f, 1.0f, true);
    int d = desk_set(c, room, false);
    if (d >= 0) {
        const LvProp *dp = &c->L->props[d];
        int t = lv_add_prop(c->L, PROP_TERMINAL, dp->floor, dp->x, dp->y + 0.76f, dp->z, dp->yaw,
                            nh_prop_info[PROP_TERMINAL].half, nh_prop_info[PROP_TERMINAL].flags);
        if (t >= 0)
            c->L->props[t].room = (uint8_t)room;
        int e = gen_entity(c, ENT_TERMINAL, room, t, nh_v3(dp->x, dp->y + 0.9f, dp->z), dp->yaw);
        if (e >= 0)
            c->P->ent[e].tier = 2;
        on_top(c, room, d, PROP_HARD_DRIVE, 4, ENTF_OBJECTIVE);
    }
}
static void sp_data_core(GenCtx *c, int room)
{
    line_walls(c, room, PROP_SERVER_RACK, 0xF, 0.02f);
    int t = center_prop(c, room, PROP_DESK, 0, 0, 0);
    if (t >= 0) {
        const LvProp *dp = &c->L->props[t];
        int term = lv_add_prop(c->L, PROP_TERMINAL, dp->floor, dp->x, dp->y + 0.76f, dp->z, dp->yaw,
                               nh_prop_info[PROP_TERMINAL].half, nh_prop_info[PROP_TERMINAL].flags);
        int e = gen_entity(c, ENT_TERMINAL, room, term, nh_v3(dp->x, dp->y + 0.9f, dp->z), dp->yaw);
        if (e >= 0)
            c->P->ent[e].tier = 3;
        on_top(c, room, t, PROP_HARD_DRIVE, 4, ENTF_OBJECTIVE);
    }
}
static void sp_corner_office(GenCtx *c, int room)
{
    int d = desk_set(c, room, true);
    line_walls(c, room, PROP_BOOKSHELF, 1 << gen_ri(c, 0, 3), 0.05f);
    along_walls(c, room, PROP_ARMCHAIR, 2);
    along_walls(c, room, PROP_PLANT, 2);
    paintings(c, room, 2, 3);
    safe_in(c, room, 3);
    if (d >= 0)
        on_top(c, room, d, PROP_LAPTOP, 2, 0);
}
static void sp_boardroom(GenCtx *c, int room)
{
    int t1 = center_prop(c, room, PROP_CONFERENCE_TABLE, -1.25f, 0, 0);
    int t2 = center_prop(c, room, PROP_CONFERENCE_TABLE, 1.25f, 0, 0);
    chairs_around(c, room, t1, PROP_OFFICE_CHAIR);
    chairs_around(c, room, t2, PROP_OFFICE_CHAIR);
    on_wall(c, room, PROP_TV, 1.4f, 2);
    on_top(c, room, t1, PROP_DOCUMENTS, 3, 0);
    along_walls(c, room, PROP_PLANT, 2);
}
static void sp_security_hub(GenCtx *c, int room)
{
    for (int k = 0; k < 2; k++) {
        int d = desk_set(c, room, false);
        if (d < 0)
            continue;
        const LvProp *dp = &c->L->props[d];
        int t = lv_add_prop(c->L, PROP_TERMINAL, dp->floor, dp->x, dp->y + 0.76f, dp->z, dp->yaw,
                            nh_prop_info[PROP_TERMINAL].half, nh_prop_info[PROP_TERMINAL].flags);
        if (t >= 0)
            c->L->props[t].room = (uint8_t)room;
        gen_entity(c, ENT_CONSOLE, room, t, nh_v3(dp->x, dp->y + 0.9f, dp->z), dp->yaw);
    }
    on_wall(c, room, PROP_TV, 1.8f, 4);
    on_wall(c, room, PROP_TV, 1.1f, 2);
    along_walls(c, room, PROP_LOCKER, 3);
}
static void sp_trading_floor(GenCtx *c, int room)
{
    rows(c, room, PROP_DESK, 1.6f, 1.4f, true);
    on_wall(c, room, PROP_TV, 2.0f, 4);
    along_walls(c, room, PROP_WATER_COOLER, 1);
}
static void sp_cubicles(GenCtx *c, int room)
{
    grid(c, room, PROP_DESK, 2.8f, 2.4f, 1.5f, 0.0f);
    along_walls(c, room, PROP_FILING_CABINET, 4);
    along_walls(c, room, PROP_PLANT, 2);
}
static void sp_records(GenCtx *c, int room)
{
    rows(c, room, PROP_BOOKSHELF, 1.2f, 1.0f, false);
    along_walls(c, room, PROP_FILING_CABINET, 4);
    const LvRoom *r = &c->L->rooms[room];
    NhVec3 ctr = room_center(r);
    gen_loot(c, room, PROP_DOCUMENTS, ctr.x, ctr.y + 0.01f, ctr.z, 3, 0);
}
static void sp_locker_room(GenCtx *c, int room)
{
    line_walls(c, room, PROP_LOCKER, 0x5, 0.02f);
    line_walls(c, room, PROP_LOCKER, 0xA, 0.02f);
    center_prop(c, room, PROP_BENCH, 0, 0, 0);
}
static void sp_break_room(GenCtx *c, int room)
{
    line_walls(c, room, PROP_KITCHEN_COUNTER, 1 << gen_ri(c, 0, 3), 0.0f);
    along_walls(c, room, PROP_FRIDGE, 1);
    along_walls(c, room, PROP_COFFEE_MACHINE, 1);
    int t = center_prop(c, room, PROP_CONFERENCE_TABLE, 0, 0, 0);
    chairs_around(c, room, t, PROP_OFFICE_CHAIR);
    along_walls(c, room, PROP_SOFA, 1);
    on_wall(c, room, PROP_TV, 1.6f, 1);
}
static void sp_banking_hall(GenCtx *c, int room)
{
    line_walls(c, room, PROP_BANK_COUNTER, 1 << gen_ri(c, 0, 3), 0.0f);
    grid(c, room, PROP_BENCH, 4.0f, 3.0f, 2.5f, 0.0f);
    along_walls(c, room, PROP_PLANT, 3);
    on_wall(c, room, PROP_PAINTING, 1.5f, 1);
}
static void sp_teller_line(GenCtx *c, int room)
{
    line_walls(c, room, PROP_BANK_COUNTER, 1 << gen_ri(c, 0, 3), 0.0f);
    for (int k = 0; k < 3; k++) {
        int d = desk_set(c, room, true);
        if (d >= 0 && k == 0)
            on_top(c, room, d, PROP_CASH_STACK, 2, 0);
    }
}
static void sp_manager(GenCtx *c, int room)
{
    int d = desk_set(c, room, true);
    along_walls(c, room, PROP_FILING_CABINET, 2);
    along_walls(c, room, PROP_ARMCHAIR, 2);
    safe_in(c, room, 3);
    if (d >= 0)
        on_top(c, room, d, PROP_DOCUMENTS, 2, 0);
}
static void sp_master_bedroom(GenCtx *c, int room)
{
    along_walls(c, room, PROP_BED, 1);
    along_walls(c, room, PROP_LAMP_FLOOR, 2);
    along_walls(c, room, PROP_WARDROBE, 2);
    int d = desk_set(c, room, false);
    if (d >= 0)
        on_top(c, room, d, PROP_JEWELRY_BOX, 4, ENTF_OBJECTIVE);
    safe_in(c, room, 3);
    paintings(c, room, 1, 3);
}
static void sp_guest_bedroom(GenCtx *c, int room)
{
    along_walls(c, room, PROP_BED, 1);
    along_walls(c, room, PROP_WARDROBE, 1);
    along_walls(c, room, PROP_LAMP_FLOOR, 1);
    along_walls(c, room, PROP_ARMCHAIR, 1);
}
static void sp_grand_library(GenCtx *c, int room)
{
    line_walls(c, room, PROP_BOOKSHELF, 0xF, 0.02f);
    int t = center_prop(c, room, PROP_CONFERENCE_TABLE, 0, 0, 0);
    chairs_around(c, room, t, PROP_OFFICE_CHAIR);
    on_top(c, room, t, PROP_DOCUMENTS, 3, 0);
}
static void sp_study(GenCtx *c, int room)
{
    int d = desk_set(c, room, false);
    line_walls(c, room, PROP_BOOKSHELF, 1 << gen_ri(c, 0, 3), 0.05f);
    center_prop(c, room, PROP_STATUE, 0, 0, 0);
    safe_in(c, room, 3);
    if (d >= 0)
        on_top(c, room, d, PROP_DOCUMENTS, 3, 0);
}
static void sp_wine_cellar(GenCtx *c, int room)
{
    line_walls(c, room, PROP_WINE_RACK, 0xF, 0.02f);
    rows(c, room, PROP_BARREL, 1.6f, 1.6f, false);
    along_walls(c, room, PROP_WATCH_CASE, 0);
    const LvRoom *r = &c->L->rooms[room];
    NhVec3 ctr = room_center(r);
    gen_loot(c, room, PROP_WATCH_CASE, ctr.x + 0.4f, ctr.y, ctr.z + 0.4f, 3, 0);
}
static void sp_dining(GenCtx *c, int room)
{
    int t1 = center_prop(c, room, PROP_CONFERENCE_TABLE, -1.25f, 0, 0);
    int t2 = center_prop(c, room, PROP_CONFERENCE_TABLE, 1.25f, 0, 0);
    chairs_around(c, room, t1, PROP_OFFICE_CHAIR);
    chairs_around(c, room, t2, PROP_OFFICE_CHAIR);
    paintings(c, room, 2, 2);
    along_walls(c, room, PROP_WINE_RACK, 1);
}
static void sp_living(GenCtx *c, int room)
{
    along_walls(c, room, PROP_SOFA, 2);
    center_prop(c, room, PROP_COFFEE_TABLE, 0, 0, 0);
    along_walls(c, room, PROP_PIANO, 1);
    along_walls(c, room, PROP_LAMP_FLOOR, 2);
    on_wall(c, room, PROP_TV, 1.3f, 1);
    paintings(c, room, 2, 2);
}
static void sp_collector_gallery(GenCtx *c, int room)
{
    paintings(c, room, 6, 3);
    grid(c, room, PROP_STATUE, 3.0f, 3.0f, 2.2f, 0.0f);
}
static void sp_panic_room(GenCtx *c, int room)
{
    safe_in(c, room, 4);
    line_walls(c, room, PROP_LOCKER, 1 << gen_ri(c, 0, 3), 0.02f);
    on_wall(c, room, PROP_TV, 1.6f, 3);
    along_walls(c, room, PROP_CRATE, 2);
}
static void sp_garage(GenCtx *c, int room)
{
    center_prop(c, room, PROP_CAR, 0, 0, NH_PI * 0.5f);
    line_walls(c, room, PROP_PALLET_RACK, 1 << gen_ri(c, 0, 3), 0.1f);
    along_walls(c, room, PROP_BARREL, 2);
}
static void sp_kitchen(GenCtx *c, int room)
{
    line_walls(c, room, PROP_KITCHEN_COUNTER, 0x5, 0.0f);
    center_prop(c, room, PROP_KITCHEN_COUNTER, 0, 0, 0);
    along_walls(c, room, PROP_FRIDGE, 2);
    along_walls(c, room, PROP_COFFEE_MACHINE, 1);
}
static void sp_sculpture_hall(GenCtx *c, int room)
{
    grid(c, room, PROP_PEDESTAL, 3.2f, 3.2f, 1.8f, 0.0f);
    const LvRoom *r = &c->L->rooms[room];
    NhVec3 ctr = room_center(r);
    gen_loot(c, room, PROP_STATUE, ctr.x, ctr.y, ctr.z, 3, 0);
    along_walls(c, room, PROP_BENCH, 2);
}
static void sp_painting_gallery(GenCtx *c, int room)
{
    paintings(c, room, 8, 2);
    grid(c, room, PROP_BENCH, 4.0f, 4.0f, 2.4f, 0.0f);
}
static void sp_jewel_exhibit(GenCtx *c, int room)
{
    int n0 = c->L->nprops;
    grid(c, room, PROP_DISPLAY_CASE, 2.8f, 2.8f, 1.8f, 0.0f);
    int best = -1;
    for (int i = n0; i < c->L->nprops; i++) {
        if (c->L->props[i].type != PROP_DISPLAY_CASE)
            continue;
        int e = on_top(c, room, i, PROP_JEWELRY_BOX, best < 0 ? 4 : 3, best < 0 ? ENTF_OBJECTIVE | ENTF_SECURED : ENTF_SECURED);
        if (best < 0 && e >= 0)
            best = e;
    }
    lasers_across(c, room, 3);
}
static void sp_the_diamond(GenCtx *c, int room)
{
    int p = center_prop(c, room, PROP_PEDESTAL, 0, 0, 0);
    if (p >= 0) {
        const LvProp *pp = &c->L->props[p];
        int dc = gen_prop(c, PROP_DISPLAY_CASE, pp->floor, pp->x, pp->y + nh_prop_info[PROP_PEDESTAL].half.y * 2.0f,
                          pp->z, 0, 0);
        on_top(c, room, dc >= 0 ? dc : p, PROP_JEWELRY_BOX, 4, ENTF_OBJECTIVE | ENTF_SECURED);
    }
    lasers_across(c, room, 5);
    along_walls(c, room, PROP_BENCH, 2);
}
static void sp_antiquities(GenCtx *c, int room)
{
    int n0 = c->L->nprops;
    along_walls(c, room, PROP_DISPLAY_CASE, 6);
    for (int i = n0; i < c->L->nprops; i++)
        if (c->L->props[i].type == PROP_DISPLAY_CASE)
            on_top(c, room, i, gen_chance(c, 50) ? PROP_WATCH_CASE : PROP_JEWELRY_BOX, 2, ENTF_SECURED);
    grid(c, room, PROP_STATUE, 3.5f, 3.5f, 2.4f, 0.0f);
}
static void sp_restoration(GenCtx *c, int room)
{
    desk_set(c, room, true);
    desk_set(c, room, false);
    grid(c, room, PROP_PEDESTAL, 2.6f, 2.6f, 1.8f, 0.0f);
    paintings(c, room, 2, 3);
    along_walls(c, room, PROP_CRATE, 2);
}
static void sp_art_storage(GenCtx *c, int room)
{
    rows(c, room, PROP_PALLET_RACK, 1.8f, 1.2f, false);
    along_walls(c, room, PROP_CRATE, 3);
    const LvRoom *r = &c->L->rooms[room];
    NhVec3 ctr = room_center(r);
    gen_loot(c, room, PROP_PAINTING, ctr.x, ctr.y + 0.5f, ctr.z, 3, ENTF_HEAVY);
}
static void sp_gift_shop(GenCtx *c, int room)
{
    rows(c, room, PROP_BOOKSHELF, 1.4f, 1.3f, true);
    along_walls(c, room, PROP_RECEPTION_DESK, 1);
    along_walls(c, room, PROP_DISPLAY_CASE, 2);
}
static void sp_curator(GenCtx *c, int room)
{
    int d = desk_set(c, room, true);
    along_walls(c, room, PROP_BOOKSHELF, 2);
    along_walls(c, room, PROP_DISPLAY_CASE, 1);
    if (d >= 0)
        on_top(c, room, d, PROP_DOCUMENTS, 2, 0);
}
static void sp_racking(GenCtx *c, int room)
{
    rows(c, room, PROP_PALLET_RACK, 2.6f, 2.0f, false);
    along_walls(c, room, PROP_CRATE_STACK, 4);
    along_walls(c, room, PROP_BARREL, 3);
}
static void sp_bonded_cage(GenCtx *c, int room)
{
    line_walls(c, room, PROP_PALLET_RACK, 1 << gen_ri(c, 0, 3), 0.1f);
    grid(c, room, PROP_CRATE_STACK, 2.4f, 2.4f, 1.6f, 0.0f);
    const LvRoom *r = &c->L->rooms[room];
    NhVec3 ctr = room_center(r);
    gen_loot(c, room, PROP_CRATE, ctr.x, ctr.y, ctr.z, 4, ENTF_OBJECTIVE | ENTF_HEAVY);
}
static void sp_loading_bay(GenCtx *c, int room)
{
    grid(c, room, PROP_CRATE, 2.5f, 2.2f, 1.6f, 0.0f);
    along_walls(c, room, PROP_BARREL, 3);
    along_walls(c, room, PROP_PALLET_RACK, 1);
}
static void sp_foreman(GenCtx *c, int room)
{
    int d = desk_set(c, room, true);
    along_walls(c, room, PROP_FILING_CABINET, 3);
    safe_in(c, room, 2);
    if (d >= 0)
        on_top(c, room, d, PROP_DOCUMENTS, 1, 0);
}
static void sp_utility_plant(GenCtx *c, int room)
{
    along_walls(c, room, PROP_PALLET_RACK, 1);
    along_walls(c, room, PROP_BARREL, 2);
}
static void sp_reception(GenCtx *c, int room)
{
    center_prop(c, room, PROP_RECEPTION_DESK, 0, 0, 0);
    along_walls(c, room, PROP_SOFA, 2);
    along_walls(c, room, PROP_PLANT, 3);
    along_walls(c, room, PROP_WATER_COOLER, 1);
    on_wall(c, room, PROP_PAINTING, 1.5f, 1);
}
static void sp_penthouse(GenCtx *c, int room)
{
    along_walls(c, room, PROP_PIANO, 1);
    grid(c, room, PROP_SOFA, 3.4f, 3.0f, 2.0f, 0.0f);
    along_walls(c, room, PROP_ARMCHAIR, 2);
    on_wall(c, room, PROP_TV, 1.4f, 1);
    paintings(c, room, 2, 3);
}
static void sp_mail_room(GenCtx *c, int room)
{
    line_walls(c, room, PROP_BOOKSHELF, 1 << gen_ri(c, 0, 3), 0.05f);
    center_prop(c, room, PROP_DESK, 0, 0, 0);
    along_walls(c, room, PROP_CRATE, 3);
}

#define T(x) (1u << (x))
static const SetPiece k_setpieces[] = {
    { "Gold vault", { RF_VAULT }, T(BT_BANK), 3, 3, 0, sp_gold_vault },
    { "Deposit box vault", { RF_VAULT }, T(BT_BANK), 2, 3, 0, sp_deposit_vault },
    { "Cash cage", { RF_VAULT, RF_STORAGE }, T(BT_BANK) | T(BT_WAREHOUSE), 3, 3, 0, sp_cash_cage },
    { "Server farm", { RF_SERVER }, 0, 3, 3, 0, sp_server_farm },
    { "Data core", { RF_SERVER }, 0, 3, 3, 0, sp_data_core },
    { "Corner office", { RF_EXEC_OFFICE }, 0, 3, 3, 0, sp_corner_office },
    { "Boardroom", { RF_MEETING }, T(BT_OFFICE) | T(BT_BANK), 4, 3, 0, sp_boardroom },
    { "Security hub", { RF_SECURITY }, 0, 2, 3, 0, sp_security_hub },
    { "Trading floor", { RF_OPEN_OFFICE }, T(BT_OFFICE) | T(BT_BANK), 4, 4, 0, sp_trading_floor },
    { "Cubicle farm", { RF_OPEN_OFFICE }, T(BT_OFFICE), 4, 4, 1, sp_cubicles },
    { "Records archive", { RF_ARCHIVE }, 0, 3, 3, 0, sp_records },
    { "Staff locker room", { RF_LOCKER_ROOM }, 0, 2, 3, 0, sp_locker_room },
    { "Break room", { RF_BREAKROOM }, 0, 3, 3, 0, sp_break_room },
    { "Banking hall", { RF_BANK_HALL }, T(BT_BANK), 4, 3, 0, sp_banking_hall },
    { "Teller line", { RF_TELLER }, T(BT_BANK), 3, 2, 0, sp_teller_line },
    { "Manager's office", { RF_EXEC_OFFICE }, T(BT_BANK) | T(BT_WAREHOUSE), 2, 3, 0, sp_manager },
    { "Master bedroom", { RF_BEDROOM }, T(BT_MANSION), 3, 4, 0, sp_master_bedroom },
    { "Guest bedroom", { RF_BEDROOM }, T(BT_MANSION), 3, 3, 1, sp_guest_bedroom },
    { "Grand library", { RF_LIBRARY }, T(BT_MANSION), 3, 4, 0, sp_grand_library },
    { "Gentleman's study", { RF_STUDY }, T(BT_MANSION), 3, 3, 0, sp_study },
    { "Wine cellar", { RF_WINE_CELLAR }, T(BT_MANSION), 2, 3, 0, sp_wine_cellar },
    { "Formal dining", { RF_DINING }, T(BT_MANSION), 4, 3, 0, sp_dining },
    { "Drawing room", { RF_LIVING }, T(BT_MANSION), 3, 4, 0, sp_living },
    { "Collector's gallery", { RF_GALLERY }, T(BT_MANSION), 3, 3, 0, sp_collector_gallery },
    { "Panic room", { RF_PANIC_ROOM }, 0, 2, 2, 0, sp_panic_room },
    { "Garage", { RF_GARAGE }, 0, 3, 3, 0, sp_garage },
    { "Country kitchen", { RF_KITCHEN }, T(BT_MANSION), 3, 3, 0, sp_kitchen },
    { "Sculpture hall", { RF_GALLERY, RF_EXHIBIT }, T(BT_MUSEUM), 4, 4, 0, sp_sculpture_hall },
    { "Painting gallery", { RF_GALLERY }, T(BT_MUSEUM), 3, 3, 1, sp_painting_gallery },
    { "Jewel exhibit", { RF_EXHIBIT }, T(BT_MUSEUM), 4, 3, 0, sp_jewel_exhibit },
    { "The diamond", { RF_EXHIBIT }, T(BT_MUSEUM), 3, 3, 0, sp_the_diamond },
    { "Antiquities hall", { RF_EXHIBIT, RF_GALLERY }, T(BT_MUSEUM), 4, 4, 0, sp_antiquities },
    { "Restoration lab", { RF_LAB }, T(BT_MUSEUM), 3, 3, 0, sp_restoration },
    { "Art storage", { RF_STORAGE }, T(BT_MUSEUM), 3, 3, 0, sp_art_storage },
    { "Gift shop", { RF_SHOP }, T(BT_MUSEUM), 3, 3, 0, sp_gift_shop },
    { "Curator's office", { RF_OFFICE }, T(BT_MUSEUM), 2, 3, 0, sp_curator },
    { "High racking", { RF_WAREHOUSE }, T(BT_WAREHOUSE), 5, 5, 0, sp_racking },
    { "Bonded cage", { RF_STORAGE }, T(BT_WAREHOUSE), 2, 3, 0, sp_bonded_cage },
    { "Loading bay", { RF_LOADING }, T(BT_WAREHOUSE), 4, 3, 0, sp_loading_bay },
    { "Foreman's office", { RF_OFFICE, RF_EXEC_OFFICE }, T(BT_WAREHOUSE), 2, 3, 0, sp_foreman },
    { "Utility plant", { RF_UTILITY }, 0, 2, 2, 0, sp_utility_plant },
    { "Reception lobby", { RF_LOBBY }, T(BT_OFFICE) | T(BT_MUSEUM), 3, 3, 0, sp_reception },
    { "Penthouse lounge", { RF_LOUNGE, RF_PENTHOUSE }, T(BT_MANSION) | T(BT_OFFICE), 4, 4, 0, sp_penthouse },
    { "Mail room", { RF_STORAGE }, T(BT_OFFICE), 2, 3, 0, sp_mail_room },
};
#undef T

int nh_gen_setpiece_count(void) { return (int)NH_ARRAY_LEN(k_setpieces); }
const char *nh_gen_setpiece_name(int i)
{
    return i >= 0 && i < (int)NH_ARRAY_LEN(k_setpieces) ? k_setpieces[i].name : "";
}

static bool sp_fits(const GenCtx *c, const SetPiece *sp, int room)
{
    const LvRoom *r = &c->L->rooms[room];
    if (sp->types && !(sp->types & (1u << c->type)))
        return false;
    bool f = false;
    for (int k = 0; k < 3; k++)
        f |= sp->funcs[k] && sp->funcs[k] == r->func;
    if (!f)
        return false;
    int w = gen_rw(r), d = gen_rd(r);
    return (w >= sp->min_w && d >= sp->min_d) || (w >= sp->min_d && d >= sp->min_w);
}

/* Keep doorways, stairs, grates and the hatch clear of furniture. */
static void keep_clear(GenCtx *c)
{
    Level *L = c->L;
    for (int f = 0; f < c->floors; f++)
        for (int z = 0; z < L->d; z++)
            for (int x = 0; x < L->w; x++) {
                const LvCell *cell = &L->cells[f][z][x];
                NhVec3 ctr = lv_cell_center(f, x, z);
                if (cell->stair || (cell->flags & (LV_C_HOLE | LV_C_GRATE | LV_C_HATCH)) ||
                    (cell->room >= LV_ROOM_FIRST && c->gr[cell->room].role == GR_STAIR))
                    gen_occ_mark(c, f, ctr.x, ctr.z, 1.0f, 1.0f, 2);
                for (int s = 0; s < 4; s++) {
                    const LvEdge *e = lv_edge_side_c(L, f, x, z, s);
                    if (!e || (e->type != LV_E_DOOR && e->type != LV_E_ARCH))
                        continue;
                    /* 1.2 m wide, 1.25 m deep in front of the opening. */
                    float px = ctr.x + (float)k_dx[s] * 0.375f, pz = ctr.z + (float)k_dz[s] * 0.375f;
                    float hx = k_dx[s] ? 0.625f : 0.6f, hz = k_dz[s] ? 0.625f : 0.6f;
                    gen_occ_mark(c, f, px, pz, hx, hz, 2);
                }
            }
}

void gen_choose_setpieces(GenCtx *c)
{
    keep_clear(c);
    Level *L = c->L;
    NhPlan *P = c->P;
    int nsp = (int)NH_ARRAY_LEN(k_setpieces);
    /* Visit rooms in a shuffled order so set pieces spread around. */
    int order[LV_ROOMS_MAX], n = 0;
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++)
        if (L->rooms[r].floor < c->floors)
            order[n++] = r;
    for (int i = n - 1; i > 0; i--) {
        int j = gen_ri(c, 0, i);
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    /* Secure rooms first: they hold the objective. */
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < n; k++) {
            int r = order[k];
            bool secure = c->gr[r].secure >= 2;
            if ((pass == 0) != secure || c->gr[r].setpiece)
                continue;
            int cand[64], nc = 0, fresh = 0;
            for (int i = 0; i < nsp && nc < 64; i++) {
                const SetPiece *sp = &k_setpieces[i];
                if (!sp_fits(c, sp, r))
                    continue;
                if (!sp->repeat && (P->setpieces >> i) & 1u)
                    continue;
                cand[nc++] = i;
                if (!((c->gp->avoid_setpieces >> i) & 1u))
                    fresh++;
            }
            if (!nc || (!secure && !gen_chance(c, 70)))
                continue;
            /* Everything that fits was seen recently: ordinary rooms get
             * ordinary furniture instead of a repeat. */
            if (!fresh && !secure && c->gp->avoid_setpieces)
                continue;
            /* Prefer pieces not seen recently. */
            int pick = -1;
            if (fresh) {
                int want = gen_ri(c, 0, fresh - 1);
                for (int i = 0; i < nc; i++)
                    if (!((c->gp->avoid_setpieces >> cand[i]) & 1u) && want-- == 0)
                        pick = cand[i];
            } else {
                pick = cand[gen_ri(c, 0, nc - 1)];
            }
            c->gr[r].setpiece = (uint8_t)(pick + 1);
            L->rooms[r].setpiece = (uint8_t)(pick + 1);
            if (!((P->setpieces >> pick) & 1u))
                P->nsetpieces++;
            P->setpieces |= 1ull << pick;
        }
}

/* Same blocking test as the nav grid (nh_nav.c) with the guards' radius. */
static bool node_blocked_by(const LvProp *p, float x, float z)
{
    const float r = 0.3f;
    float s = sinf(p->yaw), co = cosf(p->yaw);
    float dx = x - p->x, dz = z - p->z;
    float lx = co * dx - s * dz, lz = s * dx + co * dz;
    return fabsf(lx) < p->half.x + r && fabsf(lz) < p->half.z + r;
}

/* After dressing: on the 1 m nav nodes of the room, every doorway, arch and
 * vent grate must still connect through nodes clear of furniture. */
static bool room_walkable(GenCtx *c, int room, int first_prop)
{
    const Level *L = c->L;
    const LvRoom *r = &L->rooms[room];
    int f = r->floor;
    int nw = gen_rw(r) * 2, nd = gen_rd(r) * 2;
    static uint8_t blocked[LV_D_MAX * 2][LV_W_MAX * 2], seen[LV_D_MAX * 2][LV_W_MAX * 2], target[LV_D_MAX * 2][LV_W_MAX * 2];
    static int16_t qx[LV_D_MAX * LV_W_MAX * 4], qz[LV_D_MAX * LV_W_MAX * 4];
    (void)first_prop;
    for (int z = 0; z < nd; z++)
        for (int x = 0; x < nw; x++) {
            int cx = r->x0 + x / 2, cz = r->z0 + z / 2;
            const LvCell *cell = &L->cells[f][cz][cx];
            blocked[z][x] = cell->room != room || cell->stair || (cell->flags & LV_C_HOLE);
            seen[z][x] = 0;
            target[z][x] = 0;
        }
    for (int i = 0; i < L->nprops; i++) {
        const LvProp *p = &L->props[i];
        if (p->floor != f || !(p->flags & LV_P_SOLID) || p->y > lv_floor_y(f) + 1.5f)
            continue;
        float reach = fmaxf(p->half.x, p->half.z) + 0.8f;
        if (p->x < (float)r->x0 * LV_CELL - reach || p->x > (float)(r->x1 + 1) * LV_CELL + reach ||
            p->z < (float)r->z0 * LV_CELL - reach || p->z > (float)(r->z1 + 1) * LV_CELL + reach)
            continue;
        for (int z = 0; z < nd; z++)
            for (int x = 0; x < nw; x++)
                if (!blocked[z][x] &&
                    node_blocked_by(p, (float)r->x0 * LV_CELL + (float)x + 0.5f, (float)r->z0 * LV_CELL + (float)z + 0.5f))
                    blocked[z][x] = 1;
    }
    /* Targets: the nodes along each doorway/arch edge (any one of the pair
     * will do, so each opening is a group), and vent grate cells. */
    int ngroups = 0, gx[64][2], gz[64][2], gn[64];
    for (int cz = r->z0; cz <= r->z1; cz++)
        for (int cx = r->x0; cx <= r->x1; cx++) {
            if (L->cells[f][cz][cx].room != room)
                continue;
            for (int sd = 0; sd < 4; sd++) {
                const LvEdge *e = lv_edge_side_c(L, f, cx, cz, sd);
                if (!e || (e->type != LV_E_DOOR && e->type != LV_E_ARCH) || ngroups >= 64)
                    continue;
                int lx = (cx - r->x0) * 2, lz = (cz - r->z0) * 2;
                int n = 0;
                for (int k = 0; k < 2; k++) {
                    int x = lx + (sd == 0 ? 1 : sd == 2 ? 0 : k), z = lz + (sd == 1 ? 1 : sd == 3 ? 0 : k);
                    gx[ngroups][n] = x;
                    gz[ngroups][n] = z;
                    n++;
                }
                gn[ngroups++] = n;
            }
            if (L->cells[f][cz][cx].flags & (LV_C_GRATE | LV_C_HATCH)) {
                if (ngroups < 64) {
                    int lx = (cx - r->x0) * 2, lz = (cz - r->z0) * 2;
                    gx[ngroups][0] = lx;
                    gz[ngroups][0] = lz;
                    gx[ngroups][1] = lx + 1;
                    gz[ngroups][1] = lz + 1;
                    gn[ngroups++] = 2;
                }
            }
        }
    if (ngroups == 0)
        return true;
    /* Every group needs a free node; flood from the first and reach them all. */
    int head = 0, tail = 0;
    for (int g = 0; g < ngroups; g++) {
        bool any = false;
        for (int k = 0; k < gn[g]; k++)
            any |= !blocked[gz[g][k]][gx[g][k]];
        if (!any)
            return false;
    }
    for (int k = 0; k < gn[0]; k++)
        if (!blocked[gz[0][k]][gx[0][k]] && !seen[gz[0][k]][gx[0][k]]) {
            seen[gz[0][k]][gx[0][k]] = 1;
            qx[tail] = (int16_t)gx[0][k];
            qz[tail++] = (int16_t)gz[0][k];
        }
    while (head < tail) {
        int x = qx[head], z = qz[head++];
        for (int sd = 0; sd < 4; sd++) {
            int nx = x + k_dx[sd], nz = z + k_dz[sd];
            if (nx < 0 || nz < 0 || nx >= nw || nz >= nd || blocked[nz][nx] || seen[nz][nx])
                continue;
            seen[nz][nx] = 1;
            qx[tail] = (int16_t)nx;
            qz[tail++] = (int16_t)nz;
        }
    }
    for (int g = 1; g < ngroups; g++) {
        bool any = false;
        for (int k = 0; k < gn[g]; k++)
            any |= seen[gz[g][k]][gx[g][k]] != 0;
        if (!any)
            return false;
    }
    /* No pockets: free floor cut off from the doors (beyond a stray node or
     * two behind furniture) would strand loot and patrol points. */
    int stranded = 0;
    for (int z = 0; z < nd; z++)
        for (int x = 0; x < nw; x++)
            stranded += !blocked[z][x] && !seen[z][x];
    (void)target;
    return stranded <= 2;
}

uint32_t nh_gen_stat_dressed[RF_COUNT], nh_gen_stat_bare[RF_COUNT], nh_gen_stat_retry[RF_COUNT];

void gen_dress_room(GenCtx *c, int room)
{
    LvRoom *r = &c->L->rooms[room];
    if (r->floor >= c->floors || c->gr[room].role == GR_STAIR || c->gr[room].dressed)
        return;
    c->gr[room].dressed = 1;
    /* Snapshot so a layout that blocks the way through can be undone. */
    static uint8_t occ_save[LV_D_MAX * OCC_RES][LV_W_MAX * OCC_RES];
    int ox0 = r->x0 * OCC_RES, oz0 = r->z0 * OCC_RES, ow = gen_rw(r) * OCC_RES, od = gen_rd(r) * OCC_RES;
    int f = r->floor;
    for (int z = 0; z < od; z++)
        memcpy(occ_save[z], &c->occ[f][oz0 + z][ox0], (size_t)ow);
    int nprops0 = c->L->nprops, nent0 = c->P->nent;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (attempt == 4) {
            nh_gen_stat_bare[r->func]++;
            break; /* leave it bare */
        }
        if (attempt < 2 && c->gr[room].setpiece)
            k_setpieces[c->gr[room].setpiece - 1].dress(c, room);
        else if (attempt < 3)
            dress_default(c, room);
        else {
            /* Last resort: a couple of things against the walls. */
            static const uint8_t light[] = { PROP_PLANT, PROP_FILING_CABINET, PROP_CRATE, PROP_LOCKER };
            along_walls(c, room, light[gen_ri(c, 0, 3)], 2);
        }
        if (room_walkable(c, room, nprops0)) {
            nh_gen_stat_dressed[r->func]++;
            if (attempt)
                nh_gen_stat_retry[r->func]++;
            return;
        }
        /* Undo and try again. */
        c->L->nprops = nprops0;
        c->P->nent = nent0;
        for (int z = 0; z < od; z++)
            memcpy(&c->occ[f][oz0 + z][ox0], occ_save[z], (size_t)ow);
        if (attempt == 1 && c->gr[room].setpiece) {
            c->gr[room].setpiece = 0;
            r->setpiece = 0;
        }
    }
}

/* Street side: parked cars, the getaway van, bins, a ladder to the roof. */
void gen_dress_outside(GenCtx *c)
{
    Level *L = c->L;
    float zs = (float)(c->bz0 - 3) * LV_CELL + 1.0f;
    for (int i = 0; i < 2 + c->size; i++) {
        float x = 5.0f + (float)gen_ri(c, 0, (int)((float)L->w * LV_CELL - 10.0f));
        gen_prop(c, PROP_CAR, 0, x, 0.0f, zs, NH_PI * 0.5f * (float)(gen_ri(c, 0, 1) * 2 - 1), 0);
    }
    /* Bins in the back yard. */
    float zb = (float)(c->bz1 + 2) * LV_CELL;
    for (int i = 0; i < 2; i++)
        gen_prop(c, PROP_DUMPSTER, 0, (float)gen_ri(c, c->bx0 + 1, c->bx1 - 1) * LV_CELL, 0.0f, zb, 0.0f, 0);
    /* Benches along the front pavement. */
    float zp = (float)(c->bz0 - 1) * LV_CELL + 0.6f;
    gen_prop(c, PROP_BENCH, 0, (float)gen_ri(c, c->bx0 + 1, c->bx1 - 1) * LV_CELL, 0.0f, zp, 0.0f, 0);
    /* Ladder up the side wall to the roof. */
    int side = gen_chance(c, 50) ? 2 : 0;
    int lx = side == 2 ? c->bx0 : c->bx1;
    int lz = gen_ri(c, c->bz0 + 2, c->bz1 - 2);
    NhVec3 ctr = lv_cell_center(0, lx, lz);
    float px = ctr.x + (float)k_dx[side] * (LV_CELL * 0.5f + 0.15f);
    int p = lv_add_prop(L, PROP_LADDER, 0, px, 0.0f, ctr.z, gen_wall_yaw((side + 2) & 3),
                        nh_prop_info[PROP_LADDER].half, nh_prop_info[PROP_LADDER].flags);
    if (p >= 0) {
        L->props[p].room = LV_ROOM_OUTSIDE;
        gen_entity(c, ENT_LADDER, LV_ROOM_OUTSIDE, p, nh_v3(px, 0.0f, ctr.z), L->props[p].yaw);
    }
}
