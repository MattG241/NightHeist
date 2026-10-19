#include "nh_level.h"

#include <math.h>
#include <string.h>

/* ---- building ---------------------------------------------------------------- */

void lv_init(Level *L, int w, int d, int floors, uint32_t seed)
{
    memset(L, 0, sizeof(*L));
    L->w = NH_CLAMP(w, 1, LV_W_MAX);
    L->d = NH_CLAMP(d, 1, LV_D_MAX);
    L->floors = NH_CLAMP(floors, 1, LV_F_MAX - 1);
    L->seed = seed;
    L->nrooms = LV_ROOM_FIRST; /* ids 0 (void) and 1 (outside) are reserved */
    LvRoom *out = &L->rooms[LV_ROOM_OUTSIDE];
    out->floor = 0;
    out->x1 = (uint8_t)(L->w - 1);
    out->z1 = (uint8_t)(L->d - 1);
    out->flags = LV_R_OUTDOOR | LV_R_LIT;
    out->wall_tint = nh_rgba(255, 255, 255, 255);
    for (int z = 0; z < L->d; z++)
        for (int x = 0; x < L->w; x++) {
            L->cells[0][z][x].room = LV_ROOM_OUTSIDE;
            L->cells[0][z][x].surface = LV_SURF_ASPHALT;
            L->cells[0][z][x].flags = LV_C_NOCEIL;
        }
    for (int i = 0; i < 16; i++)
        L->power[i] = 1;
}

int lv_add_room(Level *L, int floor, int x0, int z0, int x1, int z1, int func, int flags)
{
    if (L->nrooms >= LV_ROOMS_MAX || floor < 0 || floor > L->floors)
        return 0;
    x0 = NH_CLAMP(x0, 0, L->w - 1);
    x1 = NH_CLAMP(x1, 0, L->w - 1);
    z0 = NH_CLAMP(z0, 0, L->d - 1);
    z1 = NH_CLAMP(z1, 0, L->d - 1);
    int id = L->nrooms++;
    LvRoom *r = &L->rooms[id];
    memset(r, 0, sizeof(*r));
    r->floor = (uint8_t)floor;
    r->x0 = (uint8_t)x0;
    r->z0 = (uint8_t)z0;
    r->x1 = (uint8_t)x1;
    r->z1 = (uint8_t)z1;
    r->func = (uint8_t)func;
    r->flags = (uint8_t)flags;
    r->wall_tint = nh_rgba(255, 255, 255, 255);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            LvCell *c = &L->cells[floor][z][x];
            c->room = (uint8_t)id;
            c->flags &= (uint8_t)~LV_C_NOCEIL;
            if (flags & LV_R_OUTDOOR)
                c->flags |= LV_C_NOCEIL;
            c->surface = LV_SURF_CONCRETE;
        }
    return id;
}

static LvEdge *edge_ptr(Level *L, int f, int x, int z, int side)
{
    switch (side & 3) {
    case 0: return &L->ex[f][z][x + 1];
    case 1: return &L->ez[f][z + 1][x];
    case 2: return &L->ex[f][z][x];
    default: return &L->ez[f][z][x];
    }
}

LvEdge *lv_edge_side(Level *L, int f, int x, int z, int side)
{
    if (!lv_in_bounds(L, f, x, z))
        return NULL;
    return edge_ptr(L, f, x, z, side);
}

const LvEdge *lv_edge_side_c(const Level *L, int f, int x, int z, int side)
{
    return lv_edge_side((Level *)L, f, x, z, side);
}

static const int k_dx[4] = { 1, 0, -1, 0 };
static const int k_dz[4] = { 0, 1, 0, -1 };

int lv_neighbor_room(const Level *L, int f, int x, int z, int side)
{
    return lv_room_at(L, f, x + k_dx[side & 3], z + k_dz[side & 3]);
}

void lv_set_edge(Level *L, int f, int x, int z, int side, int type)
{
    LvEdge *e = lv_edge_side(L, f, x, z, side);
    if (e) {
        e->type = (uint8_t)type;
        e->ref = 0;
    }
}

