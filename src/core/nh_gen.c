/* Night Heist - procedural buildings: footprint, room plans, stairs, doors,
 * windows, vents, functions, validation and fingerprints. Dressing lives in
 * nh_gen_dress.c, security and guards in nh_gen_sec.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nh_gen_internal.h"

const char *const nh_building_names[BT_COUNT] = { "warehouse", "office", "bank", "mansion", "museum" };
const char *const nh_size_names[BS_COUNT] = { "small", "medium", "large" };

static const int k_dx[4] = { 1, 0, -1, 0 };
static const int k_dz[4] = { 0, 1, 0, -1 };

/*                                         S   M   L */
static const uint8_t k_floors[BT_COUNT][BS_COUNT] = { { 1, 1, 2 }, { 2, 3, 4 }, { 2, 2, 3 }, { 2, 2, 3 }, { 1, 2, 2 } };
static const uint8_t k_width[BT_COUNT][BS_COUNT] = { { 15, 19, 23 }, { 13, 17, 21 }, { 13, 17, 21 }, { 13, 16, 19 },
                                                     { 15, 19, 23 } };
static const uint8_t k_depth[BT_COUNT][BS_COUNT] = { { 12, 14, 16 }, { 11, 12, 13 }, { 13, 14, 15 }, { 11, 12, 14 },
                                                     { 11, 13, 15 } };

#define MARGIN_FRONT 4
#define MARGIN 3

static GenCtx s_ctx;

float gen_wall_yaw(int side)
{
    static const float y[4] = { NH_PI * 0.5f, 0.0f, -NH_PI * 0.5f, NH_PI };
    return y[side & 3];
}

int gen_room_edges(const Level *L, int room, int type)
{
    const LvRoom *r = &L->rooms[room];
    int n = 0;
    for (int z = r->z0; z <= r->z1; z++)
        for (int x = r->x0; x <= r->x1; x++)
            for (int s = 0; s < 4; s++) {
                int nx = x + k_dx[s], nz = z + k_dz[s];
                if (lv_in_bounds(L, r->floor, nx, nz) && L->cells[r->floor][nz][nx].room == room)
                    continue;
                const LvEdge *e = lv_edge_side_c(L, r->floor, x, z, s);
                if (e && e->type == type)
                    n++;
            }
    return n;
}

/* ---- rooms --------------------------------------------------------------- */

static int mk(GenCtx *c, int f, int x0, int z0, int x1, int z1, int role)
{
    if (x0 > x1 || z0 > z1)
        return 0;
    int r = lv_add_room(c->L, f, x0, z0, x1, z1, RF_NONE, 0);
    if (r) {
        memset(&c->gr[r], 0, sizeof(c->gr[r]));
        c->gr[r].role = (uint8_t)role;
        c->gr[r].entry_door = -1;
        c->gr[r].parent = -1;
    }
    return r;
}

/* (u, v) strip coordinates -> world cells. axis 0: u = x, v = z; axis 1: u = z, v = x. */
static int mk_uv(GenCtx *c, int f, int axis, int u0, int u1, int v0, int v1, int role)
{
    return axis == 0 ? mk(c, f, u0, v0, u1, v1, role) : mk(c, f, v0, u0, v1, u1, role);
}

/* Door requests resolved once every room exists. */
typedef struct DoorReq {
    uint8_t f, a, b, arch;
} DoorReq;
static DoorReq s_req[600];
static int s_nreq;

static void want_door(int f, int a, int b, bool arch)
{
    if (!a || !b || a == b || s_nreq >= (int)NH_ARRAY_LEN(s_req))
        return;
    for (int i = 0; i < s_nreq; i++)
        if (((s_req[i].a == a && s_req[i].b == b) || (s_req[i].a == b && s_req[i].b == a)) && s_req[i].arch == arch)
            return;
    s_req[s_nreq++] = (DoorReq){ (uint8_t)f, (uint8_t)a, (uint8_t)b, (uint8_t)arch };
}

/* Split [a..b] into pieces of width minw..maxw (the last absorbs the rest). */
static int split_span(GenCtx *c, int a, int b, int minw, int maxw, int *starts, int max)
{
    int n = 0;
    int x = a;
    while (x <= b && n < max) {
        int left = b - x + 1;
        int w;
        if (left <= maxw)
            w = left;
        else {
            w = gen_ri(c, minw, maxw);
            if (left - w < minw)
                w = left - minw;
        }
        starts[n++] = x;
        x += w;
    }
    return n;
}

typedef struct Strip {
    int f, axis;
    int u0, u1, v0, v1;   /* strip extent */
    int corr_v;           /* v of the corridor row next to the strip */
    int corr_room;
    int skip_u0, skip_u1; /* a fixed block (stairwell) to leave out; skip_u0 > skip_u1 for none */
    int minw, maxw;
    int back_pct;         /* chance of splitting deep rooms into front + back */
    int link_pct;         /* chance of a door to the neighbouring room */
    bool arches;          /* neighbours joined by arches (galleries) */
} Strip;

static void fill_strip(GenCtx *c, const Strip *s)
{
    int spans[2][2] = { { s->u0, s->skip_u0 - 1 }, { s->skip_u1 + 1, s->u1 } };
    if (s->skip_u0 > s->skip_u1) {
        spans[0][1] = s->u1;
        spans[1][0] = 1;
        spans[1][1] = 0;
    }
    int depth = s->v1 - s->v0 + 1;
    bool corr_low = s->corr_v < s->v0; /* corridor on the low-v side */
    for (int k = 0; k < 2; k++) {
        int a = spans[k][0], b = spans[k][1];
        if (a > b)
            continue;
        int starts[32];
        int n = split_span(c, a, b, s->minw, s->maxw, starts, 32);
        int prev = 0;
        for (int i = 0; i < n; i++) {
            int x0 = starts[i], x1 = i + 1 < n ? starts[i + 1] - 1 : b;
            int w = x1 - x0 + 1;
            int front, back = 0;
            if (depth >= 6 && w >= 3 && gen_chance(c, s->back_pct)) {
                int fd = gen_ri(c, 3, depth - 3);
                int fv0 = corr_low ? s->v0 : s->v1 - fd + 1, fv1 = corr_low ? s->v0 + fd - 1 : s->v1;
                int bv0 = corr_low ? s->v0 + fd : s->v0, bv1 = corr_low ? s->v1 : s->v1 - fd;
                front = mk_uv(c, s->f, s->axis, x0, x1, fv0, fv1, GR_ROOM);
                back = mk_uv(c, s->f, s->axis, x0, x1, bv0, bv1, GR_BACK);
                want_door(s->f, front, back, false);
            } else {
                front = mk_uv(c, s->f, s->axis, x0, x1, s->v0, s->v1, GR_ROOM);
            }
            if (s->corr_room)
                want_door(s->f, front, s->corr_room, false);
            if (prev && (s->arches || gen_chance(c, s->link_pct)))
                want_door(s->f, prev, front, s->arches);
            prev = front;
            (void)back;
        }
    }
}

/* ---- stairwell ------------------------------------------------------------ */

/* Stairwell local frame: a = 0..2 across, s = 0..len-1 along; s = 0 is the
 * door end. Even floors climb in column 0 (rows 1..3, +s), odd floors in
 * column 2 (rows 3..1, -s); column 1 is a walkway that stays connected on
 * every floor. */
static void st_cell(const GenCtx *c, int a, int s, int *x, int *z)
{
    if (c->st_flip)
        s = -s;
    if (c->st_axis == 0) {
        *x = c->stx + a;
        *z = c->stz + s;
    } else {
        *x = c->stx + s;
        *z = c->stz + a;
    }
}

static int st_dir(const GenCtx *c, bool up_s)
{
    if (c->st_flip)
        up_s = !up_s;
    if (c->st_axis == 0)
        return up_s ? 1 : 3;
    return up_s ? 0 : 2;
}

static void add_stairs(GenCtx *c)
{
    for (int f = 0; f < c->floors - 1; f++) {
        int x, z;
        if ((f & 1) == 0) {
            st_cell(c, 0, 1, &x, &z);
            lv_add_stair(c->L, f, x, z, st_dir(c, true));
        } else {
            st_cell(c, 2, 3, &x, &z);
            lv_add_stair(c->L, f, x, z, st_dir(c, false));
        }
    }
}

/* Stairwell rooms on every floor; len = depth of the strip it sits in. */
static void make_stairwells(GenCtx *c, int len)
{
    for (int f = 0; f < c->floors; f++) {
        int x0, z0, x1, z1;
        st_cell(c, 0, 0, &x0, &z0);
        st_cell(c, 2, len - 1, &x1, &z1);
        c->stair_room[f] = mk(c, f, NH_MIN(x0, x1), NH_MIN(z0, z1), NH_MAX(x0, x1), NH_MAX(z0, z1), GR_STAIR);
    }
}

