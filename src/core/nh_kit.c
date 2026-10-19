#include "nh_kit.h"

#include <math.h>
#include <string.h>

#define W NH_WALL_HALF
#define UVS 0.5f /* texture repeats every 2 m */

static const int k_dx[4] = { 1, 0, -1, 0 };
static const int k_dz[4] = { 0, 1, 0, -1 };

void nh_batches_init(NhBatchSet *s, NhArena *arena, uint32_t vcap, uint32_t icap)
{
    memset(s, 0, sizeof(*s));
    s->arena = arena;
    s->vcap = vcap;
    s->icap = icap;
}

NhMesh *nh_batches_get(NhBatchSet *s, int room, int layer, int material, uint32_t need)
{
    for (int i = s->n - 1; i >= 0; i--) {
        NhBatch *b = &s->b[i];
        if (b->room == room && b->layer == layer && b->material == material) {
            if (b->mesh.vcount + need + 64 < s->vcap && b->mesh.icount + need * 2 + 192 < s->icap)
                return &b->mesh;
            break; /* full: start a continuation batch */
        }
    }
    if (s->n >= NH_BATCHES_MAX) {
        s->overflow = true;
        return NULL;
    }
    NhBatch *b = &s->b[s->n];
    if (!nh_mesh_begin(&b->mesh, s->arena, s->vcap, s->icap, true)) {
        s->overflow = true;
        return NULL;
    }
    s->n++;
    b->room = (uint8_t)room;
    b->layer = (uint8_t)layer;
    b->material = (uint8_t)material;
    return &b->mesh;
}

static const NhColor WHITE = { 255, 255, 255, 255 };

/* Quad whose winding is corrected to face along n (CCW seen from the front). */
static void face(NhMesh *m, NhVec3 p0, NhVec3 p1, NhVec3 p2, NhVec3 p3, NhVec3 n, float u0, float v0, float u1,
                 float v1, int su, int sv)
{
    NhVec3 c = nh_v3_cross(nh_v3_sub(p1, p0), nh_v3_sub(p2, p0));
    if (nh_v3_dot(c, n) < 0.0f)
        nh_mesh_quad(m, p1, p0, p3, p2, n, u1, v0, u0, v1, su, sv, WHITE, 0);
    else
        nh_mesh_quad(m, p0, p1, p2, p3, n, u0, v0, u1, v1, su, sv, WHITE, 0);
}

/* Rectangle in a wall's (u, y) space -> world quad. origin is the wall start
 * point (u = 0) at y = 0, right is the unit direction of increasing u. */
static void wall_rect(NhMesh *m, NhVec3 origin, NhVec3 right, NhVec3 n, float u0, float u1, float y0, float y1,
                      float uoff)
{
    if (u1 - u0 < 0.001f || y1 - y0 < 0.001f)
        return;
    NhVec3 a = nh_v3_add(origin, nh_v3_scale(right, u0));
    NhVec3 b = nh_v3_add(origin, nh_v3_scale(right, u1));
    NhVec3 p0 = nh_v3(a.x, y0, a.z), p1 = nh_v3(b.x, y0, b.z), p2 = nh_v3(b.x, y1, b.z), p3 = nh_v3(a.x, y1, a.z);
    int su = (int)ceilf((u1 - u0) / 0.5f), sv = (int)ceilf((y1 - y0) / 0.6f);
    nh_mesh_quad(m, p0, p1, p2, p3, n, (uoff + u0) * UVS, -y0 * UVS, (uoff + u1) * UVS, -y1 * UVS, su, sv, WHITE,
                 0);
}

typedef struct Hole {
    float u0, u1, y0, y1; /* relative to the edge start (u in 0..2), y relative to the floor */
} Hole;

static bool edge_hole(const LvEdge *e, Hole *h)
{
    switch (e->type) {
    case LV_E_DOOR:
    case LV_E_ARCH:
        *h = (Hole){ 0.5f, 1.5f, 0.0f, LV_DOOR_H };
        return true;
    case LV_E_WINDOW:
        *h = (Hole){ 0.3f, 1.7f, LV_WIN_Y0, LV_WIN_Y1 };
        return true;
    default:
        return false;
    }
}