int lv_add_door(Level *L, int f, int x, int z, int side, int kind, int lock, int level, int key_id)
{
    LvEdge *e = lv_edge_side(L, f, x, z, side);
    if (!e || L->ndoors >= LV_DOORS_MAX)
        return -1;
    int i = L->ndoors++;
    LvDoor *d = &L->doors[i];
    memset(d, 0, sizeof(*d));
    side &= 3;
    d->floor = (uint8_t)f;
    d->axis = (uint8_t)(side & 1);
    d->x = (uint8_t)(x + (side == 0 ? 1 : 0));
    d->z = (uint8_t)(z + (side == 1 ? 1 : 0));
    d->kind = (uint8_t)kind;
    d->lock = (uint8_t)lock;
    d->lock_level = (uint8_t)level;
    d->key_id = (uint8_t)key_id;
    d->locked = lock != LV_LOCK_NONE;
    int here = lv_room_at(L, f, x, z), there = lv_neighbor_room(L, f, x, z, side);
    if (side == 0 || side == 1) {
        d->room_a = (uint8_t)here;
        d->room_b = (uint8_t)there;
    } else {
        d->room_a = (uint8_t)there;
        d->room_b = (uint8_t)here;
    }
    if (here == LV_ROOM_OUTSIDE || there == LV_ROOM_OUTSIDE)
        d->flags |= LV_DOOR_F_EXTERIOR;
    e->type = LV_E_DOOR;
    e->ref = (uint8_t)(i + 1);
    return i;
}

void lv_add_window(Level *L, int f, int x, int z, int side)
{
    LvEdge *e = lv_edge_side(L, f, x, z, side);
    if (e) {
        e->type = LV_E_WINDOW;
        e->ref = LV_WIN_CLOSED;
    }
}

int lv_add_stair(Level *L, int f, int x, int z, int dir)
{
    if (L->nstairs >= LV_STAIRS_MAX || f + 1 > L->floors)
        return -1;
    int i = L->nstairs++;
    LvStair *s = &L->stairs[i];
    s->floor = (uint8_t)f;
    s->x = (uint8_t)x;
    s->z = (uint8_t)z;
    s->dir = (uint8_t)(dir & 3);
    for (int k = 0; k < LV_STAIR_LEN; k++) {
        int cx = x + k_dx[dir & 3] * k, cz = z + k_dz[dir & 3] * k;
        if (!lv_in_bounds(L, f, cx, cz))
            continue;
        L->cells[f][cz][cx].stair = (uint8_t)(i + 1);
        L->cells[f + 1][cz][cx].flags |= LV_C_HOLE;
    }
    return i;
}

/* True if the edge on `side` of upper-floor hole cell (x,z) is where stair `s`
 * comes out, i.e. must stay open. */
static bool is_stair_exit(const Level *L, int f, int x, int z, int side)
{
    for (int i = 0; i < L->nstairs; i++) {
        const LvStair *s = &L->stairs[i];
        if (s->floor + 1 != f || s->dir != side)
            continue;
        int lx = s->x + k_dx[s->dir] * (LV_STAIR_LEN - 1), lz = s->z + k_dz[s->dir] * (LV_STAIR_LEN - 1);
        if (lx == x && lz == z)
            return true;
    }
    return false;
}

static bool room_is_open_air(const Level *L, int room)
{
    return room == LV_ROOM_OUTSIDE || (room >= LV_ROOM_FIRST && (L->rooms[room].flags & LV_R_OUTDOOR));
}