/* Door from the stairwell's door end (centre column) into whatever is there. */
static void stair_doors(GenCtx *c)
{
    for (int f = 0; f < c->floors; f++) {
        int x, z;
        st_cell(c, 1, 0, &x, &z);
        int side = c->st_axis == 0 ? (c->st_flip ? 1 : 3) : (c->st_flip ? 0 : 2); /* -s direction */
        int nb = lv_neighbor_room(c->L, f, x, z, side);
        if (nb >= LV_ROOM_FIRST)
            want_door(f, c->stair_room[f], nb, false);
    }
}

/* ---- layouts --------------------------------------------------------------- */

/* Corridor spine along X with strips in front and behind. The stairwell sits
 * in the back strip with its door end on the corridor. */
static void layout_spine(GenCtx *c, int cw, int minw, int maxw, int back_pct, int link_pct, bool arches,
                         bool cross)
{
    int D = c->bz1 - c->bz0 + 1;
    int db = gen_ri(c, 5, NH_MAX(5, (D - cw) / 2 + 1)); /* back strip depth; the front gets the rest */
    if (c->type == BT_BANK && D - cw - db < 5)
        db = D - cw - 5;
    int zc = c->bz1 - db - cw + 1;               /* first corridor row */
    int da = zc - c->bz0;
    (void)da;
    int W = c->bx1 - c->bx0 + 1;
    c->st_axis = 0;
    c->st_flip = 0;
    c->stx = c->bx0 + gen_ri(c, W / 4, W - W / 4 - 3);
    c->stz = zc + cw;
    if (c->floors > 1)
        make_stairwells(c, db);
    /* Optional cross corridor through the front strip (large buildings). */
    int xc = -100;
    if (cross) {
        xc = c->bx0 + gen_ri(c, W / 3, 2 * W / 3);
        if (xc >= c->stx - 1 && xc <= c->stx + 3)
            xc = c->stx > c->bx0 + W / 2 ? c->stx - 4 : c->stx + 5;
    }
    for (int f = 0; f < c->floors; f++) {
        int corr = mk(c, f, c->bx0, zc, c->bx1, zc + cw - 1, GR_CORRIDOR);
        c->corridor[f] = corr;
        int cross_room = 0;
        if (cross && xc > c->bx0 + 2 && xc < c->bx1 - 2) {
            cross_room = mk(c, f, xc, c->bz0 + (f == 0 ? 0 : 0), xc, zc - 1, GR_CORRIDOR);
            want_door(f, corr, cross_room, true);
        }
        Strip back = { f, 0, c->bx0, c->bx1, zc + cw, c->bz1, zc + cw - 1, corr, 1, 0, minw, maxw,
                       back_pct, link_pct, arches };
        if (c->floors > 1) {
            back.skip_u0 = c->stx;
            back.skip_u1 = c->stx + 2;
        }
        fill_strip(c, &back);
        Strip front = { f, 0, c->bx0, c->bx1, c->bz0, zc - 1, zc, corr, 1, 0, minw, maxw, back_pct / 2, link_pct,
                        arches };
        if (cross_room) {
            front.skip_u0 = xc;
            front.skip_u1 = xc;
        }
        fill_strip(c, &front);
        if (cross_room) {
            /* Rooms beside the cross corridor open onto it too. */
            for (int z = c->bz0; z < zc; z++) {
                int l = lv_room_at(c->L, f, xc - 1, z), r = lv_room_at(c->L, f, xc + 1, z);
                if (l >= LV_ROOM_FIRST && gen_chance(c, 40))
                    want_door(f, l, cross_room, false);
                if (r >= LV_ROOM_FIRST && gen_chance(c, 40))
                    want_door(f, r, cross_room, false);
            }
        }
    }
}

/* Mansion: a grand hall running from the front door to the stairs at the
 * back, rooms on both sides joined to each other as well as to the hall. */
static void bsp(GenCtx *c, int f, int x0, int z0, int x1, int z1, int minw, int maxw, int *out, int *n, int max)
{
    int w = x1 - x0 + 1, d = z1 - z0 + 1;
    bool split_x = w > maxw || (w >= 2 * minw && w * d > 30 && gen_chance(c, 55));
    bool split_z = d > maxw || (d >= 2 * minw && w * d > 30 && gen_chance(c, 55));
    if (w < 2 * minw)
        split_x = false;
    if (d < 2 * minw)
        split_z = false;
    if (split_x && split_z) {
        if (w >= d)
            split_z = false;
        else
            split_x = false;
    }
    if (split_x) {
        int s = gen_ri(c, x0 + minw, x1 - minw + 1);
        bsp(c, f, x0, z0, s - 1, z1, minw, maxw, out, n, max);
        bsp(c, f, s, z0, x1, z1, minw, maxw, out, n, max);
    } else if (split_z) {
        int s = gen_ri(c, z0 + minw, z1 - minw + 1);
        bsp(c, f, x0, z0, x1, s - 1, minw, maxw, out, n, max);
        bsp(c, f, x0, s, x1, z1, minw, maxw, out, n, max);
    } else if (*n < max) {
        out[(*n)++] = mk(c, f, x0, z0, x1, z1, GR_ROOM);
    }
}

static void layout_mansion(GenCtx *c)
{
    int W = c->bx1 - c->bx0 + 1;
    int hx = c->bx0 + gen_ri(c, W / 3, W - W / 3 - 3);
    c->st_axis = 0;
    c->st_flip = 0;
    c->stx = hx;
    c->stz = c->bz1 - 4;
    /* The stairwell's door end faces the hall in front of it (-Z), so the
     * frame runs backwards: build it with s = 0 at the hall. */
    make_stairwells(c, 5);
    for (int f = 0; f < c->floors; f++) {
        int hall = mk(c, f, hx, c->bz0, hx + 2, c->stz - 1, GR_HALL);
        c->corridor[f] = hall;
        int rooms[48], n = 0;
        bsp(c, f, c->bx0, c->bz0, hx - 1, c->bz1, 3, 6, rooms, &n, 24);
        bsp(c, f, hx + 3, c->bz0, c->bx1, c->bz1, 3, 6, rooms, &n, 48);
        /* Hall doors for rooms touching it; neighbours joined at random. */
        for (int i = 0; i < n; i++) {
            const LvRoom *r = &c->L->rooms[rooms[i]];
            bool touches = (r->x1 == hx - 1 || r->x0 == hx + 3) && r->z0 < c->stz;
            if (touches)
                want_door(f, rooms[i], hall, false);
        }
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                const LvRoom *a = &c->L->rooms[rooms[i]], *b = &c->L->rooms[rooms[j]];
                bool adj_x = (a->x1 + 1 == b->x0 || b->x1 + 1 == a->x0) && a->z0 <= b->z1 && b->z0 <= a->z1;
                bool adj_z = (a->z1 + 1 == b->z0 || b->z1 + 1 == a->z0) && a->x0 <= b->x1 && b->x0 <= a->x1;
                if ((adj_x || adj_z) && gen_chance(c, 45))
                    want_door(f, rooms[i], rooms[j], false);
            }
    }
}

/* Warehouse: a big hall with a loading bay along the back, an office block
 * down one side or along the street front (with the stairs on two-storey
 * sites), sometimes a row of cage rooms and a partition wall in the hall. */
