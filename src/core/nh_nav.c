#include "nh_nav.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const int k_dx[4] = { 1, 0, -1, 0 };
static const int k_dz[4] = { 0, 1, 0, -1 };

static int node_id(const NhNav *nav, int f, int x, int z)
{
    (void)nav;
    return (f * NAV_D + z) * NAV_W + x;
}

static void node_xyz(int id, int *f, int *x, int *z)
{
    *x = id % NAV_W;
    *z = (id / NAV_W) % NAV_D;
    *f = id / (NAV_W * NAV_D);
}

static NhVec3 node_pos(const NhNav *nav, int f, int x, int z)
{
    float wx = ((float)x + 0.5f) * (LV_CELL / NAV_RES), wz = ((float)z + 0.5f) * (LV_CELL / NAV_RES);
    float y = lv_floor_y(f);
    float sh = lv_stair_height(nav->L, f, wx, wz);
    if (sh >= 0.0f)
        y = sh;
    return nh_v3(wx, y, wz);
}

static bool prop_blocks(const LvProp *p, float x, float z, float r)
{
    float s = sinf(p->yaw), c = cosf(p->yaw);
    float dx = x - p->x, dz = z - p->z;
    float lx = c * dx - s * dz, lz = s * dx + c * dz;
    return fabsf(lx) < p->half.x + r && fabsf(lz) < p->half.z + r;
}

void nh_nav_build(NhNav *nav, const Level *L, float r)
{
    nav->L = L;
    nav->w = L->w * NAV_RES;
    nav->d = L->d * NAV_RES;
    nav->floors = L->floors;
    memset(nav->flags, 0, sizeof(nav->flags));
    for (int f = 0; f <= L->floors; f++) {
        for (int z = 0; z < nav->d; z++) {
            for (int x = 0; x < nav->w; x++) {
                const LvCell *c = lv_cell(L, f, x / NAV_RES, z / NAV_RES);
                if (!c || c->room == LV_ROOM_VOID || (c->flags & LV_C_HOLE))
                    continue;
                uint8_t fl = NAV_WALK;
                if (c->stair)
                    fl |= NAV_STAIR;
                if (c->room == LV_ROOM_OUTSIDE)
                    fl |= NAV_OUTSIDE;
                float wx = ((float)x + 0.5f) * (LV_CELL / NAV_RES), wz = ((float)z + 0.5f) * (LV_CELL / NAV_RES);
                /* Props indexed in this cell and its neighbours (the agent
                 * radius reaches across cell borders). */
                int cx = x / NAV_RES, cz = z / NAV_RES;
                for (int dz = -1; dz <= 1 && fl; dz++)
                    for (int dx = -1; dx <= 1 && fl; dx++) {
                        if (!lv_in_bounds(L, f, cx + dx, cz + dz))
                            continue;
                        const uint16_t *slots = L->cell_props[f][cz + dz][cx + dx];
                        for (int k = 0; k < LV_CELL_PROPS && slots[k]; k++) {
                            const LvProp *p = &L->props[slots[k] - 1];
                            if (!(p->flags & LV_P_SOLID) || (p->flags & LV_P_HIDDEN))
                                continue;
                            if (p->y > lv_floor_y(f) + 1.5f)
                                continue; /* wall-mounted */
                            if (prop_blocks(p, wx, wz, r)) {
                                fl = 0;
                                break;
                            }
                        }
                    }
                nav->flags[f][z][x] = fl;
            }
        }
    }
    nh_nav_clear_cache(nav);
}

void nh_nav_clear_cache(NhNav *nav)
{
    for (int i = 0; i < NAV_CACHE; i++)
        nav->cache[i].from = -1;
}

static bool in_nav(const NhNav *nav, int f, int x, int z)
{
    return f >= 0 && f <= nav->floors && x >= 0 && z >= 0 && x < nav->w && z < nav->d;
}

static uint8_t nflags(const NhNav *nav, int f, int x, int z)
{
    return in_nav(nav, f, x, z) ? nav->flags[f][z][x] : 0;
}