void lv_finalize(Level *L)
{
    for (int f = 0; f <= L->floors; f++) {
        for (int z = 0; z < L->d; z++) {
            for (int x = 0; x < L->w; x++) {
                /* Each cell owns its +X and +Z edges; out-of-map neighbours
                 * are handled through the -X/-Z sides of the first row/col. */
                for (int side = 0; side < 4; side++) {
                    if ((side == 2 && x > 0) || (side == 3 && z > 0))
                        continue;
                    LvEdge *e = edge_ptr(L, f, x, z, side);
                    if (e->type != LV_E_OPEN)
                        continue;
                    int ra = L->cells[f][z][x].room;
                    int nx = x + k_dx[side], nz = z + k_dz[side];
                    int rb = lv_in_bounds(L, f, nx, nz) ? L->cells[f][nz][nx].room : LV_ROOM_VOID;
                    bool ha = (L->cells[f][z][x].flags & LV_C_HOLE) != 0;
                    bool hb = lv_in_bounds(L, f, nx, nz) && (L->cells[f][nz][nx].flags & LV_C_HOLE);
                    if (ra == rb) {
                        if (ra != LV_ROOM_VOID && ha != hb) {
                            /* Railing around stair holes, except where the stair exits. */
                            int hx = ha ? x : nx, hz = ha ? z : nz;
                            int hside = ha ? side : (side + 2) & 3;
                            if (!is_stair_exit(L, f, hx, hz, hside))
                                e->type = LV_E_RAIL;
                        }
                        continue;
                    }
                    if (ra == LV_ROOM_VOID && rb == LV_ROOM_VOID)
                        continue;
                    if (ra == LV_ROOM_VOID || rb == LV_ROOM_VOID) {
                        int solid = ra == LV_ROOM_VOID ? rb : ra;
                        /* Open-air areas meeting void get a railing (roofs,
                         * balconies); the ground outside just ends. */
                        if (room_is_open_air(L, solid)) {
                            if (solid != LV_ROOM_OUTSIDE)
                                e->type = LV_E_RAIL;
                            continue;
                        }
                    } else if (room_is_open_air(L, ra) && room_is_open_air(L, rb)) {
                        e->type = LV_E_RAIL;
                        continue;
                    }
                    e->type = LV_E_WALL;
                }
            }
        }
    }
    lv_index_props(L);
}

int lv_add_prop(Level *L, int type, int f, float x, float y, float z, float yaw, NhVec3 half, int flags)
{
    if (L->nprops >= LV_PROPS_MAX)
        return -1;
    int i = L->nprops++;
    LvProp *p = &L->props[i];
    memset(p, 0, sizeof(*p));
    p->type = (uint16_t)type;
    p->floor = (uint8_t)f;
    p->x = x;
    p->y = y;
    p->z = z;
    p->yaw = yaw;
    p->half = half;
    p->flags = (uint8_t)flags;
    p->room = (uint8_t)lv_room_at(L, f, lv_cell_of(x), lv_cell_of(z));
    return i;
}

void lv_index_props(Level *L)
{
    memset(L->cell_props, 0, sizeof(L->cell_props));
    /* Solid and view-blocking props first so small items never crowd them
     * out of a full cell. */
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < L->nprops; i++) {
        const LvProp *p = &L->props[i];
        bool hard = (p->flags & (LV_P_SOLID | LV_P_OCCLUDER)) != 0;
        if (hard != (pass == 0))
            continue;
        float r = sqrtf(p->half.x * p->half.x + p->half.z * p->half.z);
        int x0 = lv_cell_of(p->x - r), x1 = lv_cell_of(p->x + r);
        int z0 = lv_cell_of(p->z - r), z1 = lv_cell_of(p->z + r);
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                if (!lv_in_bounds(L, p->floor, x, z))
                    continue;
                uint16_t *slots = L->cell_props[p->floor][z][x];
                for (int s = 0; s < LV_CELL_PROPS; s++)
                    if (!slots[s]) {
                        slots[s] = (uint16_t)(i + 1);
                        break;
                    }
            }
    }
}

int lv_add_light(Level *L, NhVec3 pos, NhColor color, float radius, int kind, int room, int circuit)
{
    if (L->nlights >= LV_LIGHTS_MAX)
        return -1;
    int i = L->nlights++;
    LvLight *l = &L->lights[i];
    memset(l, 0, sizeof(*l));
    l->pos = pos;
    l->dir = nh_v3(0, -1, 0);
    l->color = color;
    l->radius = radius;
    l->kind = (uint8_t)kind;
    l->room = (uint8_t)room;
    l->circuit = (uint8_t)circuit;
    l->on = 1;
    return i;
}