static void layout_warehouse(GenCtx *c)
{
    int variant = gen_ri(c, 0, 2); /* 0 block left, 1 block right, 2 block along the front */
    int D = c->bz1 - c->bz0 + 1;
    int bw = gen_ri(c, 5, 7);
    if (c->floors > 1)
        bw = NH_MAX(bw, 6);
    if (variant == 2 && D - bw < 8)
        variant = gen_ri(c, 0, 1);
    int ld = gen_ri(c, 2, 4);
    int hx0 = c->bx0, hx1 = c->bx1, hz0 = c->bz0, hz1 = c->bz1 - ld;
    int ox0, ox1, oz0, oz1, corr_x0, corr_x1, corr_z0, corr_z1;
    if (variant == 0) {
        ox0 = c->bx0, ox1 = c->bx0 + bw - 2, oz0 = c->bz0, oz1 = c->bz1;
        corr_x0 = corr_x1 = c->bx0 + bw - 1, corr_z0 = c->bz0, corr_z1 = c->bz1;
        hx0 = corr_x0 + 1;
        c->st_axis = 1;
        c->st_flip = 1;
        c->stx = corr_x0 - 1;
        c->stz = c->bz1 - 2;
    } else if (variant == 1) {
        ox0 = c->bx1 - bw + 2, ox1 = c->bx1, oz0 = c->bz0, oz1 = c->bz1;
        corr_x0 = corr_x1 = c->bx1 - bw + 1, corr_z0 = c->bz0, corr_z1 = c->bz1;
        hx1 = corr_x0 - 1;
        c->st_axis = 1;
        c->st_flip = 0;
        c->stx = corr_x0 + 1;
        c->stz = c->bz1 - 2;
    } else {
        ox0 = c->bx0, ox1 = c->bx1, oz0 = c->bz0, oz1 = c->bz0 + bw - 2;
        corr_x0 = c->bx0, corr_x1 = c->bx1, corr_z0 = corr_z1 = c->bz0 + bw - 1;
        hz0 = corr_z0 + 1;
        int W = c->bx1 - c->bx0 + 1;
        c->st_axis = 0;
        c->st_flip = 1;
        c->stx = c->bx0 + gen_ri(c, 2, W - 5);
        c->stz = corr_z0 - 1;
    }
    if (c->floors > 1)
        make_stairwells(c, 5);
    /* Optional cage strip along the hall's far side. */
    bool cages = gen_chance(c, 55) && hx1 - hx0 >= 10;
    int cw = gen_ri(c, 3, 4);
    bool cage_left = variant == 1 ? true : variant == 0 ? false : gen_chance(c, 50);
    bool split = gen_chance(c, 40) && hz1 - hz0 >= 7;
    for (int f = 0; f < c->floors; f++) {
        int gx0 = hx0, gx1 = hx1;
        int cage_rooms[8], ncage = 0;
        if (cages) {
            int sx0 = cage_left ? hx0 : hx1 - cw + 1, sx1 = cage_left ? hx0 + cw - 1 : hx1;
            if (cage_left)
                gx0 = sx1 + 1;
            else
                gx1 = sx0 - 1;
            int starts[8];
            int n = split_span(c, hz0, hz1, 3, 5, starts, 8);
            for (int k = 0; k < n; k++) {
                int z0 = starts[k], z1 = k + 1 < n ? starts[k + 1] - 1 : hz1;
                cage_rooms[ncage++] = mk(c, f, sx0, z0, sx1, z1, GR_ROOM);
            }
        }
        int hall, hall2 = 0;
        if (split) {
            int zs = gen_ri(c, hz0 + 3, hz1 - 3);
            hall = mk(c, f, gx0, hz0, gx1, zs - 1, GR_HALL);
            hall2 = mk(c, f, gx0, zs, gx1, hz1, GR_HALL);
            want_door(f, hall, hall2, false);
            want_door(f, hall2, hall, true);
        } else {
            hall = mk(c, f, gx0, hz0, gx1, hz1, GR_HALL);
        }
        int back = hall2 ? hall2 : hall;
        /* Loading bays: one long bay or several, divided by partitions. */
        int bstarts[4];
        int nb = split_span(c, hx0, hx1, NH_MAX(6, (hx1 - hx0 + 1) / gen_ri(c, 1, 3)), hx1 - hx0 + 1, bstarts, 4);
        int prev_bay = 0;
        for (int k = 0; k < nb; k++) {
            int x0 = bstarts[k], x1 = k + 1 < nb ? bstarts[k + 1] - 1 : hx1;
            int load = mk(c, f, x0, c->bz1 - ld + 1, x1, c->bz1, GR_ROOM);
            int above = c->L->cells[f][c->bz1 - ld][NH_CLAMP((x0 + x1) / 2, gx0, gx1)].room;
            want_door(f, above >= LV_ROOM_FIRST && c->gr[above].role == GR_HALL ? above : back, load, true);
            if (prev_bay)
                want_door(f, prev_bay, load, false);
            prev_bay = load;
        }
        for (int k = 0; k < ncage; k++)
            want_door(f, cage_rooms[k], c->L->cells[f][c->L->rooms[cage_rooms[k]].z0][cage_left ? gx0 : gx1].room, false);
        int corr = mk(c, f, corr_x0, corr_z0, corr_x1, corr_z1, GR_CORRIDOR);
        c->corridor[f] = corr;
        want_door(f, hall, corr, false);
        if (hall2)
            want_door(f, hall2, corr, false);
        /* Offices in the block, skipping the stairwell. */
        if (variant <= 1) {
            int zend = c->floors > 1 ? c->stz - 1 : oz1;
            int starts[16];
            int n = split_span(c, oz0, zend, 2, gen_ri(c, 3, 5), starts, 16);
            for (int k = 0; k < n; k++) {
                int z0 = starts[k], z1 = k + 1 < n ? starts[k + 1] - 1 : zend;
                int r = mk(c, f, ox0, z0, ox1, z1, GR_ROOM);
                want_door(f, r, corr, false);
            }
        } else {
            Strip st = { f, 0, ox0, ox1, oz0, oz1, corr_z0, corr, 1, 0, 3, 4, 0, 15, false };
            if (c->floors > 1) {
                st.skip_u0 = c->stx;
                st.skip_u1 = c->stx + 2;
            }
            fill_strip(c, &st);
        }
    }
}

/* ---- doors, windows -------------------------------------------------------- */

/* Shared boundary cells of rooms a and b on floor f: cell in a, side towards b. */
static int shared_edges(const Level *L, int f, int a, int b, int *xs, int *zs, int *ss, int max)
{
    const LvRoom *r = &L->rooms[a];
    int n = 0;
    for (int z = r->z0; z <= r->z1; z++)
        for (int x = r->x0; x <= r->x1; x++) {
            if (L->cells[f][z][x].room != a)
                continue;
            for (int s = 0; s < 4; s++) {
                int nx = x + k_dx[s], nz = z + k_dz[s];
                if (!lv_in_bounds(L, f, nx, nz) || L->cells[f][nz][nx].room != b)
                    continue;
                if ((L->cells[f][z][x].flags | L->cells[f][nz][nx].flags) & LV_C_HOLE)
                    continue;
                if (L->cells[f][z][x].stair || L->cells[f][nz][nx].stair)
                    continue;
                if (n < max) {
                    xs[n] = x;
                    zs[n] = z;
                    ss[n] = s;
                    n++;
                }
            }
        }
    return n;
}

static bool edge_has_door_near(const Level *L, int f, int x, int z, int s)
{
    /* Keep doors at least one edge apart along a wall. */
    int px = s == 0 || s == 2 ? 0 : 1, pz = s == 0 || s == 2 ? 1 : 0;
    for (int k = -1; k <= 1; k += 2) {
        const LvEdge *e = lv_edge_side_c(L, f, x + px * k, z + pz * k, s);
        if (e && (e->type == LV_E_DOOR || e->type == LV_E_ARCH))
            return true;
    }
    return false;
}

