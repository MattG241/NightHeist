#include "nh_portal.h"

#include <math.h>
#include <string.h>

static int portal_room(int r)
{
    return r == LV_ROOM_VOID ? LV_ROOM_OUTSIDE : r;
}

static void add_portal(NhPortalSet *ps, int a, int b, int kind, int door, const NhVec3 c[4], NhVec3 n)
{
    a = portal_room(a);
    b = portal_room(b);
    if (a == b || ps->n >= NH_PORTALS_MAX)
        return;
    if (ps->room_count[a] >= NH_ROOM_PORTALS_MAX || ps->room_count[b] >= NH_ROOM_PORTALS_MAX)
        return;
    NhPortal *p = &ps->p[ps->n];
    p->a = (uint8_t)a;
    p->b = (uint8_t)b;
    p->kind = (uint8_t)kind;
    p->door = (uint8_t)door;
    memcpy(p->c, c, sizeof(p->c));
    p->n = n;
    ps->room_portals[a][ps->room_count[a]++] = (uint16_t)ps->n;
    ps->room_portals[b][ps->room_count[b]++] = (uint16_t)ps->n;
    ps->n++;
}

void nh_portals_build(const Level *L, NhPortalSet *ps)
{
    memset(ps, 0, sizeof(*ps));
    for (int f = 0; f <= L->floors; f++) {
        float base = lv_floor_y(f);
        for (int z = 0; z < L->d; z++) {
            for (int x = 0; x < L->w; x++) {
                for (int side = 0; side < 2; side++) { /* +X and +Z edges */
                    const LvEdge *e = lv_edge_side_c(L, f, x, z, side);
                    if (!e || (e->type != LV_E_DOOR && e->type != LV_E_ARCH && e->type != LV_E_WINDOW))
                        continue;
                    int ra = lv_room_at(L, f, x, z);
                    int rb = lv_neighbor_room(L, f, x, z, side);
                    if (ra == LV_ROOM_VOID && rb == LV_ROOM_VOID)
                        continue;
                    float u0 = 0.5f, u1 = 1.5f, y0 = 0.0f, y1 = LV_DOOR_H;
                    int kind = e->type == LV_E_DOOR ? NH_PORTAL_DOOR : NH_PORTAL_ARCH;
                    if (e->type == LV_E_WINDOW) {
                        u0 = 0.3f;
                        u1 = 1.7f;
                        y0 = LV_WIN_Y0;
                        y1 = LV_WIN_Y1;
                        kind = NH_PORTAL_WINDOW;
                    }
                    NhVec3 c[4], n;
                    if (side == 0) {
                        float bx = (float)(x + 1) * LV_CELL, z0 = (float)z * LV_CELL;
                        c[0] = nh_v3(bx, base + y0, z0 + u0);
                        c[1] = nh_v3(bx, base + y0, z0 + u1);
                        c[2] = nh_v3(bx, base + y1, z0 + u1);
                        c[3] = nh_v3(bx, base + y1, z0 + u0);
                        n = nh_v3(1, 0, 0);
                    } else {
                        float bz = (float)(z + 1) * LV_CELL, x0 = (float)x * LV_CELL;
                        c[0] = nh_v3(x0 + u0, base + y0, bz);
                        c[1] = nh_v3(x0 + u1, base + y0, bz);
                        c[2] = nh_v3(x0 + u1, base + y1, bz);
                        c[3] = nh_v3(x0 + u0, base + y1, bz);
                        n = nh_v3(0, 0, 1);
                    }
                    add_portal(ps, ra, rb, kind, e->type == LV_E_DOOR ? e->ref : 0, c, n);
                }
            }
        }
    }
    /* Stair holes: the lower stair room sees up into the upper room. */
    for (int i = 0; i < L->nstairs; i++) {
        const LvStair *s = &L->stairs[i];
        int lower = lv_room_at(L, s->floor, s->x, s->z);
        int upper = lv_room_at(L, s->floor + 1, s->x, s->z);
        float minx = 1e9f, minz = 1e9f, maxx = -1e9f, maxz = -1e9f;
        static const int dx[4] = { 1, 0, -1, 0 }, dz[4] = { 0, 1, 0, -1 };
        for (int k = 0; k < LV_STAIR_LEN; k++) {
            int cx = s->x + dx[s->dir] * k, cz = s->z + dz[s->dir] * k;
            minx = fminf(minx, (float)cx * LV_CELL);
            maxx = fmaxf(maxx, (float)(cx + 1) * LV_CELL);
            minz = fminf(minz, (float)cz * LV_CELL);
            maxz = fmaxf(maxz, (float)(cz + 1) * LV_CELL);
        }
        float y = lv_floor_y(s->floor + 1);
        NhVec3 c[4] = { nh_v3(minx, y, minz), nh_v3(maxx, y, minz), nh_v3(maxx, y, maxz), nh_v3(minx, y, maxz) };
        add_portal(ps, lower, upper, NH_PORTAL_HOLE, 0, c, nh_v3(0, 1, 0));
    }
}