/* ---- queries ------------------------------------------------------------------ */

const LvCell *lv_cell(const Level *L, int f, int x, int z)
{
    if (!lv_in_bounds(L, f, x, z))
        return NULL;
    return &L->cells[f][z][x];
}

int lv_room_at(const Level *L, int f, int x, int z)
{
    const LvCell *c = lv_cell(L, f, x, z);
    return c ? c->room : LV_ROOM_VOID;
}

int lv_cell_of(float world)
{
    return (int)floorf(world / LV_CELL);
}

int lv_floor_of(float y)
{
    int f = (int)floorf((y + 0.05f) / LV_STOREY);
    return f < 0 ? 0 : f;
}

float lv_floor_y(int f)
{
    return (float)f * LV_STOREY;
}

NhVec3 lv_cell_center(int f, int x, int z)
{
    return nh_v3(((float)x + 0.5f) * LV_CELL, lv_floor_y(f), ((float)z + 0.5f) * LV_CELL);
}

bool lv_edge_blocks_move(const Level *L, const LvEdge *e)
{
    switch (e->type) {
    case LV_E_OPEN:
    case LV_E_ARCH:
        return false;
    case LV_E_DOOR:
        return e->ref && L->doors[e->ref - 1].open < 0.8f;
    case LV_E_WINDOW:
        return e->ref == LV_WIN_CLOSED;
    default:
        return true;
    }
}

bool lv_edge_blocks_sight(const Level *L, const LvEdge *e, float h)
{
    if (h > LV_CEIL)
        return e->type != LV_E_OPEN;
    switch (e->type) {
    case LV_E_OPEN:
    case LV_E_RAIL:
        return false;
    case LV_E_ARCH:
        return h > LV_DOOR_H;
    case LV_E_DOOR:
        if (h > LV_DOOR_H)
            return true;
        return e->ref && L->doors[e->ref - 1].open < 0.25f;
    case LV_E_WINDOW:
        return h < LV_WIN_Y0 || h > LV_WIN_Y1;
    default:
        return true;
    }
}

float lv_stair_height(const Level *L, int f, float x, float z)
{
    const LvCell *c = lv_cell(L, f, lv_cell_of(x), lv_cell_of(z));
    if (!c || !c->stair)
        return -1.0f;
    const LvStair *s = &L->stairs[c->stair - 1];
    float run = LV_STAIR_LEN * LV_CELL, t;
    switch (s->dir) {
    case 0: t = x - s->x * LV_CELL; break;
    case 1: t = z - s->z * LV_CELL; break;
    case 2: t = (s->x + 1) * LV_CELL - x; break;
    default: t = (s->z + 1) * LV_CELL - z; break;
    }
    t = NH_CLAMP(t / run, 0.0f, 1.0f);
    return lv_floor_y(f) + t * LV_STOREY;
}

float lv_ground_height(const Level *L, float x, float z, float feet_y)
{
    int cx = lv_cell_of(x), cz = lv_cell_of(z);
    float reach = feet_y + LV_STEP_UP;
    int top = NH_MIN(lv_floor_of(reach), L->floors);
    for (int f = top; f >= 0; f--) {
        const LvCell *c = lv_cell(L, f, cx, cz);
        if (!c) {
            if (f == 0)
                return 0.0f; /* the ground continues past the map edge */
            continue;
        }
        if (c->stair) {
            float h = lv_stair_height(L, f, x, z);
            if (h <= reach)
                return h;
        }
        if (c->flags & LV_C_HOLE)
            continue;
        if (c->room == LV_ROOM_VOID && f > 0)
            continue;
        float slab = lv_floor_y(f);
        if (slab <= reach)
            return slab;
    }
    return -1000.0f;
}