static int place_door(GenCtx *c, int f, int a, int b, bool arch)
{
    int xs[64], zs[64], ss[64];
    int n = shared_edges(c->L, f, a, b, xs, zs, ss, 64);
    if (!n)
        return -1;
    /* Avoid corners and neighbouring doors; prefer the middle third for arches. */
    int order[64];
    for (int i = 0; i < n; i++)
        order[i] = i;
    for (int i = n - 1; i > 0; i--) {
        int j = gen_ri(c, 0, i);
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    int pick = -1;
    for (int k = 0; k < n && pick < 0; k++) {
        int i = order[k];
        const LvEdge *e = lv_edge_side_c(c->L, f, xs[i], zs[i], ss[i]);
        if (e->type != LV_E_OPEN)
            continue;
        if (!edge_has_door_near(c->L, f, xs[i], zs[i], ss[i]))
            pick = i;
    }
    for (int k = 0; k < n && pick < 0; k++) {
        int i = order[k];
        if (lv_edge_side_c(c->L, f, xs[i], zs[i], ss[i])->type == LV_E_OPEN)
            pick = i;
    }
    if (pick < 0)
        return -1;
    if (arch) {
        lv_set_edge(c->L, f, xs[pick], zs[pick], ss[pick], LV_E_ARCH);
        return -2;
    }
    return lv_add_door(c->L, f, xs[pick], zs[pick], ss[pick], LV_DOOR_WOOD, LV_LOCK_NONE, 0, 0);
}

/* Wide openings between two corridors (or hall and loading bay): every
 * shared edge becomes an arch so they read as one space. */
static void open_between(GenCtx *c, int f, int a, int b)
{
    int xs[64], zs[64], ss[64];
    int n = shared_edges(c->L, f, a, b, xs, zs, ss, 64);
    for (int i = 0; i < n; i++)
        lv_set_edge(c->L, f, xs[i], zs[i], ss[i], LV_E_ARCH);
}

static void resolve_doors(GenCtx *c)
{
    for (int i = 0; i < s_nreq; i++) {
        DoorReq *q = &s_req[i];
        int ra = c->gr[q->a].role, rb = c->gr[q->b].role;
        bool wide = (ra == GR_CORRIDOR && rb == GR_CORRIDOR) ||
                    (c->type == BT_WAREHOUSE && q->arch && (ra == GR_HALL) != (rb == GR_HALL));
        if (wide)
            open_between(c, q->f, q->a, q->b);
        else
            place_door(c, q->f, q->a, q->b, q->arch);
    }
}

/* Union-find over rooms joined by doors/arches, used to patch islands. */
static int s_uf[LV_ROOMS_MAX];
static int uf_find(int a)
{
    while (s_uf[a] != a)
        a = s_uf[a] = s_uf[s_uf[a]];
    return a;
}

static void ensure_connected(GenCtx *c)
{
    Level *L = c->L;
    for (int i = 0; i < L->nrooms; i++)
        s_uf[i] = i;
    for (int f = 0; f < c->floors; f++)
        for (int z = 0; z < L->d; z++)
            for (int x = 0; x < L->w; x++)
                for (int s = 0; s < 2; s++) {
                    /* Before lv_finalize, edges between different rooms are
                     * still OPEN but will become walls: only doors and arches
                     * connect. */
                    const LvEdge *e = lv_edge_side_c(L, f, x, z, s);
                    if (!e || (e->type != LV_E_DOOR && e->type != LV_E_ARCH))
                        continue;
                    int a = L->cells[f][z][x].room, b = lv_neighbor_room(L, f, x, z, s);
                    if (a >= LV_ROOM_FIRST && b >= LV_ROOM_FIRST && a != b)
                        s_uf[uf_find(a)] = uf_find(b);
                }
    /* Stairwells join floors. */
    for (int f = 0; f + 1 < c->floors; f++)
        if (c->stair_room[f] && c->stair_room[f + 1])
            s_uf[uf_find(c->stair_room[f])] = uf_find(c->stair_room[f + 1]);
    int root = uf_find(c->corridor[0] ? c->corridor[0] : LV_ROOM_FIRST);
    for (int pass = 0; pass < 4; pass++) {
        bool changed = false;
        for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
            if (c->gr[r].role == GR_ROOF || L->rooms[r].floor >= c->floors || uf_find(r) == uf_find(root))
                continue;
            /* Door to any neighbour already connected to the root. */
            const LvRoom *rr = &L->rooms[r];
            int f = rr->floor;
            for (int z = rr->z0; z <= rr->z1 && uf_find(r) != uf_find(root); z++)
                for (int x = rr->x0; x <= rr->x1 && uf_find(r) != uf_find(root); x++)
                    for (int s = 0; s < 4; s++) {
                        int nb = lv_neighbor_room(L, f, x, z, s);
                        if (nb < LV_ROOM_FIRST || nb == r || uf_find(nb) != uf_find(root) ||
                            c->gr[nb].role == GR_ROOF)
                            continue;
                        if (place_door(c, f, r, nb, false) != -1) {
                            s_uf[uf_find(r)] = uf_find(root);
                            changed = true;
                        }
                        break;
                    }
        }
        if (!changed)
            break;
    }
}

static bool cell_is_exterior_edge(const Level *L, int f, int x, int z, int s, int floors)
{
    int nx = x + k_dx[s], nz = z + k_dz[s];
    if (!lv_in_bounds(L, f, nx, nz))
        return true;
    int nb = L->cells[f][nz][nx].room;
    (void)floors;
    return f == 0 ? nb == LV_ROOM_OUTSIDE : nb == LV_ROOM_VOID;
}

static void add_windows(GenCtx *c)
{
    Level *L = c->L;
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        const LvRoom *rr = &L->rooms[r];
        if (rr->floor >= c->floors)
            continue;
        int f = rr->floor;
        int pct;
        switch (rr->func) {
        case RF_VAULT: case RF_SERVER: case RF_PANIC_ROOM: case RF_WINE_CELLAR:
            pct = 0;
            break;
        case RF_BATHROOM: case RF_STORAGE: case RF_UTILITY: case RF_ARCHIVE: case RF_LOCKER_ROOM:
            pct = 15;
            break;
        case RF_STAIRWELL:
            pct = 30;
            break;
        case RF_WAREHOUSE: case RF_LOADING:
            pct = 18;
            break;
        case RF_EXHIBIT: case RF_GALLERY:
            pct = 25;
            break;
        default:
            pct = f == 0 && c->type == BT_BANK ? 30 : 55;
            break;
        }
        if (!pct)
            continue;
        for (int z = rr->z0; z <= rr->z1; z++)
            for (int x = rr->x0; x <= rr->x1; x++) {
                if (L->cells[f][z][x].room != r || L->cells[f][z][x].stair)
                    continue;
                for (int s = 0; s < 4; s++) {
                    if (!cell_is_exterior_edge(L, f, x, z, s, c->floors))
                        continue;
                    /* Still OPEN here: lv_finalize turns the rest into walls. */
                    const LvEdge *e = lv_edge_side_c(L, f, x, z, s);
                    if (!e || (e->type != LV_E_WALL && e->type != LV_E_OPEN))
                        continue;
                    if (gen_chance(c, pct))
                        lv_add_window(L, f, x, z, s);
                }
            }
    }
}

/* Exterior door on a room's outside wall, on the given side (or any). */
static int exterior_door(GenCtx *c, int room, int want_side, int kind)
{
    Level *L = c->L;
    const LvRoom *r = &L->rooms[room];
    int xs[64], zs[64], ss[64], n = 0;
    for (int z = r->z0; z <= r->z1; z++)
        for (int x = r->x0; x <= r->x1; x++)
            for (int s = 0; s < 4; s++) {
                if (want_side >= 0 && s != want_side)
                    continue;
                if (L->cells[0][z][x].room != room || L->cells[0][z][x].stair)
                    continue;
                if (!cell_is_exterior_edge(L, 0, x, z, s, c->floors))
                    continue;
                const LvEdge *e = lv_edge_side_c(L, 0, x, z, s);
                if (!e || (e->type != LV_E_WALL && e->type != LV_E_WINDOW && e->type != LV_E_OPEN))
                    continue;
                if (edge_has_door_near(L, 0, x, z, s))
                    continue;
                if (n < 64) {
                    xs[n] = x;
                    zs[n] = z;
                    ss[n] = s;
                    n++;
                }
            }
    if (!n)
        return -1;
    /* Front doors go near the middle of the wall. */
    int i = n > 2 ? gen_ri(c, n / 4, n - 1 - n / 4) : gen_ri(c, 0, n - 1);
    LvEdge *e = lv_edge_side(L, 0, xs[i], zs[i], ss[i]);
    e->type = LV_E_OPEN;
    e->ref = 0;
    int d = lv_add_door(L, 0, xs[i], zs[i], ss[i], kind, LV_LOCK_PICK, 1, 0);
    if (d >= 0)
        L->doors[d].flags |= LV_DOOR_F_EXTERIOR;
    return d;
}

/* ---- functions ------------------------------------------------------------- */

typedef struct Need {
    uint8_t func;
    uint8_t where;   /* W_* */
    uint8_t min_area, max_area;
    uint8_t count[BS_COUNT];
    uint8_t secure;
} Need;

enum { W_GROUND = 1, W_UPPER = 2, W_TOP = 4, W_ANY = 7, W_BACK = 8, W_FRONT = 16, W_EXT = 32, W_BIG = 64 };