/* Can an agent step from node (x,z) to its 4-neighbour in direction s? */
static bool step_ok(const NhNav *nav, int f, int x, int z, int s, bool doors, float *extra)
{
    int nx = x + k_dx[s], nz = z + k_dz[s];
    if (!(nflags(nav, f, nx, nz) & NAV_WALK))
        return false;
    int cx = x / NAV_RES, cz = z / NAV_RES, ncx = nx / NAV_RES, ncz = nz / NAV_RES;
    *extra = 0.0f;
    const Level *L = nav->L;
    const LvCell *ca = lv_cell(L, f, cx, cz), *cb = lv_cell(L, f, ncx, ncz);
    if (cx != ncx || cz != ncz) {
        const LvEdge *e = lv_edge_side_c(L, f, cx, cz, s);
        if (!e)
            return false;
        switch (e->type) {
        case LV_E_OPEN:
            break;
        case LV_E_ARCH:
            break;
        case LV_E_DOOR: {
            if (!doors)
                return false;
            const LvDoor *d = &L->doors[e->ref - 1];
            if (d->locked && d->open < 0.5f)
                *extra = 40.0f; /* guards have keys, but avoid locked routes */
            else
                *extra = 1.5f;
            break;
        }
        default:
            return false;
        }
        /* Stair cells only connect along the stair, entering at the bottom. */
        if (ca->stair || cb->stair) {
            if (ca->stair && cb->stair && ca->stair == cb->stair) {
                const LvStair *st = &L->stairs[ca->stair - 1];
                if (s != st->dir && s != ((st->dir + 2) & 3))
                    return false;
            } else {
                const LvStair *st = &L->stairs[(cb->stair ? cb->stair : ca->stair) - 1];
                bool entering = cb->stair != 0;
                int want = entering ? st->dir : (st->dir + 2) & 3;
                int sc = entering ? ncx : cx, sz = entering ? ncz : cz;
                if (s != want || sc != st->x || sz != st->z)
                    return false;
            }
        }
    }
    return true;
}

typedef struct Link {
    int to;
    float cost;
} Link;

/* Neighbours of a node: 4-way steps, diagonals when both sides are clear,
 * and stair-top links to the floor above (and back down). */
static int neighbours(const NhNav *nav, int id, bool doors, Link *out)
{
    int f, x, z;
    node_xyz(id, &f, &x, &z);
    int n = 0;
    bool ok4[4] = { false, false, false, false };
    for (int s = 0; s < 4; s++) {
        float extra;
        if (step_ok(nav, f, x, z, s, doors, &extra)) {
            ok4[s] = true;
            out[n++] = (Link){ node_id(nav, f, x + k_dx[s], z + k_dz[s]), 1.0f + extra };
        }
    }
    for (int s = 0; s < 4; s++) {
        int t = (s + 1) & 3;
        if (!ok4[s] || !ok4[t])
            continue;
        int dx = x + k_dx[s] + k_dx[t], dz = z + k_dz[s] + k_dz[t];
        float e1, e2;
        /* Both L-shaped routes must exist so we never cut a wall corner. */
        if (!(nflags(nav, f, dx, dz) & NAV_WALK) || !step_ok(nav, f, x + k_dx[s], z + k_dz[s], t, doors, &e1) ||
            !step_ok(nav, f, x + k_dx[t], z + k_dz[t], s, doors, &e2))
            continue;
        if (e1 > 0.0f || e2 > 0.0f)
            continue; /* don't cut diagonally through doorways */
        out[n++] = (Link){ node_id(nav, f, dx, dz), 1.4142f };
    }
    const Level *L = nav->L;
    const LvCell *c = lv_cell(L, f, x / NAV_RES, z / NAV_RES);
    for (int i = 0; i < L->nstairs; i++) {
        const LvStair *st = &L->stairs[i];
        int topx = st->x + k_dx[st->dir] * (LV_STAIR_LEN - 1), topz = st->z + k_dz[st->dir] * (LV_STAIR_LEN - 1);
        int exx = topx + k_dx[st->dir], exz = topz + k_dz[st->dir];
        /* Node in the last stair cell, on the far edge -> exit cell above. */
        if (c && c->stair == i + 1 && f == st->floor && x / NAV_RES == topx && z / NAV_RES == topz) {
            int nx = x + k_dx[st->dir], nz = z + k_dz[st->dir];
            if (nx / NAV_RES == exx && nz / NAV_RES == exz && (nflags(nav, f + 1, nx, nz) & NAV_WALK))
                out[n++] = (Link){ node_id(nav, f + 1, nx, nz), 1.2f };
        }
        if (f == st->floor + 1 && x / NAV_RES == exx && z / NAV_RES == exz) {
            int px = x - k_dx[st->dir], pz = z - k_dz[st->dir];
            if (px / NAV_RES == topx && pz / NAV_RES == topz && (nflags(nav, f - 1, px, pz) & NAV_WALK))
                out[n++] = (Link){ node_id(nav, f - 1, px, pz), 1.2f };
        }
    }
    return n;
}