/* Push a circle (centre p, radius r) out of a segment a-b in XZ. */
static bool push_segment(float *px, float *pz, float r, float ax, float az, float bx, float bz)
{
    float dx = bx - ax, dz = bz - az;
    float len2 = dx * dx + dz * dz;
    float t = len2 > 0 ? ((*px - ax) * dx + (*pz - az) * dz) / len2 : 0.0f;
    t = NH_CLAMP(t, 0.0f, 1.0f);
    float cx = ax + dx * t, cz = az + dz * t;
    float ox = *px - cx, oz = *pz - cz;
    float d2 = ox * ox + oz * oz;
    if (d2 >= r * r)
        return false;
    float d = sqrtf(d2);
    if (d < 1e-5f) {
        /* Exactly on the line: push along the segment normal. */
        ox = -dz;
        oz = dx;
        d = sqrtf(ox * ox + oz * oz);
        if (d < 1e-6f)
            return false;
        ox /= d;
        oz /= d;
        *px = cx + ox * r;
        *pz = cz + oz * r;
        return true;
    }
    *px = cx + ox / d * r;
    *pz = cz + oz / d * r;
    return true;
}

/* Does an edge block a cylinder spanning [y0, y1] relative to its floor? */
static bool edge_blocks_cylinder(const Level *L, const LvEdge *e, float y0, float y1)
{
    if (!lv_edge_blocks_move(L, e)) {
        /* Openings still have a wall above them (lintel) and below windows. */
        if (e->type == LV_E_DOOR || e->type == LV_E_ARCH)
            return y1 > LV_DOOR_H + 0.05f;
        if (e->type == LV_E_WINDOW)
            return y0 < LV_WIN_Y0 - LV_STEP_UP || y1 > LV_WIN_Y1;
        return false;
    }
    if (e->type == LV_E_RAIL)
        return y0 < LV_RAIL_H;
    return true;
}

static bool push_prop(const LvProp *p, float *px, float *pz, float r)
{
    float s = sinf(p->yaw), c = cosf(p->yaw);
    float dx = *px - p->x, dz = *pz - p->z;
    /* Into the prop's frame (yaw rotates about +Y like nh_m34_rot_y). */
    float lx = c * dx - s * dz, lz = s * dx + c * dz;
    float qx = NH_CLAMP(lx, -p->half.x, p->half.x), qz = NH_CLAMP(lz, -p->half.z, p->half.z);
    float ox = lx - qx, oz = lz - qz;
    float d2 = ox * ox + oz * oz;
    if (d2 >= r * r)
        return false;
    if (d2 < 1e-8f) {
        /* Centre inside the box: push out through the nearest face. */
        float ex = p->half.x - fabsf(lx), ez = p->half.z - fabsf(lz);
        if (ex < ez)
            lx = (lx < 0 ? -1.0f : 1.0f) * (p->half.x + r);
        else
            lz = (lz < 0 ? -1.0f : 1.0f) * (p->half.z + r);
    } else {
        float d = sqrtf(d2);
        lx = qx + ox / d * r;
        lz = qz + oz / d * r;
    }
    *px = p->x + c * lx + s * lz;
    *pz = p->z - s * lx + c * lz;
    return true;
}

#define WALL_HALF_THICK 0.08f