static const Need k_office[] = {
    { RF_SECURITY, W_GROUND, 4, 16, { 1, 1, 1 }, 1 },
    { RF_SERVER, W_ANY | W_BACK, 6, 24, { 1, 1, 2 }, 2 },
    { RF_EXEC_OFFICE, W_TOP | W_EXT, 6, 30, { 1, 2, 2 }, 2 },
    { RF_OPEN_OFFICE, W_UPPER | W_BIG, 16, 80, { 1, 2, 3 }, 0 },
    { RF_MEETING, W_ANY, 6, 24, { 1, 2, 3 }, 0 },
    { RF_BREAKROOM, W_ANY, 4, 20, { 1, 1, 2 }, 0 },
    { RF_BATHROOM, W_ANY, 3, 12, { 1, 2, 3 }, 0 },
    { RF_ARCHIVE, W_ANY | W_BACK, 4, 20, { 0, 1, 1 }, 1 },
    { RF_UTILITY, W_GROUND | W_BACK, 3, 14, { 1, 1, 1 }, 0 },
    { RF_STORAGE, W_ANY | W_BACK, 3, 14, { 1, 1, 2 }, 0 },
    { RF_LOCKER_ROOM, W_GROUND, 4, 16, { 0, 1, 1 }, 0 },
};
static const Need k_bank[] = {
    { RF_VAULT, W_GROUND | W_BACK, 6, 24, { 1, 1, 1 }, 3 },
    { RF_TELLER, W_GROUND, 6, 30, { 1, 1, 1 }, 1 },
    { RF_SECURITY, W_GROUND, 4, 16, { 1, 1, 1 }, 1 },
    { RF_EXEC_OFFICE, W_ANY | W_EXT, 6, 30, { 1, 1, 2 }, 2 },
    { RF_ARCHIVE, W_ANY | W_BACK, 4, 24, { 1, 1, 1 }, 2 },
    { RF_SERVER, W_UPPER | W_BACK, 4, 20, { 0, 1, 1 }, 2 },
    { RF_LOCKER_ROOM, W_GROUND, 4, 16, { 1, 1, 1 }, 0 },
    { RF_BREAKROOM, W_ANY, 4, 20, { 1, 1, 1 }, 0 },
    { RF_BATHROOM, W_ANY, 3, 12, { 1, 1, 2 }, 0 },
    { RF_MEETING, W_UPPER, 6, 24, { 1, 1, 2 }, 0 },
    { RF_UTILITY, W_GROUND | W_BACK, 3, 14, { 1, 1, 1 }, 0 },
    { RF_STORAGE, W_ANY, 3, 14, { 0, 1, 1 }, 0 },
};
static const Need k_mansion[] = {
    { RF_LIVING, W_GROUND | W_BIG, 12, 40, { 1, 1, 1 }, 0 },
    { RF_DINING, W_GROUND, 9, 36, { 1, 1, 1 }, 0 },
    { RF_KITCHEN, W_GROUND | W_EXT, 6, 30, { 1, 1, 1 }, 0 },
    { RF_LIBRARY, W_ANY, 9, 36, { 1, 1, 1 }, 0 },
    { RF_STUDY, W_ANY | W_BACK, 6, 24, { 1, 1, 1 }, 2 },
    { RF_GARAGE, W_GROUND | W_EXT, 9, 36, { 0, 1, 1 }, 0 },
    { RF_WINE_CELLAR, W_GROUND | W_BACK, 6, 24, { 0, 1, 1 }, 1 },
    { RF_BEDROOM, W_UPPER, 9, 36, { 2, 3, 4 }, 0 },
    { RF_BATHROOM, W_ANY, 4, 16, { 1, 2, 3 }, 0 },
    { RF_GALLERY, W_UPPER, 9, 40, { 0, 1, 1 }, 1 },
    { RF_LOUNGE, W_UPPER, 9, 36, { 1, 1, 1 }, 0 },
    { RF_PANIC_ROOM, W_UPPER | W_BACK, 4, 16, { 0, 0, 1 }, 3 },
    { RF_SECURITY, W_GROUND, 4, 16, { 0, 1, 1 }, 1 },
    { RF_UTILITY, W_GROUND, 3, 16, { 1, 1, 1 }, 0 },
};
static const Need k_museum[] = {
    { RF_EXHIBIT, W_ANY | W_BACK | W_BIG, 12, 60, { 1, 1, 2 }, 3 },
    { RF_SECURITY, W_GROUND, 4, 16, { 1, 1, 1 }, 1 },
    { RF_GALLERY, W_ANY | W_BIG, 12, 60, { 2, 3, 5 }, 0 },
    { RF_SHOP, W_GROUND | W_FRONT, 6, 24, { 1, 1, 1 }, 0 },
    { RF_LAB, W_ANY | W_BACK, 6, 30, { 0, 1, 1 }, 1 },
    { RF_STORAGE, W_ANY | W_BACK, 4, 24, { 1, 1, 2 }, 1 },
    { RF_OFFICE, W_ANY, 4, 20, { 1, 1, 2 }, 0 },
    { RF_BATHROOM, W_GROUND, 3, 12, { 1, 1, 1 }, 0 },
    { RF_UTILITY, W_GROUND | W_BACK, 3, 14, { 1, 1, 1 }, 0 },
    { RF_ARCHIVE, W_UPPER | W_BACK, 4, 20, { 0, 1, 1 }, 1 },
};
static const Need k_warehouse[] = {
    { RF_SECURITY, W_GROUND, 4, 16, { 1, 1, 1 }, 1 },
    { RF_STORAGE, W_ANY, 4, 20, { 1, 1, 2 }, 2 },
    { RF_OFFICE, W_ANY, 4, 20, { 1, 2, 2 }, 0 },
    { RF_BREAKROOM, W_GROUND, 4, 20, { 1, 1, 1 }, 0 },
    { RF_LOCKER_ROOM, W_GROUND, 4, 16, { 0, 1, 1 }, 0 },
    { RF_BATHROOM, W_GROUND, 3, 12, { 1, 1, 1 }, 0 },
    { RF_EXEC_OFFICE, W_ANY, 4, 24, { 0, 1, 1 }, 2 },
    { RF_UTILITY, W_GROUND, 3, 14, { 1, 1, 1 }, 0 },
    { RF_ARCHIVE, W_UPPER, 4, 20, { 0, 0, 1 }, 1 },
    { RF_SERVER, W_ANY, 4, 16, { 0, 0, 1 }, 2 },
};

static int filler_func(GenCtx *c, const LvRoom *r)
{
    int a = gen_area(r);
    switch (c->type) {
    case BT_OFFICE:
        if (a >= 16 && gen_chance(c, 45))
            return RF_OPEN_OFFICE;
        return gen_chance(c, 18) ? RF_MEETING : (gen_chance(c, 12) ? RF_STORAGE : RF_OFFICE);
    case BT_BANK:
        return r->floor == 0 ? (a >= 9 ? RF_OFFICE : RF_STORAGE) : (a >= 16 ? RF_OPEN_OFFICE : RF_OFFICE);
    case BT_MANSION:
        if (r->floor == 0)
            return a >= 12 ? (gen_chance(c, 50) ? RF_LOUNGE : RF_LIVING) : (gen_chance(c, 50) ? RF_STUDY : RF_BATHROOM);
        return a >= 9 ? RF_BEDROOM : RF_BATHROOM;
    case BT_MUSEUM:
        return a >= 9 ? RF_GALLERY : RF_STORAGE;
    default:
        return a >= 9 ? RF_OFFICE : RF_STORAGE;
    }
}

static int score_room(GenCtx *c, int r, const Need *n)
{
    const LvRoom *rr = &c->L->rooms[r];
    const GenRoom *g = &c->gr[r];
    int a = gen_area(rr);
    if (a < n->min_area || a > n->max_area)
        return -1;
    int top = c->floors - 1;
    bool ok_floor = ((n->where & W_GROUND) && rr->floor == 0) || ((n->where & W_UPPER) && rr->floor > 0) ||
                    ((n->where & W_TOP) && rr->floor == top);
    if (!ok_floor)
        return -1;
    int s = 100 + gen_ri(c, 0, 30);
    if (n->where & W_BACK)
        s += g->role == GR_BACK ? 60 : (g->front ? -40 : 10);
    if (n->where & W_FRONT)
        s += g->front ? 60 : -30;
    if (n->where & W_EXT)
        s += g->exterior ? 30 : -20;
    if (n->where & W_BIG)
        s += a;
    if ((n->where & W_TOP) && rr->floor == top)
        s += 20;
    return s;
}