static int pos_node(const NhNav *nav, NhVec3 p)
{
    int f = lv_floor_of(p.y + 0.3f);
    int x = (int)floorf(p.x / (LV_CELL / NAV_RES)), z = (int)floorf(p.z / (LV_CELL / NAV_RES));
    /* On a stair ramp the feet may be closer to the floor above. */
    if (!(nflags(nav, f, x, z) & NAV_WALK) && f > 0 && (nflags(nav, f - 1, x, z) & NAV_STAIR))
        f--;
    if (!in_nav(nav, f, x, z))
        return -1;
    return node_id(nav, f, x, z);
}

bool nh_nav_walkable(const NhNav *nav, NhVec3 p)
{
    int id = pos_node(nav, p);
    if (id < 0)
        return false;
    int f, x, z;
    node_xyz(id, &f, &x, &z);
    return (nav->flags[f][z][x] & NAV_WALK) != 0;
}

bool nh_nav_snap(const NhNav *nav, NhVec3 p, NhVec3 *out)
{
    int id = pos_node(nav, p);
    if (id < 0) {
        int f = NH_CLAMP(lv_floor_of(p.y + 0.3f), 0, nav->floors);
        int x = NH_CLAMP((int)(p.x / (LV_CELL / NAV_RES)), 0, nav->w - 1);
        int z = NH_CLAMP((int)(p.z / (LV_CELL / NAV_RES)), 0, nav->d - 1);
        id = node_id(nav, f, x, z);
    }
    int f, x, z;
    node_xyz(id, &f, &x, &z);
    for (int r = 0; r <= 4; r++) {
        float best = 1e9f;
        int bx = -1, bz = -1;
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dz) != r)
                    continue;
                if (!(nflags(nav, f, x + dx, z + dz) & NAV_WALK))
                    continue;
                float d = (float)(dx * dx + dz * dz);
                if (d < best) {
                    best = d;
                    bx = x + dx;
                    bz = z + dz;
                }
            }
        if (bx >= 0) {
            *out = node_pos(nav, f, bx, bz);
            return true;
        }
    }
    return false;
}

/* ---- A* -------------------------------------------------------------------- */

static float heur(const NhNav *nav, int a, int b)
{
    int fa, xa, za, fb, xb, zb;
    node_xyz(a, &fa, &xa, &za);
    node_xyz(b, &fb, &xb, &zb);
    float dx = (float)(xa - xb), dz = (float)(za - zb);
    (void)nav;
    return sqrtf(dx * dx + dz * dz) + (float)abs(fa - fb) * 3.0f;
}

static float g_of(const NhNav *nav, int id)
{
    return nav->stamp[id] == nav->cur_stamp ? nav->g[id] : 1e30f;
}

static void heap_push(NhNav *nav, int id, const float *f_score_unused)
{
    (void)f_score_unused;
    nav->heap[nav->heap_n++] = id;
}

/* Small binary heap keyed by g + h; h recomputed on compare (cheap). */
static int s_goal;
static float key(const NhNav *nav, int id)
{
    return nav->g[id] + heur(nav, id, s_goal);
}