bool lv_collide_cylinder(const Level *L, NhVec3 *pos, float r, float y0, float y1)
{
    bool hit = false;
    float px = pos->x, pz = pos->z;
    for (int iter = 0; iter < 3; iter++) {
        bool any = false;
        int fl0 = lv_floor_of(y0), fl1 = NH_MIN(lv_floor_of(y1), L->floors);
        for (int f = fl0; f <= fl1; f++) {
            float base = lv_floor_y(f);
            float ly0 = y0 - base, ly1 = y1 - base;
            int cx = lv_cell_of(px), cz = lv_cell_of(pz);
            for (int z = cz - 1; z <= cz + 1; z++) {
                for (int x = cx - 1; x <= cx + 1; x++) {
                    if (!lv_in_bounds(L, f, x, z))
                        continue;
                    /* +X and +Z edges of this cell, plus -X/-Z on the map border. */
                    for (int side = 0; side < 4; side++) {
                        if ((side == 2 && x > 0) || (side == 3 && z > 0))
                            continue;
                        const LvEdge *e = edge_ptr((Level *)L, f, x, z, side);
                        if (e->type == LV_E_OPEN || !edge_blocks_cylinder(L, e, ly0, ly1))
                            continue;
                        float ax, az, bx, bz;
                        if (side == 0 || side == 2) {
                            ax = bx = (float)(x + (side == 0)) * LV_CELL;
                            az = (float)z * LV_CELL;
                            bz = az + LV_CELL;
                        } else {
                            az = bz = (float)(z + (side == 1)) * LV_CELL;
                            ax = (float)x * LV_CELL;
                            bx = ax + LV_CELL;
                        }
                        if (push_segment(&px, &pz, r + WALL_HALF_THICK, ax, az, bx, bz))
                            any = true;
                    }
                    for (int s = 0; s < LV_CELL_PROPS; s++) {
                        uint16_t pi = L->cell_props[f][z][x][s];
                        if (!pi)
                            break;
                        const LvProp *p = &L->props[pi - 1];
                        if (!(p->flags & LV_P_SOLID) || (p->flags & LV_P_HIDDEN))
                            continue;
                        if (y1 <= p->y + 0.02f || y0 >= p->y + p->half.y * 2.0f - LV_STEP_UP * 0.5f)
                            continue;
                        if (push_prop(p, &px, &pz, r))
                            any = true;
                    }
                }
            }
        }
        /* Map border. */
        float maxx = L->w * LV_CELL - r, maxz = L->d * LV_CELL - r;
        if (px < r) { px = r; any = true; }
        if (pz < r) { pz = r; any = true; }
        if (px > maxx) { px = maxx; any = true; }
        if (pz > maxz) { pz = maxz; any = true; }
        if (!any)
            break;
        hit = true;
    }
    pos->x = px;
    pos->z = pz;
    return hit;
}

/* Segment vs prop box (slab test in the prop frame). */
static bool seg_hits_prop(const LvProp *p, NhVec3 a, NhVec3 b, float *t_out)
{
    float s = sinf(p->yaw), c = cosf(p->yaw);
    float ax = a.x - p->x, az = a.z - p->z, bx = b.x - p->x, bz = b.z - p->z;
    float o[3] = { c * ax - s * az, a.y - (p->y + p->half.y), s * ax + c * az };
    float e[3] = { c * bx - s * bz, b.y - (p->y + p->half.y), s * bx + c * bz };
    float h[3] = { p->half.x, p->half.y, p->half.z };
    float t0 = 0.0f, t1 = 1.0f;
    for (int i = 0; i < 3; i++) {
        float d = e[i] - o[i];
        if (fabsf(d) < 1e-7f) {
            if (o[i] < -h[i] || o[i] > h[i])
                return false;
            continue;
        }
        float ta = (-h[i] - o[i]) / d, tb = (h[i] - o[i]) / d;
        if (ta > tb) {
            float tmp = ta;
            ta = tb;
            tb = tmp;
        }
        t0 = fmaxf(t0, ta);
        t1 = fminf(t1, tb);
        if (t0 > t1)
            return false;
    }
    *t_out = t0;
    return true;
}