/* A wall with an optional hole. u runs from the edge start; ext extends both ends. */
static void wall_with_hole(NhMesh *m, NhVec3 origin, NhVec3 right, NhVec3 n, float ext, float base, float top,
                           const Hole *h, float uoff)
{
    if (!h) {
        wall_rect(m, origin, right, n, -ext, LV_CELL + ext, base, top, uoff);
        return;
    }
    float hy0 = base + h->y0, hy1 = NH_MIN(base + h->y1, top);
    wall_rect(m, origin, right, n, -ext, h->u0, base, top, uoff);
    wall_rect(m, origin, right, n, h->u1, LV_CELL + ext, base, top, uoff);
    wall_rect(m, origin, right, n, h->u0, h->u1, base, hy0, uoff);
    wall_rect(m, origin, right, n, h->u0, h->u1, hy1, top, uoff);
}

/* Reveal faces lining a hole through a wall of thickness 2W centred on the
 * boundary. `origin` is the boundary start, right/n as for the wall. */
static void hole_reveal(NhMesh *m, NhVec3 origin, NhVec3 right, NhVec3 n, float base, const Hole *h)
{
    NhVec3 a = nh_v3_add(origin, nh_v3_scale(right, h->u0));
    NhVec3 b = nh_v3_add(origin, nh_v3_scale(right, h->u1));
    NhVec3 in = nh_v3_scale(n, W), out = nh_v3_scale(n, -W);
    float y0 = base + h->y0, y1 = base + h->y1;
    NhVec3 na = right, nb = nh_v3_scale(right, -1.0f);
    /* Left jamb faces +right, right jamb faces -right. */
    NhVec3 ai = nh_v3_add(a, in), ao = nh_v3_add(a, out), bi = nh_v3_add(b, in), bo = nh_v3_add(b, out);
    face(m, nh_v3(ao.x, y0, ao.z), nh_v3(ai.x, y0, ai.z), nh_v3(ai.x, y1, ai.z), nh_v3(ao.x, y1, ao.z), na,
                 0, -y0 * UVS, 0.08f, -y1 * UVS, 1, 3);
    face(m, nh_v3(bi.x, y0, bi.z), nh_v3(bo.x, y0, bo.z), nh_v3(bo.x, y1, bo.z), nh_v3(bi.x, y1, bi.z), nb,
                 0, -y0 * UVS, 0.08f, -y1 * UVS, 1, 3);
    /* Lintel underside faces down. */
    face(m, nh_v3(ai.x, y1, ai.z), nh_v3(bi.x, y1, bi.z), nh_v3(bo.x, y1, bo.z), nh_v3(ao.x, y1, ao.z),
                 nh_v3(0, -1, 0), 0, 0, 0.5f, 0.08f, 2, 1);
    if (h->y0 > 0.01f) /* window sill faces up */
        face(m, nh_v3(ao.x, y0, ao.z), nh_v3(bo.x, y0, bo.z), nh_v3(bi.x, y0, bi.z), nh_v3(ai.x, y0, ai.z),
                     nh_v3(0, 1, 0), 0, 0, 0.5f, 0.08f, 2, 1);
}

static bool indoor_room(const Level *L, int r)
{
    return r >= LV_ROOM_FIRST && !(L->rooms[r].flags & LV_R_OUTDOOR);
}

static int surface_material(const Level *L, int room, const LvCell *c, const NhKitMaterials *km)
{
    if (room == LV_ROOM_OUTSIDE && c->surface == LV_SURF_GRASS)
        return km->ground;
    return km->map[L->rooms[room].floor_mat];
}

/* Edge start (u = 0) for side s of cell (x,z): the corner where `right` begins. */
static void side_frame(int x, int z, int s, NhVec3 *start, NhVec3 *right, NhVec3 *inward)
{
    float x0 = (float)x * LV_CELL, z0 = (float)z * LV_CELL;
    NhVec3 n = nh_v3((float)-k_dx[s], 0, (float)-k_dz[s]);
    *inward = n;
    *right = nh_v3(n.z, 0, -n.x);
    /* Boundary centre minus half the edge along `right`. */
    NhVec3 c = nh_v3(x0 + LV_CELL * 0.5f + k_dx[s] * LV_CELL * 0.5f, 0, z0 + LV_CELL * 0.5f + k_dz[s] * LV_CELL * 0.5f);
    *start = nh_v3_sub(c, nh_v3_scale(*right, LV_CELL * 0.5f));
}