static void sift_up(NhNav *nav, int i)
{
    while (i > 0) {
        int p = (i - 1) / 2;
        if (key(nav, nav->heap[p]) <= key(nav, nav->heap[i]))
            break;
        int t = nav->heap[p];
        nav->heap[p] = nav->heap[i];
        nav->heap[i] = t;
        i = p;
    }
}

static int heap_pop(NhNav *nav)
{
    int top = nav->heap[0];
    nav->heap[0] = nav->heap[--nav->heap_n];
    int i = 0;
    for (;;) {
        int l = i * 2 + 1, r = l + 1, m = i;
        if (l < nav->heap_n && key(nav, nav->heap[l]) < key(nav, nav->heap[m]))
            m = l;
        if (r < nav->heap_n && key(nav, nav->heap[r]) < key(nav, nav->heap[m]))
            m = r;
        if (m == i)
            break;
        int t = nav->heap[m];
        nav->heap[m] = nav->heap[i];
        nav->heap[i] = t;
        i = m;
    }
    return top;
}

bool nh_nav_line_clear(const NhNav *nav, NhVec3 a, NhVec3 b)
{
    int fa = lv_floor_of(a.y + 0.3f), fb = lv_floor_of(b.y + 0.3f);
    if (fa != fb)
        return false;
    float len = nh_v3_dist_xz(a, b);
    int steps = (int)(len / 0.25f) + 1;
    const float r = 0.25f;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        NhVec3 p = nh_v3_lerp(a, b, t);
        /* Agent footprint: centre and four points at its radius. */
        static const float ox[5] = { 0, 1, -1, 0, 0 }, oz[5] = { 0, 0, 0, 1, -1 };
        for (int k = 0; k < 5; k++) {
            NhVec3 q = nh_v3(p.x + ox[k] * r, p.y, p.z + oz[k] * r);
            int x = (int)floorf(q.x / (LV_CELL / NAV_RES)), z = (int)floorf(q.z / (LV_CELL / NAV_RES));
            uint8_t fl = nflags(nav, fa, x, z);
            if (!(fl & NAV_WALK) || (fl & NAV_STAIR))
                return false;
        }
        if (i > 0) {
            NhVec3 prev = nh_v3_lerp(a, b, (float)(i - 1) / (float)steps);
            int cx0 = lv_cell_of(prev.x), cz0 = lv_cell_of(prev.z), cx1 = lv_cell_of(p.x), cz1 = lv_cell_of(p.z);
            if (cx0 != cx1 || cz0 != cz1) {
                if (cx0 != cx1 && cz0 != cz1)
                    return false; /* crossing a corner exactly: be safe */
                int s = cx1 > cx0 ? 0 : cx1 < cx0 ? 2 : cz1 > cz0 ? 1 : 3;
                const LvEdge *e = lv_edge_side_c(nav->L, fa, cx0, cz0, s);
                if (!e || (e->type != LV_E_OPEN))
                    return false; /* doorways keep their waypoints */
            }
        }
    }
    return true;
}