static bool cell_blocks_vertical(const Level *L, int f, int x, int z, float ya, float yb)
{
    /* A segment piece inside one cell column, from height ya to yb: does it
     * pass through a floor slab or a ceiling void? */
    float lo = fminf(ya, yb), hi = fmaxf(ya, yb);
    int f0 = lv_floor_of(lo), f1 = lv_floor_of(hi);
    for (int k = f0 + 1; k <= f1; k++) {
        const LvCell *c = lv_cell(L, k, x, z);
        if (c && !(c->flags & LV_C_HOLE) && c->room != LV_ROOM_VOID)
            return true;
    }
    const LvCell *c = lv_cell(L, f, x, z);
    if (c && !(c->flags & LV_C_NOCEIL) && !(c->flags & LV_C_HOLE)) {
        float ceil = lv_floor_y(f) + LV_CEIL;
        const LvCell *above = lv_cell(L, f + 1, x, z);
        bool open_above = above && (above->flags & LV_C_HOLE);
        if (!open_above && hi > ceil && lo < ceil + 0.01f && f1 == f0)
            return true;
    }
    return false;
}

bool lv_raycast(const Level *L, NhVec3 a, NhVec3 b, float *hit_t)
{
    float dx = b.x - a.x, dz = b.z - a.z, dy = b.y - a.y;
    int ix = lv_cell_of(a.x), iz = lv_cell_of(a.z);
    int ex = lv_cell_of(b.x), ez = lv_cell_of(b.z);
    int stepx = dx > 0 ? 1 : -1, stepz = dz > 0 ? 1 : -1;
    float tdx = fabsf(dx) > 1e-9f ? LV_CELL / fabsf(dx) : 1e30f;
    float tdz = fabsf(dz) > 1e-9f ? LV_CELL / fabsf(dz) : 1e30f;
    float tmx = fabsf(dx) > 1e-9f ? (((float)(ix + (dx > 0)) * LV_CELL) - a.x) / dx : 1e30f;
    float tmz = fabsf(dz) > 1e-9f ? (((float)(iz + (dz > 0)) * LV_CELL) - a.z) / dz : 1e30f;
    float t = 0.0f;
    float best = 2.0f;
    for (int guard = 0; guard < 256; guard++) {
        float tnext = fminf(fminf(tmx, tmz), 1.0f);
        /* Contents of the current cell column between t and tnext. */
        float ya = a.y + dy * t, yb = a.y + dy * tnext;
        int f = lv_floor_of(fminf(ya, yb));
        if (lv_in_bounds(L, f, ix, iz)) {
            if (cell_blocks_vertical(L, f, ix, iz, ya, yb)) {
                best = t;
                break;
            }
            for (int ff = lv_floor_of(fminf(ya, yb)); ff <= NH_MIN(lv_floor_of(fmaxf(ya, yb)), L->floors); ff++) {
                for (int s = 0; s < LV_CELL_PROPS; s++) {
                    uint16_t pi = L->cell_props[ff][iz][ix][s];
                    if (!pi)
                        break;
                    const LvProp *p = &L->props[pi - 1];
                    float pt;
                    if ((p->flags & LV_P_OCCLUDER) && !(p->flags & LV_P_HIDDEN) && seg_hits_prop(p, a, b, &pt) &&
                        pt < best)
                        best = pt;
                }
            }
            if (best <= tnext)
                break;
        }
        if (tnext >= 1.0f || (ix == ex && iz == ez))
            break;
        /* Cross into the next cell through an edge. */
        float tc = tnext;
        NhVec3 p = nh_v3(a.x + dx * tc, a.y + dy * tc, a.z + dz * tc);
        int pf = lv_floor_of(p.y);
        float h = p.y - lv_floor_y(pf);
        const LvEdge *e = NULL;
        if (tmx < tmz) {
            if (lv_in_bounds(L, pf, ix, iz))
                e = edge_ptr((Level *)L, pf, ix, iz, stepx > 0 ? 0 : 2);
            ix += stepx;
            tmx += tdx;
        } else {
            if (lv_in_bounds(L, pf, ix, iz))
                e = edge_ptr((Level *)L, pf, ix, iz, stepz > 0 ? 1 : 3);
            iz += stepz;
            tmz += tdz;
        }
        if (e && lv_edge_blocks_sight(L, e, h)) {
            best = tc;
            break;
        }
        t = tc;
    }
    if (best <= 1.0f) {
        if (hit_t)
            *hit_t = best;
        return true;
    }
    return false;
}