static void assign_functions(GenCtx *c)
{
    Level *L = c->L;
    const Need *needs;
    int nneeds;
    switch (c->type) {
    case BT_OFFICE: needs = k_office; nneeds = NH_ARRAY_LEN(k_office); break;
    case BT_BANK: needs = k_bank; nneeds = NH_ARRAY_LEN(k_bank); break;
    case BT_MANSION: needs = k_mansion; nneeds = NH_ARRAY_LEN(k_mansion); break;
    case BT_MUSEUM: needs = k_museum; nneeds = NH_ARRAY_LEN(k_museum); break;
    default: needs = k_warehouse; nneeds = NH_ARRAY_LEN(k_warehouse); break;
    }
    /* Exterior / facade flags. */
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        const LvRoom *rr = &L->rooms[r];
        if (rr->floor >= c->floors)
            continue;
        for (int z = rr->z0; z <= rr->z1; z++)
            for (int x = rr->x0; x <= rr->x1; x++)
                for (int s = 0; s < 4; s++)
                    if (cell_is_exterior_edge(L, rr->floor, x, z, s, c->floors)) {
                        c->gr[r].exterior |= (uint8_t)(1u << s);
                        if (s == 3 && z == c->bz0)
                            c->gr[r].front = 1;
                    }
    }
    /* Fixed roles first. */
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        int role = c->gr[r].role;
        int f = L->rooms[r].floor;
        if (role == GR_STAIR)
            nh_room_apply_style(L, r, RF_STAIRWELL);
        else if (role == GR_CORRIDOR)
            nh_room_apply_style(L, r, RF_CORRIDOR);
        else if (role == GR_HALL) {
            if (c->type == BT_WAREHOUSE)
                nh_room_apply_style(L, r, f == 0 ? RF_WAREHOUSE : RF_STORAGE);
            else
                nh_room_apply_style(L, r, f == 0 ? RF_LOBBY : RF_GALLERY);
        }
    }
    if (c->type == BT_WAREHOUSE) {
        for (int r = LV_ROOM_FIRST; r < L->nrooms; r++)
            if (c->gr[r].role == GR_ROOM && gen_rw(&L->rooms[r]) >= 6 && L->rooms[r].z1 == c->bz1)
                nh_room_apply_style(L, r, L->rooms[r].floor == 0 ? RF_LOADING : RF_STORAGE);
    }
    /* The entrance: the ground-floor front room nearest the middle (or the
     * hall for mansions and warehouses). */
    c->entrance = 0;
    if (c->type == BT_MANSION)
        c->entrance = c->corridor[0];
    else if (c->type == BT_WAREHOUSE) {
        for (int r = LV_ROOM_FIRST; r < L->nrooms; r++)
            if (c->gr[r].role == GR_HALL && L->rooms[r].floor == 0 && c->gr[r].front)
                c->entrance = r;
    }
    if (!c->entrance) {
        int best = -1, mid = (c->bx0 + c->bx1);
        for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
            const LvRoom *rr = &L->rooms[r];
            if (rr->floor != 0 || !c->gr[r].front || c->gr[r].role != GR_ROOM || gen_area(rr) < 6)
                continue;
            int s = gen_area(rr) * 4 - abs(rr->x0 + rr->x1 - mid) * 3 + gen_ri(c, 0, 6);
            if (s > best) {
                best = s;
                c->entrance = r;
            }
        }
        if (!c->entrance)
            c->entrance = c->corridor[0];
        int func = c->type == BT_BANK ? RF_BANK_HALL : RF_LOBBY;
        if (c->entrance && c->gr[c->entrance].role == GR_ROOM)
            nh_room_apply_style(L, c->entrance, func);
    }
    /* Needs, in order. */
    for (int i = 0; i < nneeds; i++) {
        const Need *n = &needs[i];
        int count = n->count[c->size];
        for (int k = 0; k < count; k++) {
            int best = -1, bs = -1;
            for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
                if (L->rooms[r].func != RF_NONE || L->rooms[r].floor >= c->floors || r == c->entrance)
                    continue;
                if (c->gr[r].role != GR_ROOM && c->gr[r].role != GR_BACK)
                    continue;
                int s = score_room(c, r, n);
                /* Teller areas sit next to the banking hall. */
                if (n->func == RF_TELLER && s >= 0) {
                    bool adj = false;
                    for (int q = 0; q < s_nreq; q++)
                        adj |= (s_req[q].a == r && s_req[q].b == c->entrance) ||
                               (s_req[q].b == r && s_req[q].a == c->entrance);
                    const LvRoom *rr = &L->rooms[r], *e = &L->rooms[c->entrance];
                    adj |= (rr->x1 + 1 == e->x0 || e->x1 + 1 == rr->x0) && rr->z0 <= e->z1 && e->z0 <= rr->z1;
                    s = adj ? s + 80 : s - 50;
                }
                if (s > bs) {
                    bs = s;
                    best = r;
                }
            }
            if (best < 0)
                break;
            nh_room_apply_style(L, best, n->func);
            c->gr[best].secure = n->secure;
        }
    }
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++)
        if (L->rooms[r].func == RF_NONE && L->rooms[r].floor < c->floors)
            nh_room_apply_style(L, r, filler_func(c, &L->rooms[r]));
    /* Security flags for the renderer/AI. */
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        if (c->gr[r].secure >= 2)
            L->rooms[r].flags |= LV_R_SECURE;
        if (c->gr[r].role == GR_CORRIDOR || c->gr[r].role == GR_HALL)
            L->rooms[r].flags |= LV_R_CORRIDOR;
        if (c->gr[r].role == GR_STAIR)
            L->rooms[r].flags |= LV_R_STAIRWELL;
    }
}

/* ---- vents and the roof hatch ---------------------------------------------- */

static bool vent_cell_ok(const GenCtx *c, int f, int x, int z)
{
    const LvCell *cell = lv_cell(c->L, f, x, z);
    if (!cell || cell->room < LV_ROOM_FIRST || cell->stair || (cell->flags & (LV_C_HOLE | LV_C_VENT)))
        return false;
    if (c->gr[cell->room].role == GR_STAIR)
        return false;
    /* The cell above must not be a hole either (stair void). */
    const LvCell *up = lv_cell(c->L, f + 1, x, z);
    return !(up && (up->flags & LV_C_HOLE));
}

/* Straight duct from a public room into a secure one, over the walls. */
static int add_vents(GenCtx *c)
{
    Level *L = c->L;
    int made = 0, want = 1 + c->size;
    for (int tries = 0; tries < 200 && made < want; tries++) {
        int r = gen_ri(c, LV_ROOM_FIRST, L->nrooms - 1);
        if (L->rooms[r].floor >= c->floors || c->gr[r].secure < 1)
            continue;
        const LvRoom *t = &L->rooms[r];
        int f = t->floor;
        int x = gen_ri(c, t->x0, t->x1), z = gen_ri(c, t->z0, t->z1);
        int s = gen_ri(c, 0, 3);
        if (!vent_cell_ok(c, f, x, z))
            continue;
        /* Walk out of the room until we reach an unsecured, ordinary room. */
        int len = 0, cx = x, cz = z, end_room = 0;
        bool ok = true;
        while (len < 12) {
            cx += k_dx[s];
            cz += k_dz[s];
            len++;
            if (!vent_cell_ok(c, f, cx, cz)) {
                ok = false;
                break;
            }
            int rr = L->cells[f][cz][cx].room;
            if (rr != r && c->gr[rr].secure == 0 && c->gr[rr].role != GR_STAIR && len >= 3) {
                end_room = rr;
                break;
            }
        }
        if (!ok || !end_room)
            continue;
        for (int k = 0; k <= len; k++)
            L->cells[f][z + k_dz[s] * k][x + k_dx[s] * k].flags |= LV_C_VENT;
        L->cells[f][z][x].flags |= LV_C_GRATE;
        L->cells[f][cz][cx].flags |= LV_C_GRATE;
        made++;
    }
    return made;
}

static void add_hatch(GenCtx *c)
{
    int top = c->floors - 1;
    int x, z;
    if (c->floors > 1) {
        /* Over the stairwell walkway at the far end. */
        st_cell(c, 1, 4, &x, &z);
    } else {
        /* Single storey: a storage or utility room, else the corridor. */
        int best = c->corridor[0];
        for (int r = LV_ROOM_FIRST; r < c->L->nrooms; r++)
            if (c->L->rooms[r].func == RF_UTILITY || c->L->rooms[r].func == RF_STORAGE)
                best = r;
        const LvRoom *rr = &c->L->rooms[best];
        x = (rr->x0 + rr->x1) / 2;
        z = (rr->z0 + rr->z1) / 2;
    }
    c->L->cells[top][z][x].flags |= LV_C_HATCH;
    NhVec3 p = lv_cell_center(top, x, z);
    p.y += LV_CEIL;
    gen_entity(c, ENT_HATCH, c->L->cells[top][z][x].room, -1, p, 0.0f);
}

/* ---- lights ------------------------------------------------------------------ */

static NhColor light_color(GenCtx *c, int func)
{
    switch (func) {
    case RF_SERVER: return nh_rgba(120, 160, 230, 255);
    case RF_VAULT: case RF_PANIC_ROOM: return nh_rgba(200, 210, 225, 255);
    case RF_OFFICE: case RF_OPEN_OFFICE: case RF_CORRIDOR: case RF_STORAGE: case RF_ARCHIVE:
    case RF_WAREHOUSE: case RF_LOADING: case RF_SECURITY: case RF_UTILITY: case RF_LOCKER_ROOM:
    case RF_BATHROOM: case RF_LAB:
        return nh_rgba(170, 182, 205, 255);
    default:
        return c->type == BT_MANSION ? nh_rgba(230, 190, 140, 255) : nh_rgba(215, 196, 160, 255);
    }
}