static void finish_path(NhNav *nav, int start, int goal, NhVec3 a, NhVec3 b, NhPath *out)
{
    /* Walk parents back from the goal. */
    static int chain[4096];
    int n = 0;
    for (int id = goal; id >= 0 && n < (int)NH_ARRAY_LEN(chain); id = nav->parent[id]) {
        chain[n++] = id;
        if (id == start)
            break;
    }
    /* Raw points (start..goal), inserting doorway centres. */
    static NhVec3 raw[4096];
    int nr = 0;
    raw[nr++] = a;
    for (int i = n - 1; i >= 0; i--) {
        int f, x, z;
        node_xyz(chain[i], &f, &x, &z);
        NhVec3 p = node_pos(nav, f, x, z);
        if (i < n - 1 && nr < 4090) {
            int pf, px, pz;
            node_xyz(chain[i + 1], &pf, &px, &pz);
            int cx0 = px / NAV_RES, cz0 = pz / NAV_RES, cx1 = x / NAV_RES, cz1 = z / NAV_RES;
            if (pf == f && (cx0 != cx1 || cz0 != cz1)) {
                int s = cx1 > cx0 ? 0 : cx1 < cx0 ? 2 : cz1 > cz0 ? 1 : 3;
                const LvEdge *e = lv_edge_side_c(nav->L, f, cx0, cz0, s);
                if (e && (e->type == LV_E_DOOR || e->type == LV_E_ARCH)) {
                    NhVec3 c0 = lv_cell_center(f, cx0, cz0), c1 = lv_cell_center(f, cx1, cz1);
                    NhVec3 mid = nh_v3_scale(nh_v3_add(c0, c1), 0.5f);
                    NhVec3 dir = nh_v3_norm(nh_v3_sub(c1, c0));
                    raw[nr++] = nh_v3_sub(mid, nh_v3_scale(dir, 0.45f));
                    raw[nr++] = nh_v3_add(mid, nh_v3_scale(dir, 0.45f));
                }
            }
        }
        raw[nr++] = p;
    }
    raw[nr++] = b;
    /* String pulling: from each anchor jump to the farthest visible point. */
    out->n = 0;
    int i = 0;
    out->p[out->n++] = raw[0];
    while (i < nr - 1 && out->n < NAV_PATH_MAX) {
        int j = nr - 1;
        while (j > i + 1 && !nh_nav_line_clear(nav, raw[i], raw[j]))
            j--;
        out->p[out->n++] = raw[j];
        i = j;
    }
}

bool nh_nav_path(NhNav *nav, NhVec3 a, NhVec3 b, bool doors, NhPath *out)
{
    NhVec3 sa, sb;
    if (!nh_nav_snap(nav, a, &sa) || !nh_nav_snap(nav, b, &sb))
        return false;
    int start = pos_node(nav, sa), goal = pos_node(nav, sb);
    if (start < 0 || goal < 0)
        return false;
    nav->cache_clock++;
    for (int i = 0; i < NAV_CACHE; i++) {
        if (nav->cache[i].from == start && nav->cache[i].to == goal) {
            nav->cache[i].age = nav->cache_clock;
            *out = nav->cache[i].path;
            out->p[0] = a;
            out->p[out->n - 1] = b;
            nav->expansions = 0;
            return true;
        }
    }
    nav->cur_stamp++;
    nav->heap_n = 0;
    s_goal = goal;
    nav->g[start] = 0.0f;
    nav->stamp[start] = nav->cur_stamp;
    nav->parent[start] = -1;
    nav->closed[start] = 0;
    heap_push(nav, start, NULL);
    nav->expansions = 0;
    bool found = false;
    Link links[16];
    while (nav->heap_n > 0) {
        int cur = heap_pop(nav);
        if (cur == goal) {
            found = true;
            break;
        }
        if (nav->closed[cur] && nav->stamp[cur] == nav->cur_stamp)
            continue;
        nav->closed[cur] = 1;
        if (++nav->expansions > 30000)
            break;
        int nl = neighbours(nav, cur, doors, links);
        for (int k = 0; k < nl; k++) {
            int nb = links[k].to;
            float ng = nav->g[cur] + links[k].cost;
            if (ng < g_of(nav, nb)) {
                nav->g[nb] = ng;
                nav->stamp[nb] = nav->cur_stamp;
                nav->parent[nb] = cur;
                nav->closed[nb] = 0;
                if (nav->heap_n < NAV_NODES) {
                    nav->heap[nav->heap_n] = nb;
                    sift_up(nav, nav->heap_n++);
                }
            }
        }
    }
    if (!found)
        return false;
    finish_path(nav, start, goal, a, b, out);
    /* Cache it (replace the oldest entry). */
    int slot = 0;
    for (int i = 1; i < NAV_CACHE; i++)
        if (nav->cache[i].from < 0 || nav->cache[i].age < nav->cache[slot].age)
            slot = i;
    nav->cache[slot].from = start;
    nav->cache[slot].to = goal;
    nav->cache[slot].age = nav->cache_clock;
    nav->cache[slot].path = *out;
    return true;
}

float nh_path_length(const NhPath *p)
{
    float l = 0.0f;
    for (int i = 1; i < p->n; i++)
        l += nh_v3_dist(p->p[i - 1], p->p[i]);
    return l;
}