static void emit_rail(NhMesh *m, NhVec3 start, NhVec3 right, NhVec3 n, float base)
{
    NhVec3 a = start, b = nh_v3_add(start, nh_v3_scale(right, LV_CELL));
    NhVec3 off = nh_v3_scale(n, 0.05f);
    for (int k = 0; k < 2; k++) {
        float y = base + (k ? 0.95f : 0.5f), t = k ? 0.05f : 0.03f;
        NhVec3 lo = nh_v3(fminf(a.x, b.x) + off.x - t, y, fminf(a.z, b.z) + off.z - t);
        NhVec3 hi = nh_v3(fmaxf(a.x, b.x) + off.x + t, y + t, fmaxf(a.z, b.z) + off.z + t);
        nh_mesh_box(m, lo, hi, 1.0f, WHITE, 0);
    }
    for (int k = 0; k <= 2; k++) {
        NhVec3 p = nh_v3_add(nh_v3_add(a, nh_v3_scale(right, LV_CELL * 0.5f * (float)k)), off);
        nh_mesh_box(m, nh_v3(p.x - 0.025f, base, p.z - 0.025f), nh_v3(p.x + 0.025f, base + 1.0f, p.z + 0.025f), 1.0f,
                    WHITE, 0);
    }
}

static void emit_stairs(const Level *L, const LvStair *s, NhMesh *m)
{
    const int steps = 18;
    float run = LV_STAIR_LEN * LV_CELL, rise = LV_STOREY;
    float base = lv_floor_y(s->floor);
    NhVec3 dir = nh_v3((float)k_dx[s->dir], 0, (float)k_dz[s->dir]);
    NhVec3 side = nh_v3(dir.z, 0, -dir.x); /* right when facing up the stairs... */
    NhVec3 start = lv_cell_center(s->floor, s->x, s->z);
    start = nh_v3_sub(start, nh_v3_scale(dir, LV_CELL * 0.5f));
    float hw = LV_CELL * 0.5f - 0.05f;
    (void)L;
    for (int i = 0; i < steps; i++) {
        float t0 = run * (float)i / steps, t1 = run * (float)(i + 1) / steps;
        float y0 = base + rise * (float)i / steps, y1 = base + rise * (float)(i + 1) / steps;
        NhVec3 c0 = nh_v3_add(start, nh_v3_scale(dir, t0)), c1 = nh_v3_add(start, nh_v3_scale(dir, t1));
        NhVec3 l0 = nh_v3_sub(c0, nh_v3_scale(side, hw)), r0 = nh_v3_add(c0, nh_v3_scale(side, hw));
        NhVec3 l1 = nh_v3_sub(c1, nh_v3_scale(side, hw)), r1 = nh_v3_add(c1, nh_v3_scale(side, hw));
        /* tread (up) */
        face(m, nh_v3(r0.x, y1, r0.z), nh_v3(l0.x, y1, l0.z), nh_v3(l1.x, y1, l1.z), nh_v3(r1.x, y1, r1.z),
                     nh_v3(0, 1, 0), 0, t0 * UVS, 1.0f, t1 * UVS, 2, 1);
        /* riser faces back down the stairs (-dir) */
        face(m, nh_v3(l0.x, y0, l0.z), nh_v3(r0.x, y0, r0.z), nh_v3(r0.x, y1, r0.z), nh_v3(l0.x, y1, l0.z),
                     nh_v3_scale(dir, -1.0f), 0, -y0 * UVS, 1.0f, -y1 * UVS, 2, 1);
        /* stringers: solid sides from the floor up to the tread */
        face(m, nh_v3(r0.x, base, r0.z), nh_v3(r1.x, base, r1.z), nh_v3(r1.x, y1, r1.z), nh_v3(r0.x, y1, r0.z),
                     side, t0 * UVS, -base * UVS, t1 * UVS, -y1 * UVS, 1, 2);
        face(m, nh_v3(l1.x, base, l1.z), nh_v3(l0.x, base, l0.z), nh_v3(l0.x, y1, l0.z), nh_v3(l1.x, y1, l1.z),
                     nh_v3_scale(side, -1.0f), t1 * UVS, -base * UVS, t0 * UVS, -y1 * UVS, 1, 2);
    }
}