int nh_room_at_point(const Level *L, NhVec3 p)
{
    int f = lv_floor_of(p.y - 0.3f);
    int r = lv_room_at(L, f, lv_cell_of(p.x), lv_cell_of(p.z));
    if (r == LV_ROOM_VOID && f > 0)
        r = lv_room_at(L, f - 1, lv_cell_of(p.x), lv_cell_of(p.z));
    return r == LV_ROOM_VOID ? LV_ROOM_OUTSIDE : r;
}

typedef struct Ctx {
    const Level *L;
    const NhPortalSet *ps;
    const float *vp;
    NhVec3 eye;
    uint8_t *visible;
    NhVisRect best[LV_ROOMS_MAX];
    int budget;
    int count;
} Ctx;

static bool portal_open(const Level *L, const NhPortal *p)
{
    if (p->kind != NH_PORTAL_DOOR)
        return true;
    const LvDoor *d = &L->doors[p->door - 1];
    return d->open > 0.04f || d->kind == LV_DOOR_GLASS;
}

/* Screen rect of a portal; false if entirely behind the camera. */
static bool project_rect(const float *vp, const NhPortal *p, NhVisRect *r)
{
    r->x0 = r->y0 = 1e9f;
    r->x1 = r->y1 = -1e9f;
    int behind = 0;
    for (int i = 0; i < 4; i++) {
        NhVec3 c = p->c[i];
        float x = vp[0] * c.x + vp[1] * c.y + vp[2] * c.z + vp[3];
        float y = vp[4] * c.x + vp[5] * c.y + vp[6] * c.z + vp[7];
        float w = vp[12] * c.x + vp[13] * c.y + vp[14] * c.z + vp[15];
        if (w < 0.05f) {
            behind++;
            continue;
        }
        x /= w;
        y /= w;
        r->x0 = fminf(r->x0, x);
        r->x1 = fmaxf(r->x1, x);
        r->y0 = fminf(r->y0, y);
        r->y1 = fmaxf(r->y1, y);
    }
    if (behind == 4)
        return false;
    if (behind > 0) {
        /* Straddles the near plane: be conservative. */
        r->x0 = -1;
        r->y0 = -1;
        r->x1 = 1;
        r->y1 = 1;
    }
    return true;
}

static bool rect_clip(NhVisRect *r, const NhVisRect *by)
{
    r->x0 = fmaxf(r->x0, by->x0);
    r->y0 = fmaxf(r->y0, by->y0);
    r->x1 = fminf(r->x1, by->x1);
    r->y1 = fminf(r->y1, by->y1);
    return r->x0 < r->x1 && r->y0 < r->y1;
}

static bool rect_inside(const NhVisRect *a, const NhVisRect *b)
{
    return a->x0 >= b->x0 && a->y0 >= b->y0 && a->x1 <= b->x1 && a->y1 <= b->y1;
}

static void visit(Ctx *c, int room, NhVisRect rect, int via, int depth)
{
    if (c->budget-- <= 0)
        return;
    if (!c->visible[room]) {
        c->visible[room] = 1;
        c->count++;
        c->best[room] = rect;
    } else {
        if (rect_inside(&rect, &c->best[room]))
            return; /* nothing new seen through this path */
        NhVisRect *b = &c->best[room];
        b->x0 = fminf(b->x0, rect.x0);
        b->y0 = fminf(b->y0, rect.y0);
        b->x1 = fmaxf(b->x1, rect.x1);
        b->y1 = fmaxf(b->y1, rect.y1);
    }
    if (depth > 12)
        return;
    for (int i = 0; i < c->ps->room_count[room]; i++) {
        int pi = c->ps->room_portals[room][i];
        if (pi == via)
            continue;
        const NhPortal *p = &c->ps->p[pi];
        if (!portal_open(c->L, p))
            continue;
        int other = p->a == room ? p->b : p->a;
        /* The camera must be on this room's side of the portal plane. */
        NhVec3 n = p->a == room ? p->n : nh_v3_scale(p->n, -1.0f);
        float side = nh_v3_dot(nh_v3_sub(c->eye, p->c[0]), n);
        if (side > 0.25f)
            continue;
        NhVisRect r;
        if (!project_rect(c->vp, p, &r) || !rect_clip(&r, &rect))
            continue;
        visit(c, other, r, pi, depth + 1);
    }
}

int nh_portals_visible(const Level *L, const NhPortalSet *ps, const float viewproj[16], NhVec3 eye, int cam_room,
                       uint8_t *visible)
{
    static Ctx c;
    memset(&c, 0, sizeof(c));
    c.L = L;
    c.ps = ps;
    c.vp = viewproj;
    c.eye = eye;
    c.visible = visible;
    c.budget = 400;
    memset(visible, 0, LV_ROOMS_MAX);
    NhVisRect full = { -1, -1, 1, 1 };
    visit(&c, cam_room, full, -1, 0);
    return c.count;
}