static void add_lights(GenCtx *c)
{
    Level *L = c->L;
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        LvRoom *rr = &L->rooms[r];
        if (rr->floor >= c->floors)
            continue;
        int f = rr->floor;
        int w = gen_rw(rr), d = gen_rd(rr);
        int nx = (w + 3) / 4, nz = (d + 3) / 4;
        if (nx * nz > 6)
            nz = NH_MAX(1, 6 / nx);
        int lit_pct = nh_room_styles[rr->func].lit_chance;
        if (c->security >= 3)
            lit_pct = lit_pct * 3 / 4;
        bool on = gen_chance(c, lit_pct);
        if (on)
            rr->flags |= LV_R_LIT;
        c->gr[r].lit = on;
        int circuit = 1 + f * 2 + ((rr->x0 + rr->x1) / 2 > (c->bx0 + c->bx1) / 2 ? 1 : 0);
        rr->circuit = (uint8_t)circuit;
        NhColor col = light_color(c, rr->func);
        float radius = 6.0f + 1.2f * (float)NH_MIN(NH_MAX(w, d), 6);
        for (int iz = 0; iz < nz; iz++)
            for (int ix = 0; ix < nx; ix++) {
                float x = (float)rr->x0 * LV_CELL + ((float)ix + 0.5f) * (float)w * LV_CELL / (float)nx;
                float z = (float)rr->z0 * LV_CELL + ((float)iz + 0.5f) * (float)d * LV_CELL / (float)nz;
                /* Snap to a cell centre so the lamp never sits over a stair hole. */
                int cx = lv_cell_of(x), cz = lv_cell_of(z);
                const LvCell *above = lv_cell(L, f + 1, cx, cz);
                if (above && (above->flags & LV_C_HOLE))
                    continue;
                if (L->cells[f][cz][cx].room != r)
                    continue;
                int li = lv_add_light(L, nh_v3(x, lv_floor_y(f) + LV_CEIL - 0.05f, z), col, radius, LV_LIGHT_CEILING,
                                      r, circuit);
                if (li < 0)
                    continue;
                L->lights[li].on = on;
                if (rr->func == RF_STAIRWELL) {
                    L->lights[li].emergency = 1;
                    L->lights[li].on = 1;
                }
                int p = lv_add_prop(L, PROP_CEILING_LAMP, f, x, lv_floor_y(f) + LV_CEIL, z, 0,
                                    nh_prop_info[PROP_CEILING_LAMP].half, nh_prop_info[PROP_CEILING_LAMP].flags);
                (void)p;
            }
    }
}

/* Moonlight through some windows and street lights along the road. */
static void add_exterior_lights(GenCtx *c)
{
    Level *L = c->L;
    int moons = 0, maxm = 6 + c->size * 3;
    for (int f = 0; f < c->floors; f++)
        for (int z = 0; z < L->d; z++)
            for (int x = 0; x < L->w; x++)
                for (int s = 0; s < 4; s++) {
                    const LvEdge *e = lv_edge_side_c(L, f, x, z, s);
                    if (!e || e->type != LV_E_WINDOW || L->cells[f][z][x].room < LV_ROOM_FIRST)
                        continue;
                    if (!cell_is_exterior_edge(L, f, x, z, s, c->floors) || moons >= maxm || !gen_chance(c, 35))
                        continue;
                    NhVec3 p = lv_cell_center(f, x, z);
                    p.x += (float)k_dx[s] * 0.9f;
                    p.z += (float)k_dz[s] * 0.9f;
                    p.y = lv_floor_y(f) + 2.4f;
                    int li = lv_add_light(L, p, nh_rgba(70, 90, 140, 255), 7.0f, LV_LIGHT_WINDOW,
                                          L->cells[f][z][x].room, 15);
                    if (li >= 0) {
                        L->lights[li].dir = nh_v3_norm(nh_v3(-(float)k_dx[s], -0.6f, -(float)k_dz[s]));
                        L->lights[li].cone = 0.5f; /* cos 60 */
                        L->lights[li].emergency = 1;
                        L->lights[li].on = 1;
                        moons++;
                    }
                }
    /* Street lights in front of the building. */
    float zs = (float)(c->bz0 - 2) * LV_CELL + 1.4f;
    for (float x = 6.0f; x < (float)L->w * LV_CELL - 4.0f; x += 13.0f) {
        int sl = lv_add_light(L, nh_v3(x, 4.8f, zs + 0.8f), nh_rgba(230, 170, 100, 255), 16.0f, LV_LIGHT_STREET,
                              LV_ROOM_OUTSIDE, 15);
        if (sl >= 0) {
            L->lights[sl].emergency = 1;
            L->lights[sl].on = 1;
        }
        lv_add_prop(L, PROP_STREETLIGHT, 0, x, 0.0f, zs, NH_PI, nh_prop_info[PROP_STREETLIGHT].half,
                    nh_prop_info[PROP_STREETLIGHT].flags);
    }
    /* A lamp over the back yard. */
    float zb = (float)(c->bz1 + 2) * LV_CELL;
    int bl = lv_add_light(L, nh_v3((float)(c->bx0 + c->bx1) * 0.5f * LV_CELL + 1.0f, 4.0f, zb),
                          nh_rgba(220, 160, 90, 255), 12.0f, LV_LIGHT_STREET, LV_ROOM_OUTSIDE, 15);
    if (bl >= 0) {
        L->lights[bl].emergency = 1;
        L->lights[bl].on = 1;
    }
}

/* ---- main ---------------------------------------------------------------------- */

static bool generate_once(GenCtx *c, uint32_t attempt)
{
    const NhGenParams *p = c->gp;
    Level *L = c->L;
    nh_rng_seed(&c->rng, ((uint64_t)p->seed << 8) ^ (uint64_t)(p->type * 7 + p->size), 0x9E1u + attempt);
    c->type = p->type % BT_COUNT;
    c->size = p->size % BS_COUNT;
    c->security = NH_MIN(p->security, 4);
    c->floors = k_floors[c->type][c->size];
    int W = k_width[c->type][c->size] + gen_ri(c, -2, 2);
    int D = k_depth[c->type][c->size] + gen_ri(c, 0, 2);
    int mw = W + 2 * MARGIN, md = D + MARGIN_FRONT + MARGIN;
    lv_init(L, mw, md, c->floors, p->seed);
    L->rooms[LV_ROOM_OUTSIDE].func = RF_OUTSIDE;
    L->rooms[LV_ROOM_OUTSIDE].floor_mat = MAT_ASPHALT;
    L->rooms[LV_ROOM_OUTSIDE].wall_mat = c->type == BT_MANSION || c->type == BT_MUSEUM ? MAT_STUCCO : MAT_BRICK;
    c->bx0 = MARGIN;
    c->bx1 = MARGIN + W - 1;
    c->bz0 = MARGIN_FRONT;
    c->bz1 = MARGIN_FRONT + D - 1;
    memset(c->stair_room, 0, sizeof(c->stair_room));
    memset(c->corridor, 0, sizeof(c->corridor));
    memset(c->gr, 0, sizeof(c->gr));
    memset(c->occ, 0, sizeof(c->occ));
    c->gr[LV_ROOM_OUTSIDE].role = GR_OUTSIDE;
    s_nreq = 0;
    NhPlan *P = c->P;
    memset(P, 0, sizeof(*P));
    P->params = *p;
    P->objective = -1;
    P->security_circuit = 9;

    switch (c->type) {
    case BT_OFFICE:
        layout_spine(c, c->size == BS_LARGE ? 2 : 1, 2, 4, 30, 20, false, c->size == BS_LARGE);
        break;
    case BT_BANK:
        layout_spine(c, 1, 3, 5, 55, 15, false, c->size == BS_LARGE);
        break;
    case BT_MUSEUM:
        layout_spine(c, c->size == BS_SMALL ? 1 : 2, 4, 7, 25, 70, true, c->size == BS_LARGE);
        break;
    case BT_MANSION:
        layout_mansion(c);
        break;
    default:
        layout_warehouse(c);
        break;
    }
    if (c->floors > 1)
        stair_doors(c);
    /* Roof over the footprint. */
    c->roof_room = mk(c, c->floors, c->bx0, c->bz0, c->bx1, c->bz1, GR_ROOF);
    L->rooms[c->roof_room].flags = LV_R_OUTDOOR | LV_R_ROOF;
    nh_room_apply_style(L, c->roof_room, RF_ROOF);
    L->rooms[c->roof_room].floor_mat = MAT_ROOF;
    for (int z = c->bz0; z <= c->bz1; z++)
        for (int x = c->bx0; x <= c->bx1; x++)
            L->cells[c->floors][z][x].flags |= LV_C_NOCEIL;
    if (c->floors > 1)
        add_stairs(c);
    resolve_doors(c);
    ensure_connected(c);
    assign_functions(c);
    /* Exterior doors: the front entrance, a side or back door, and loading
     * shutters / a garage door where the building has them. */
    c->front_door = exterior_door(c, c->entrance, 3, c->type == BT_WAREHOUSE ? LV_DOOR_METAL : LV_DOOR_GLASS);
    if (c->front_door < 0)
        c->front_door = exterior_door(c, c->entrance, -1, LV_DOOR_WOOD);
    if (c->front_door < 0)
        return false;
    int side_room = c->corridor[0];
    if (c->type == BT_MANSION) {
        for (int r = LV_ROOM_FIRST; r < L->nrooms; r++)
            if (L->rooms[r].func == RF_KITCHEN)
                side_room = r;
    }
    int sd = exterior_door(c, side_room, -1, LV_DOOR_METAL);
    if (sd >= 0) {
        L->doors[sd].lock_level = 2;
    }
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        int fn = L->rooms[r].func;
        if ((fn == RF_LOADING || fn == RF_GARAGE) && L->rooms[r].floor == 0) {
            int n = fn == RF_LOADING ? 2 : 1;
            for (int k = 0; k < n; k++) {
                int d = exterior_door(c, r, fn == RF_LOADING ? 1 : -1, LV_DOOR_SHUTTER);
                if (d >= 0)
                    L->doors[d].lock_level = 2;
            }
        }
    }
    add_windows(c);
    lv_finalize(L);
    add_vents(c);
    add_lights(c);
    add_exterior_lights(c);
    gen_room_graph_depths(c);
    gen_choose_setpieces(c);
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++)
        gen_dress_room(c, r);
    gen_dress_outside(c);
    add_hatch(c);
    gen_security(c);
    gen_guards(c);
    lv_index_props(L);
    P->fingerprint = nh_gen_fingerprint(L);
    return true;
}