void nh_kit_build_room(const Level *L, int room, const NhKitMaterials *km, NhBatchSet *out)
{
    const LvRoom *R = &L->rooms[room];
    bool outside = room == LV_ROOM_OUTSIDE;
    bool roof = (R->flags & LV_R_ROOF) != 0;
    int f = R->floor;
    float base = lv_floor_y(f);

    /* ---- cells: floors, ceilings, stairs, ducts ---- */
    for (int z = R->z0; z <= R->z1; z++) {
        for (int x = R->x0; x <= R->x1; x++) {
            const LvCell *c = lv_cell(L, f, x, z);
            if (!c || c->room != room)
                continue;
            float x0 = (float)x * LV_CELL, x1 = x0 + LV_CELL, z0 = (float)z * LV_CELL, z1 = z0 + LV_CELL;
            if (!(c->flags & LV_C_HOLE)) {
                int mat = roof ? km->roof : surface_material(L, room, c, km);
                NhMesh *m = nh_batches_get(out, room, NH_LAYER_ROOM, mat, 40);
                if (m)
                    face(m, nh_v3(x0, base, z1), nh_v3(x1, base, z1), nh_v3(x1, base, z0), nh_v3(x0, base, z0),
                                 nh_v3(0, 1, 0), x0 * UVS, z1 * UVS, x1 * UVS, z0 * UVS, 4, 4);
            }
            const LvCell *above = lv_cell(L, f + 1, x, z);
            bool open_above = above && (above->flags & LV_C_HOLE);
            if (!(c->flags & LV_C_NOCEIL) && !open_above && !outside && !roof) {
                float y = base + LV_CEIL;
                NhMesh *m = nh_batches_get(out, room, NH_LAYER_ROOM, R->ceil_mat ? km->map[R->ceil_mat] : km->ceiling, 20);
                if (m)
                    face(m, nh_v3(x0, y, z0), nh_v3(x1, y, z0), nh_v3(x1, y, z1), nh_v3(x0, y, z1),
                                 nh_v3(0, -1, 0), x0 * UVS, z0 * UVS, x1 * UVS, z1 * UVS, 2, 2);
            }
            if (c->stair) {
                NhMesh *m = nh_batches_get(out, room, NH_LAYER_ROOM, km->stair, 1600);
                const LvStair *st = &L->stairs[c->stair - 1];
                if (m && st->x == x && st->z == z)
                    emit_stairs(L, st, m);
            }
            /* Vent duct in this cell's ceiling void. */
            if (c->flags & LV_C_VENT) {
                NhMesh *m = nh_batches_get(out, room, NH_LAYER_VENT, km->duct, 64);
                if (m) {
                    float yl = base + LV_CEIL + 0.02f, yh = base + LV_STOREY - 0.02f;
                    if (!(c->flags & LV_C_GRATE))
                        face(m, nh_v3(x0, yl, z1), nh_v3(x1, yl, z1), nh_v3(x1, yl, z0), nh_v3(x0, yl, z0),
                                     nh_v3(0, 1, 0), 0, 1, 1, 0, 2, 2);
                    face(m, nh_v3(x0, yh, z0), nh_v3(x1, yh, z0), nh_v3(x1, yh, z1), nh_v3(x0, yh, z1),
                                 nh_v3(0, -1, 0), 0, 0, 1, 1, 2, 2);
                    for (int s = 0; s < 4; s++) {
                        const LvCell *nb = lv_cell(L, f, x + k_dx[s], z + k_dz[s]);
                        if (nb && (nb->flags & LV_C_VENT))
                            continue;
                        NhVec3 st, rt, n;
                        side_frame(x, z, s, &st, &rt, &n);
                        wall_rect(m, nh_v3_add(st, nh_v3_scale(n, 0.02f)), rt, n, 0, LV_CELL, yl, yh, 0);
                    }
                }
            }
            /* ---- edges of this cell ---- */
            for (int s = 0; s < 4; s++) {
                const LvEdge *e = lv_edge_side_c(L, f, x, z, s);
                if (!e || e->type == LV_E_OPEN)
                    continue;
                int nr = lv_neighbor_room(L, f, x, z, s);
                NhVec3 st, rt, n;
                side_frame(x, z, s, &st, &rt, &n);
                float uoff = (s == 0 || s == 2) ? z0 : x0;
                if (e->type == LV_E_RAIL) {
                    if (roof && nr == LV_ROOM_VOID) {
                        /* Roof parapet: a solid low wall, both faces. */
                        NhMesh *m = nh_batches_get(out, room, NH_LAYER_ROOM, km->parapet, 120);
                        if (m) {
                            NhVec3 o = nh_v3_add(st, nh_v3_scale(n, W * 2.0f));
                            wall_rect(m, o, rt, n, -W * 2, LV_CELL + W * 2, base, base + 1.0f, uoff);
                            NhVec3 outer = nh_v3_add(st, nh_v3_scale(n, -W));
                            NhVec3 rr = nh_v3_scale(rt, -1.0f), on = nh_v3_scale(n, -1.0f);
                            NhVec3 o2 = nh_v3_add(outer, nh_v3_scale(rt, LV_CELL));
                            wall_rect(m, o2, rr, on, -W, LV_CELL + W, base - 0.6f, base + 1.0f, uoff);
                            NhVec3 a = nh_v3_add(outer, nh_v3_scale(rt, -W)), b = nh_v3_add(o, nh_v3_scale(rt, -W * 2));
                            NhVec3 a2 = nh_v3_add(a, nh_v3_scale(rt, LV_CELL + W * 2)),
                                   b2 = nh_v3_add(b, nh_v3_scale(rt, LV_CELL + W * 4));
                            float y = base + 1.0f;
                            face(m, nh_v3(a.x, y, a.z), nh_v3(b.x, y, b.z), nh_v3(b2.x, y, b2.z),
                                         nh_v3(a2.x, y, a2.z), nh_v3(0, 1, 0), 0, 0, 1, 0.2f, 2, 1);
                        }
                    } else {
                        NhMesh *m = nh_batches_get(out, room, NH_LAYER_ROOM, km->rail, 200);
                        if (m && (s == 0 || s == 1 || nr != room))
                            emit_rail(m, st, rt, n, base);
                        /* Slab edge around stair holes, seen from below. */
                        if (m && (c->flags & LV_C_HOLE)) {
                            const LvCell *nc = lv_cell(L, f, x + k_dx[s], z + k_dz[s]);
                            if (nc && !(nc->flags & LV_C_HOLE)) {
                                NhVec3 o = nh_v3_add(st, nh_v3_scale(n, -0.0f));
                                wall_rect(m, o, rt, n, 0, LV_CELL, base - (LV_STOREY - LV_CEIL), base, uoff);
                            }
                        }
                    }
                    continue;
                }
                /* Wall-type edge (wall, door, window, arch). */
                Hole h;
                bool has_hole = edge_hole(e, &h);
                if (outside) {
                    /* Facade: outer face of the building, full storey height. */
                    if (!indoor_room(L, nr))
                        continue;
                    int top = L->floors;
                    for (int ff = 0; ff < top; ff++) {
                        const LvEdge *fe = lv_edge_side_c(L, ff, x, z, s);
                        int fr = lv_neighbor_room(L, ff, x, z, s);
                        if (!fe || !indoor_room(L, fr))
                            continue;
                        Hole fh;
                        bool fhas = edge_hole(fe, &fh);
                        NhMesh *m = nh_batches_get(out, room, NH_LAYER_ROOM, km->facade, 200);
                        if (!m)
                            continue;
                        NhVec3 o = nh_v3_add(st, nh_v3_scale(n, W));
                        float fb = lv_floor_y(ff), ftop = fb + LV_STOREY;
                        wall_with_hole(m, o, rt, n, W, fb, ftop, fhas ? &fh : NULL, uoff);
                        if (fhas) {
                            NhMesh *rm = nh_batches_get(out, room, NH_LAYER_ROOM, km->frame, 40);
                            if (rm)
                                hole_reveal(rm, st, rt, n, fb, &fh);
                        }
                    }
                    continue;
                }
                if (nr == LV_ROOM_VOID && f > 0 && !indoor_room(L, room))
                    continue;
                /* Interior face, offset into this cell. */
                float top = base + LV_CEIL;
                const LvCell *above = lv_cell(L, f + 1, x, z);
                if (above && (above->flags & LV_C_HOLE))
                    top = base + LV_STOREY; /* double-height stairwell */
                if (roof)
                    top = base + 1.0f;
                int wm = km->map[R->wall_mat];
                NhMesh *m = nh_batches_get(out, room, NH_LAYER_ROOM, wm, 200);
                if (!m)
                    continue;
                NhVec3 o = nh_v3_add(st, nh_v3_scale(n, W));
                wall_with_hole(m, o, rt, n, W, base, top, has_hole ? &h : NULL, uoff);
                /* Reveals: emitted once per opening, by the + side owner or the
                 * indoor side of an exterior opening. */
                if (has_hole) {
                    bool owner = indoor_room(L, nr) ? (s == 0 || s == 1) : true;
                    if (owner)
                        hole_reveal(m, st, rt, n, base, &h);
                }
            }
        }
    }
}

void nh_kit_room_bounds(const Level *L, int room, NhVec3 *bmin, NhVec3 *bmax)
{
    const LvRoom *R = &L->rooms[room];
    float base = lv_floor_y(R->floor);
    *bmin = nh_v3((float)R->x0 * LV_CELL - 0.2f, base - 0.7f, (float)R->z0 * LV_CELL - 0.2f);
    *bmax = nh_v3((float)(R->x1 + 1) * LV_CELL + 0.2f, base + LV_STOREY + 0.2f, (float)(R->z1 + 1) * LV_CELL + 0.2f);
    if (room == LV_ROOM_OUTSIDE)
        bmax->y = lv_floor_y(L->floors) + 1.5f;
}