bool nh_gen_building(Level *L, const NhGenParams *p, NhPlan *out)
{
    GenCtx *c = &s_ctx;
    c->L = L;
    c->P = out;
    c->gp = p;
    char why[96];
    for (uint32_t attempt = 0; attempt < 64; attempt++) {
        if (!generate_once(c, attempt))
            continue;
        out->attempts = (int)attempt + 1;
        bool repeat = false;
        for (int i = 0; i < p->navoid; i++)
            repeat |= p->avoid_layouts[i] == out->fingerprint;
        if (repeat)
            continue;
        if (nh_gen_validate(L, out, why, sizeof(why)))
            return true;
    }
    return false;
}

/* ---- validation ------------------------------------------------------------------ */

/* Fixpoint search over rooms: start outside, cross edges the keys collected
 * so far allow, pick up keys in every reached room, repeat. */
void gen_reach(const Level *L, const NhPlan *P, uint8_t *reach)
{
    uint32_t cards = 0, codes = 0, fuses = 0;
    memset(reach, 0, LV_ROOMS_MAX);
    reach[LV_ROOM_OUTSIDE] = 1;
    bool any = true;
    int guard = 0;
    while (any && guard++ < 64) {
        any = false;
        /* Keys in reached rooms. */
        for (int i = 0; i < P->nent; i++) {
            const NhEntity *e = &P->ent[i];
            if (!reach[e->room])
                continue;
            if (e->kind == ENT_KEYCARD)
                cards |= 1u << (e->key_id & 31);
            else if (e->kind == ENT_CODE)
                codes |= 1u << (e->key_id & 31);
            else if (e->kind == ENT_FUSE_BOX)
                fuses |= 1u << (e->key_id & 15);
        }
        /* Edges */
        for (int f = 0; f <= L->floors; f++)
            for (int z = 0; z < L->d; z++)
                for (int x = 0; x < L->w; x++)
                    for (int s = 0; s < 2; s++) {
                        const LvEdge *e = lv_edge_side_c(L, f, x, z, s);
                        if (!e)
                            continue;
                        int a = L->cells[f][z][x].room, b = lv_neighbor_room(L, f, x, z, s);
                        if (!a || !b || a == b || reach[a] == reach[b])
                            continue;
                        bool pass = false;
                        if (e->type == LV_E_ARCH || e->type == LV_E_OPEN)
                            pass = true;
                        else if (e->type == LV_E_DOOR && e->ref) {
                            const LvDoor *d = &L->doors[e->ref - 1];
                            switch (d->lock) {
                            case LV_LOCK_NONE: case LV_LOCK_PICK: pass = true; break;
                            case LV_LOCK_KEYCARD: pass = (cards >> (d->key_id & 31)) & 1u; break;
                            case LV_LOCK_CODE: pass = (codes >> (d->key_id & 31)) & 1u; break;
                            case LV_LOCK_POWERED: pass = (fuses >> (d->key_id & 15)) & 1u; break;
                            case LV_LOCK_VAULT:
                                pass = ((cards >> (d->key_id & 31)) & 1u) && ((codes >> (d->key_id & 31)) & 1u);
                                break;
                            default: break;
                            }
                        }
                        if (pass) {
                            reach[a] = reach[b] = 1;
                            any = true;
                        }
                    }
        /* Stairs join the rooms at both ends. */
        for (int i = 0; i < L->nstairs; i++) {
            const LvStair *st = &L->stairs[i];
            int ex = st->x + k_dx[st->dir] * LV_STAIR_LEN, ez = st->z + k_dz[st->dir] * LV_STAIR_LEN;
            int a = L->cells[st->floor][st->z][st->x].room;
            int b = lv_room_at(L, st->floor + 1, ex, ez);
            if (a && b && reach[a] != reach[b]) {
                reach[a] = reach[b] = 1;
                any = true;
            }
        }
        /* Vents join the rooms under their grates (on one floor). */
        for (int f = 0; f < L->floors; f++)
            for (int z = 0; z < L->d; z++)
                for (int x = 0; x < L->w; x++) {
                    if (!(L->cells[f][z][x].flags & LV_C_GRATE))
                        continue;
                    int a = L->cells[f][z][x].room;
                    if (!reach[a])
                        continue;
                    /* Follow the duct to its other grate. */
                    for (int s = 0; s < 4; s++) {
                        int cx = x, cz = z;
                        for (int k = 0; k < 20; k++) {
                            cx += k_dx[s];
                            cz += k_dz[s];
                            const LvCell *cc = lv_cell(L, f, cx, cz);
                            if (!cc || !(cc->flags & LV_C_VENT))
                                break;
                            if (cc->flags & LV_C_GRATE) {
                                if (!reach[cc->room]) {
                                    reach[cc->room] = 1;
                                    any = true;
                                }
                                break;
                            }
                        }
                    }
                }
    }
}

bool nh_gen_validate(const Level *L, const NhPlan *P, char *why, int why_len)
{
    static uint8_t reach[LV_ROOMS_MAX];
    gen_reach(L, P, reach);
    /* Everything reachable? */
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        if (L->rooms[r].flags & LV_R_ROOF)
            continue;
        if (!reach[r]) {
            if (why)
                snprintf(why, (size_t)why_len, "room %d (%s, floor %d) unreachable", r,
                         nh_room_styles[L->rooms[r].func].name, L->rooms[r].floor);
            return false;
        }
    }
    if (P->objective < 0 || P->objective >= P->nent || !reach[P->ent[P->objective].room]) {
        if (why)
            snprintf(why, (size_t)why_len, "objective missing or unreachable");
        return false;
    }
    if (P->nentry == 0 || P->nextract == 0) {
        if (why)
            snprintf(why, (size_t)why_len, "no entry or extraction point");
        return false;
    }
    return true;
}

/* ---- fingerprints and signatures ----------------------------------------------------- */

static uint32_t fnv(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        h ^= (v >> (i * 8)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

uint32_t nh_gen_fingerprint(const Level *L)
{
    uint32_t h = 2166136261u;
    h = fnv(h, (uint32_t)(L->w | (L->d << 8) | (L->floors << 16)));
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        const LvRoom *rr = &L->rooms[r];
        h = fnv(h, (uint32_t)(rr->floor | (rr->x0 << 8) | (rr->z0 << 16) | ((uint32_t)rr->func << 24)));
        h = fnv(h, (uint32_t)(rr->x1 | (rr->z1 << 8)));
    }
    for (int i = 0; i < L->ndoors; i++) {
        const LvDoor *d = &L->doors[i];
        h = fnv(h, (uint32_t)(d->floor | (d->x << 8) | (d->z << 16) | (d->axis << 24) | (d->lock << 28)));
    }
    for (int i = 0; i < L->nstairs; i++)
        h = fnv(h, (uint32_t)(L->stairs[i].floor | (L->stairs[i].x << 8) | (L->stairs[i].z << 16)));
    return h;
}

void nh_gen_signature(const Level *L, uint8_t sig[GEN_SIG_BYTES])
{
    memset(sig, 0, GEN_SIG_BYTES);
    for (int f = 0; f < L->floors; f++)
        for (int z = 0; z < L->d; z++)
            for (int x = 0; x < L->w; x++)
                for (int s = 0; s < 2; s++) {
                    const LvEdge *e = lv_edge_side_c(L, f, x, z, s);
                    if (!e || e->type == LV_E_OPEN)
                        continue;
                    /* Interior walls only: the outline says little about the layout. */
                    int a = L->cells[f][z][x].room, b = lv_neighbor_room(L, f, x, z, s);
                    if (a < LV_ROOM_FIRST || b < LV_ROOM_FIRST)
                        continue;
                    int bit = ((f * LV_D_MAX + z) * LV_W_MAX + x) * 2 + s;
                    sig[bit >> 3] |= (uint8_t)(1u << (bit & 7));
                }
}

float nh_gen_signature_distance(const uint8_t *a, const uint8_t *b)
{
    int inter = 0, uni = 0;
    for (int i = 0; i < GEN_SIG_BYTES; i++) {
        inter += __builtin_popcount((unsigned)(a[i] & b[i]));
        uni += __builtin_popcount((unsigned)(a[i] | b[i]));
    }
    return uni ? 1.0f - (float)inter / (float)uni : 0.0f;
}
