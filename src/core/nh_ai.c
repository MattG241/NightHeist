#include "nh_ai.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "nh_bake.h"
#include "nh_fixedstep.h"

const AiArchInfo nh_ai_arch[ARCH_COUNT] = {
    /*            name   walk run  range fov  hear gain decay distr search hide radio torch therm civ doors */
    [ARCH_ROOKIE] = { "rookie", 1.3f, 3.6f, 14, 100, 0.9f, 0.8f, 0.22f, 0.9f, 12, 0, 1, 1, 0, 0, 1 },
    [ARCH_GUARD] = { "guard", 1.4f, 4.0f, 18, 110, 1.0f, 1.0f, 0.15f, 0.5f, 20, 0, 1, 1, 0, 0, 1 },
    [ARCH_VETERAN] = { "veteran", 1.4f, 4.2f, 21, 120, 1.2f, 1.25f, 0.10f, 0.2f, 32, 1, 1, 1, 0, 0, 1 },
    [ARCH_LAZY] = { "lazy", 1.1f, 3.4f, 12, 90, 0.8f, 0.7f, 0.30f, 0.8f, 8, 0, 1, 0, 0, 0, 1 },
    [ARCH_HANDLER] = { "handler", 1.3f, 3.8f, 16, 110, 1.0f, 1.0f, 0.15f, 0.4f, 20, 0, 1, 1, 0, 0, 1 },
    [ARCH_CAMOP] = { "camera op", 1.2f, 3.6f, 10, 100, 0.8f, 0.9f, 0.20f, 0.3f, 10, 0, 1, 0, 0, 0, 1 },
    [ARCH_SUPERVISOR] = { "supervisor", 1.3f, 3.8f, 18, 110, 1.0f, 1.1f, 0.12f, 0.3f, 24, 1, 1, 1, 0, 0, 1 },
    [ARCH_ELITE] = { "elite", 1.5f, 4.6f, 21, 120, 1.2f, 1.3f, 0.08f, 0.1f, 40, 1, 1, 0, 1, 0, 1 },
    [ARCH_JANITOR] = { "janitor", 1.1f, 3.8f, 12, 120, 1.1f, 1.2f, 0.20f, 0.6f, 0, 0, 0, 0, 0, 1, 1 },
    [ARCH_POLICE] = { "police", 1.5f, 4.4f, 21, 120, 1.1f, 1.3f, 0.08f, 0.2f, 45, 1, 1, 1, 0, 0, 1 },
    [ARCH_DOG] = { "dog", 1.6f, 5.5f, 10, 200, 1.6f, 1.0f, 0.20f, 0.7f, 25, 0, 0, 0, 0, 0, 0 },
};

const char *const nh_ai_state_names[AS_COUNT] = {
    "patrol", "idle", "suspicious", "investigate", "alert", "search", "return",
    "distracted", "station", "flee", "ko", "carried",
};

#define SUS_SUSPICIOUS 0.30f
#define SUS_INVESTIGATE 0.65f
#define SUS_ALERT 1.0f
#define CATCH_DIST 1.3f
#define GUARD_R 0.3f

static const int k_dx[4] = { 1, 0, -1, 0 };
static const int k_dz[4] = { 0, 1, 0, -1 };

static const AiArchInfo *A(const AiGuard *g) { return &nh_ai_arch[g->arch]; }

static bool calm_state(int s)
{
    return s == AS_PATROL || s == AS_IDLE || s == AS_RETURN || s == AS_STATION || s == AS_DISTRACTED;
}

static bool down_state(int s) { return s == AS_KO || s == AS_CARRIED; }

static float yaw_to(NhVec3 from, NhVec3 to)
{
    return atan2f(-(to.x - from.x), -(to.z - from.z));
}

static float rngf(AiWorld *w) { return nh_rng_float(&w->rng); }

static void emit(AiWorld *w, int type, int guard, int arg, NhVec3 pos)
{
    if (w->nev >= AI_EVENTS_MAX)
        return;
    w->ev[w->nev++] = (AiEvent){ (uint8_t)type, (uint8_t)guard, (uint8_t)arg, pos };
}

static void bark(AiWorld *w, AiGuard *g, int b, bool force)
{
    if (!force && g->bark_cd > 0.0f)
        return;
    g->bark_cd = 3.0f;
    emit(w, EV_BARK, (int)(g - w->g), b, ai_guard_eye(g));
}

static void set_state(AiGuard *g, int s)
{
    if (g->state == s)
        return;
    g->prev_state = g->state;
    g->state = (uint8_t)s;
    g->state_t = 0.0f;
    g->action = ACT_NONE;
    g->asleep = false;
}

/* ---- geometry helpers ----------------------------------------------------- */

NhVec3 ai_guard_eye(const AiGuard *g)
{
    float h = g->arch == ARCH_DOG ? 0.55f : (g->asleep ? 1.1f : 1.65f);
    return nh_v3(g->pos.x, g->pos.y + h, g->pos.z);
}

NhVec3 ai_guard_dir(const AiGuard *g)
{
    return nh_dir_from_yaw_pitch(g->yaw + g->head_yaw, 0.0f);
}

int ai_count_blockers(const Level *L, NhVec3 a, NhVec3 b)
{
    int fa = lv_floor_of(a.y + 0.3f), fb = lv_floor_of(b.y + 0.3f);
    int n = abs(fa - fb) * 2;
    float len = nh_v3_dist_xz(a, b);
    int steps = (int)(len / 0.5f) + 1;
    int cx = lv_cell_of(a.x), cz = lv_cell_of(a.z);
    for (int i = 1; i <= steps && n < 6; i++) {
        NhVec3 p = nh_v3_lerp(a, b, (float)i / (float)steps);
        int nx = lv_cell_of(p.x), nz = lv_cell_of(p.z);
        if (nx == cx && nz == cz)
            continue;
        /* Step through x then z so diagonal moves cross real edges. */
        while ((nx != cx || nz != cz) && n < 6) {
            int s = nx > cx ? 0 : nx < cx ? 2 : nz > cz ? 1 : 3;
            const LvEdge *e = lv_edge_side_c(L, fa, cx, cz, s);
            if (e) {
                if (e->type == LV_E_WALL)
                    n++;
                else if (e->type == LV_E_WINDOW && e->ref == LV_WIN_CLOSED)
                    n++;
                else if (e->type == LV_E_DOOR && e->ref && L->doors[e->ref - 1].open < 0.5f)
                    n++;
            }
            cx += k_dx[s];
            cz += k_dz[s];
        }
    }
    return n;
}

static int room_of(const Level *L, NhVec3 p)
{
    return lv_room_at(L, lv_floor_of(p.y + 0.3f), lv_cell_of(p.x), lv_cell_of(p.z));
}

/* ---- setup --------------------------------------------------------------- */

void ai_init(AiWorld *w, Level *L, NhNav *nav, uint32_t seed)
{
    memset(w, 0, sizeof(*w));
    w->L = L;
    w->nav = nav;
    nh_rng_seed(&w->rng, seed, 0xA1u);
    w->alarm_threshold = 3;
    w->difficulty = 1.0f;
    w->checkin_period = 90.0f;
    w->checkin_t = w->checkin_period;
}

int ai_add_guard(AiWorld *w, int arch, NhVec3 pos, float yaw, const AiPatrolPt *route, int nroute)
{
    if (w->n >= AI_MAX_GUARDS)
        return -1;
    int i = w->n++;
    AiGuard *g = &w->g[i];
    memset(g, 0, sizeof(*g));
    g->active = 1;
    g->arch = (uint8_t)arch;
    g->pos = pos;
    g->last_pos = pos;
    g->yaw = yaw;
    g->partner = -1;
    g->reported_by = -1;
    g->floor = lv_floor_of(pos.y + 0.3f);
    nroute = NH_MIN(nroute, AI_ROUTE_MAX);
    for (int k = 0; k < nroute; k++)
        g->route[k] = route[k];
    g->nroute = (uint8_t)nroute;
    if (nroute == 0) {
        /* Stationary: remember the post. */
        g->route[0] = (AiPatrolPt){ pos, yaw, ACT_LOOK, 6.0f };
        g->nroute = 1;
    }
    g->state = (arch == ARCH_CAMOP || nroute == 0) ? AS_STATION : AS_PATROL;
    g->checkin_due = w->checkin_period;
    return i;
}

/* ---- movement ------------------------------------------------------------- */

static bool set_goal(AiWorld *w, AiGuard *g, NhVec3 goal, bool run)
{
    g->run = run;
    g->arrived = false;
    g->path_failed = false;
    if (g->has_goal && nh_v3_dist(goal, g->goal) < 0.4f && g->path.n > 0) {
        g->goal = goal;
        g->path.p[g->path.n - 1] = goal;
        return true;
    }
    g->goal = goal;
    g->repaths = 0;
    g->stuck_t = 0.0f;
    if (!nh_nav_path(w->nav, g->pos, goal, A(g)->opens_doors, &g->path)) {
        g->has_goal = false;
        g->path_failed = true;
        return false;
    }
    g->has_goal = true;
    g->path_i = 1;
    return true;
}

static void stop(AiGuard *g)
{
    g->has_goal = false;
    g->speed = 0.0f;
}

/* Door on the edge the guard is about to cross, or -1. */
static int door_ahead(const Level *L, NhVec3 pos, NhVec3 dir, int f)
{
    NhVec3 ahead = nh_v3_add(pos, nh_v3_scale(dir, 0.7f));
    int cx = lv_cell_of(pos.x), cz = lv_cell_of(pos.z), nx = lv_cell_of(ahead.x), nz = lv_cell_of(ahead.z);
    if (cx == nx && cz == nz)
        return -1;
    int s = nx > cx ? 0 : nx < cx ? 2 : nz > cz ? 1 : 3;
    const LvEdge *e = lv_edge_side_c(L, f, cx, cz, s);
    if (e && e->type == LV_E_DOOR && e->ref)
        return e->ref - 1;
    return -1;
}

static void move_guard(AiWorld *w, AiGuard *g, float dt)
{
    Level *L = w->L;
    const AiArchInfo *a = A(g);
    g->speed = 0.0f;
    /* Close a door behind us once clear of it. */
    if (g->door_close) {
        LvDoor *d = &L->doors[g->door_close - 1];
        if (nh_v3_dist_xz(g->pos, g->door_close_pos) > 1.4f) {
            d->target = 0.0f;
            g->door_close = 0;
        }
    }
    if (!g->has_goal)
        return;
    if (g->path_i >= g->path.n) {
        g->arrived = true;
        g->has_goal = false;
        return;
    }
    NhVec3 tgt = g->path.p[g->path_i];
    float d = nh_v3_dist_xz(g->pos, tgt);
    float reach = g->path_i == g->path.n - 1 ? 0.3f : 0.4f;
    if (d < reach) {
        g->path_i++;
        if (g->path_i >= g->path.n) {
            g->arrived = true;
            g->has_goal = false;
        }
        return;
    }
    float want = yaw_to(g->pos, tgt);
    float turn = (g->run ? 9.0f : 5.0f) * dt;
    g->yaw = nh_angle_approach(g->yaw, want, turn);
    float err = fabsf(nh_wrap_angle(want - g->yaw));
    float spd = g->run ? a->run : a->walk;
    if (w->alarm && !g->run)
        spd *= 1.15f;
    spd *= err > 1.2f ? 0.15f : 1.0f - err * 0.5f;
    NhVec3 dir = nh_dir_from_yaw_pitch(g->yaw, 0.0f);
    /* Doors: open, wait, walk through, close behind. */
    int di = door_ahead(L, g->pos, dir, g->floor);
    if (di >= 0) {
        LvDoor *door = &L->doors[di];
        if (door->open < 0.8f) {
            if (!a->opens_doors) {
                stop(g);
                g->path_failed = true;
                return;
            }
            if (door->target < 1.0f) {
                bool was_closed = door->open < 0.05f;
                door->target = 1.0f;
                emit(w, EV_DOOR_OPENED, (int)(g - w->g), (uint8_t)di, g->pos);
                if (was_closed && calm_state(g->state)) {
                    g->door_close = (int16_t)(di + 1);
                    g->door_close_pos = lv_cell_center(door->floor, door->x, door->z);
                    if (door->axis == 0)
                        g->door_close_pos.x = (float)door->x * LV_CELL;
                    else
                        g->door_close_pos.z = (float)door->z * LV_CELL;
                }
            }
            return; /* wait for it to swing open */
        }
    }
    float step = NH_MIN(spd * dt, d);
    NhVec3 np = nh_v3_add(g->pos, nh_v3_scale(dir, step));
    lv_collide_cylinder(L, &np, GUARD_R, np.y + 0.45f, np.y + 1.7f);
    float h = lv_ground_height(L, np.x, np.z, g->pos.y + 0.1f);
    if (h > -999.0f)
        np.y = h;
    g->speed = nh_v3_dist_xz(np, g->pos) / dt;
    g->pos = np;
    g->floor = lv_floor_of(g->pos.y + 0.3f);
    /* Stuck: repath, then give up. */
    g->stuck_t += dt;
    if (g->stuck_t > 1.0f) {
        if (nh_v3_dist_xz(g->pos, g->last_pos) < 0.15f) {
            if (++g->repaths > 3) {
                stop(g);
                g->path_failed = true;
            } else if (nh_nav_path(w->nav, g->pos, g->goal, a->opens_doors, &g->path)) {
                g->path_i = 1;
            }
        }
        g->last_pos = g->pos;
        g->stuck_t = 0.0f;
    }
}

/* ---- perception ----------------------------------------------------------- */

float ai_visibility(const AiWorld *w, const AiGuard *g, const AiPlayerView *pv, float *dist)
{
    *dist = 1e9f;
    if (pv->in_vent || pv->hiding_prop >= 0 || pv->cuffed || g->asleep || down_state(g->state))
        return 0.0f;
    const AiArchInfo *a = A(g);
    NhVec3 eye = ai_guard_eye(g);
    NhVec3 chest = nh_v3(pv->pos.x, pv->pos.y + pv->height * 0.62f, pv->pos.z);
    NhVec3 to = nh_v3_sub(chest, eye);
    float d = nh_v3_len(to);
    *dist = d;
    float light = NH_CLAMP(pv->light, 0.0f, 1.0f);
    float range = a->view_range * (0.22f + 0.78f * light);
    if (a->thermal)
        range = NH_MAX(range, 12.0f);
    if (g->state == AS_ALERT || g->state == AS_SEARCH)
        range *= 1.25f;
    range *= 0.85f + 0.15f * w->difficulty;
    if (d > range || d < 1e-3f)
        return d < 1e-3f ? 1.0f : 0.0f;
    if (fabsf(to.y) > d * 0.8f)
        return 0.0f; /* straight above or below */
    NhVec3 fwd = ai_guard_dir(g);
    NhVec3 toh = nh_v3_norm(nh_v3(to.x, 0.0f, to.z));
    float c = nh_v3_dot(fwd, toh);
    float half = a->fov_deg * 0.5f * NH_PI / 180.0f;
    float ang = acosf(NH_CLAMP(c, -1.0f, 1.0f));
    float cone = 1.0f;
    if (ang > half) {
        if (d < 5.0f && ang < half + 35.0f * NH_PI / 180.0f)
            cone = 0.35f; /* peripheral: movement up close */
        else
            return 0.0f;
    }
    /* Three sample points: head, chest, feet. */
    NhVec3 pts[3] = { pv->head, chest, nh_v3(pv->pos.x, pv->pos.y + 0.25f, pv->pos.z) };
    int seen = 0;
    for (int i = 0; i < 3; i++) {
        float t;
        if (!lv_raycast(w->L, eye, pts[i], &t) && !ai_smoke_blocks(w, eye, pts[i]))
            seen++;
    }
    return cone * (float)seen / 3.0f;
}

bool ai_smoke_blocks(const AiWorld *w, NhVec3 a, NhVec3 b)
{
    for (int i = 0; i < (int)NH_ARRAY_LEN(w->smoke); i++) {
        if (w->smoke[i].t <= 0.0f)
            continue;
        /* Segment / sphere: distance from the centre to the segment. */
        NhVec3 ab = nh_v3_sub(b, a), ac = nh_v3_sub(w->smoke[i].pos, a);
        float len2 = nh_v3_dot(ab, ab);
        float t = len2 > 1e-6f ? NH_CLAMP(nh_v3_dot(ac, ab) / len2, 0.0f, 1.0f) : 0.0f;
        NhVec3 q = nh_v3_add(a, nh_v3_scale(ab, t));
        float r = w->smoke[i].radius * NH_MIN(1.0f, w->smoke[i].t / 3.0f + 0.3f);
        if (nh_v3_dist(q, w->smoke[i].pos) < r)
            return true;
    }
    return false;
}

float ai_point_visibility(const AiWorld *w, NhVec3 eye, NhVec3 fwd, float fov_deg, float range, const AiPlayerView *pv,
                          float *dist)
{
    *dist = 1e9f;
    if (pv->in_vent || pv->hiding_prop >= 0 || pv->cuffed)
        return 0.0f;
    NhVec3 chest = nh_v3(pv->pos.x, pv->pos.y + pv->height * 0.62f, pv->pos.z);
    NhVec3 to = nh_v3_sub(chest, eye);
    float d = nh_v3_len(to);
    *dist = d;
    float light = NH_CLAMP(pv->light, 0.0f, 1.0f);
    float r = range * (0.22f + 0.78f * light);
    if (d > r || d < 1e-3f)
        return 0.0f;
    float c = nh_v3_dot(nh_v3_scale(to, 1.0f / d), fwd);
    if (c < cosf(fov_deg * 0.5f * NH_PI / 180.0f))
        return 0.0f;
    NhVec3 pts[3] = { pv->head, chest, nh_v3(pv->pos.x, pv->pos.y + 0.25f, pv->pos.z) };
    int seen = 0;
    for (int i = 0; i < 3; i++) {
        float t;
        if (!lv_raycast(w->L, eye, pts[i], &t) && !ai_smoke_blocks(w, eye, pts[i]))
            seen++;
    }
    return (float)seen / 3.0f;
}

void ai_add_smoke(AiWorld *w, NhVec3 pos, float radius, float seconds)
{
    int slot = 0;
    for (int i = 1; i < (int)NH_ARRAY_LEN(w->smoke); i++)
        if (w->smoke[i].t < w->smoke[slot].t)
            slot = i;
    w->smoke[slot].pos = pos;
    w->smoke[slot].radius = radius;
    w->smoke[slot].t = seconds;
}

float ai_gain_rate(const AiWorld *w, const AiGuard *g, const AiPlayerView *pv, float vis, float d)
{
    if (vis <= 0.0f)
        return 0.0f;
    const AiArchInfo *a = A(g);
    float light = NH_CLAMP(pv->light, 0.0f, 1.0f);
    float range = a->view_range * (0.22f + 0.78f * light);
    if (a->thermal)
        range = NH_MAX(range, 12.0f);
    float df = 1.0f - (d / range) * (d / range);
    df = NH_MAX(df, 0.08f);
    float lt = a->thermal ? 1.0f : 0.35f + 0.65f * light;
    float stance = pv->crouched ? 0.6f : 1.0f;
    float motion = pv->speed < 0.3f ? 0.6f : pv->speed < 2.5f ? 1.0f : 1.6f;
    float rate = 1.2f * vis * df * lt * stance * motion * a->gain * (1.0f + g->awareness * 0.6f) * w->difficulty;
    if (d < 2.5f)
        rate *= 2.5f; /* nobody misses someone at arm's length */
    if (pv->disguised) {
        int room = room_of(w->L, pv->pos);
        bool secure = room >= LV_ROOM_FIRST && (w->L->rooms[room].flags & LV_R_SECURE);
        if (!secure && d > 3.0f)
            rate *= 0.12f;
    }
    if (w->alarm)
        rate *= 1.3f;
    return rate;
}

/* Rooms within `depth` door/arch steps of `room`, including it. */
static int nearby_rooms(const Level *L, int room, int depth, int *out, int max)
{
    int n = 0;
    out[n++] = room;
    for (int head = 0, lvl = 0; head < n && lvl < depth; lvl++) {
        int end = n;
        for (; head < end; head++) {
            const LvRoom *r = &L->rooms[out[head]];
            for (int z = r->z0; z <= r->z1; z++)
                for (int x = r->x0; x <= r->x1; x++)
                    for (int s = 0; s < 4; s++) {
                        const LvEdge *e = lv_edge_side_c(L, r->floor, x, z, s);
                        if (!e || (e->type != LV_E_DOOR && e->type != LV_E_ARCH))
                            continue;
                        int nr = lv_neighbor_room(L, r->floor, x, z, s);
                        if (nr < LV_ROOM_FIRST)
                            continue;
                        bool dup = false;
                        for (int k = 0; k < n; k++)
                            dup |= out[k] == nr;
                        if (!dup && n < max)
                            out[n++] = nr;
                    }
        }
    }
    return n;
}

static void build_search(AiWorld *w, AiGuard *g, NhVec3 center, bool wide)
{
    const Level *L = w->L;
    const AiArchInfo *a = A(g);
    g->nsearch = 0;
    g->search_i = 0;
    NhVec3 cand[40];
    int16_t cprop[40];
    int nc = 0;
    cand[nc] = center;
    cprop[nc++] = -1;
    /* Hiding spots right next to where the player vanished get checked first. */
    for (int i = 0; i < L->nprops && nc < 40; i++) {
        const LvProp *p = &L->props[i];
        if (!(p->flags & LV_P_HIDE) || (p->flags & LV_P_HIDDEN))
            continue;
        float d = nh_v3_dist(nh_v3(p->x, p->y, p->z), center);
        if (d < 2.2f || (a->checks_hiding && d < 9.0f)) {
            NhVec3 s;
            if (nh_nav_snap(w->nav, nh_v3(p->x, lv_floor_y(p->floor), p->z), &s)) {
                cand[nc] = s;
                cprop[nc++] = (int16_t)i;
            }
        }
    }
    int room = room_of(L, center);
    if (room >= LV_ROOM_FIRST) {
        int rooms[8];
        int nr = nearby_rooms(L, room, wide ? 2 : 1, rooms, wide ? 6 : 4);
        for (int k = 0; k < nr; k++) {
            const LvRoom *r = &L->rooms[rooms[k]];
            int pts = (k == 0 ? 3 : 1) + (wide ? 1 : 0);
            for (int j = 0; j < pts && nc < 40; j++) {
                int cx = r->x0 + (int)nh_rng_range(&w->rng, (uint32_t)(r->x1 - r->x0 + 1));
                int cz = r->z0 + (int)nh_rng_range(&w->rng, (uint32_t)(r->z1 - r->z0 + 1));
                NhVec3 p = lv_cell_center(r->floor, cx, cz);
                p.x += (rngf(w) - 0.5f) * 1.2f;
                p.z += (rngf(w) - 0.5f) * 1.2f;
                NhVec3 s;
                if (nh_nav_snap(w->nav, p, &s)) {
                    cand[nc] = s;
                    cprop[nc++] = -1;
                }
            }
        }
    }
    /* Greedy nearest-neighbour ordering from the centre (hiding spots near the
     * centre first because they sit closest). */
    bool used[40] = { false };
    NhVec3 at = g->pos;
    while (g->nsearch < AI_SEARCH_MAX) {
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < nc; i++) {
            if (used[i])
                continue;
            float d = nh_v3_dist(at, cand[i]);
            if (g->nsearch == 0 && i == 0)
                d = -1.0f; /* always the last known position first */
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best < 0)
            break;
        used[best] = true;
        g->search[g->nsearch] = cand[best];
        g->search_prop[g->nsearch] = cprop[best];
        g->nsearch++;
        at = cand[best];
    }
}

static void start_search(AiWorld *w, AiGuard *g, NhVec3 center, bool wide, bool run)
{
    set_state(g, AS_SEARCH);
    build_search(w, g, center, wide);
    g->search_t = A(g)->search_time * (0.85f + 0.3f * rngf(w)) * (wide ? 1.3f : 1.0f);
    if (g->search_t < 6.0f)
        g->search_t = 6.0f;
    g->wait_t = 0.0f;
    if (g->nsearch)
        set_goal(w, g, g->search[0], run);
}

static void start_investigate(AiWorld *w, AiGuard *g, NhVec3 where);

static void go_alert(AiWorld *w, AiGuard *g, NhVec3 where, bool saw)
{
    int gi = (int)(g - w->g);
    bool was = g->state == AS_ALERT;
    set_state(g, AS_ALERT);
    g->suspicion = SUS_ALERT;
    g->lkp = where;
    g->lkp_age = 0.0f;
    g->awareness = NH_MIN(1.0f, g->awareness + 0.35f);
    if (!was) {
        if (A(g)->civilian) {
            set_state(g, AS_FLEE);
            bark(w, g, BARK_SCREAM, true);
            return;
        }
        bark(w, g, g->arch == ARCH_DOG ? BARK_DOG : BARK_THERE, true);
        if (saw)
            emit(w, EV_SPOTTED, gi, 0, where);
        if (A(g)->radio && g->radio_t <= 0.0f)
            g->radio_t = 1.5f;
        if (g->arch == ARCH_DOG) {
            emit(w, EV_DOG_BARK, gi, 0, g->pos);
            /* The handler comes over to see what the dog has found. */
            if (g->partner >= 0) {
                AiGuard *h = &w->g[g->partner];
                if (!down_state(h->state) && (calm_state(h->state) || h->state == AS_SUSPICIOUS)) {
                    h->suspicion = NH_MAX(h->suspicion, 0.5f);
                    start_investigate(w, h, where);
                }
            }
        }
    }
    set_goal(w, g, where, true);
}

static void start_investigate(AiWorld *w, AiGuard *g, NhVec3 where)
{
    set_state(g, AS_INVESTIGATE);
    g->stim = where;
    bark(w, g, BARK_WHO, false);
    set_goal(w, g, where, false);
    g->wait_t = 0.0f;
}

static void hear(AiWorld *w, AiGuard *g, const NhNoise *n, float dt_p)
{
    const AiArchInfo *a = A(g);
    if (down_state(g->state))
        return;
    float d = nh_v3_dist(g->pos, n->pos);
    int bl = ai_count_blockers(w->L, nh_v3_add(g->pos, nh_v3(0, 1.0f, 0)), n->pos);
    float r = n->radius * a->hearing * powf(0.45f, (float)bl) * (g->asleep ? 0.5f : 1.0f);
    if (g->state == AS_ALERT || g->state == AS_SEARCH)
        r *= 1.2f;
    if (d > r)
        return;
    float close = 1.0f - d / r;
    NhVec3 p = n->pos;
    /* Sound only gives a rough direction at range. */
    float jit = d * 0.12f;
    p.x += (rngf(w) - 0.5f) * jit;
    p.z += (rngf(w) - 0.5f) * jit;
    if (g->asleep && close > 0.3f)
        g->asleep = false;
    if (!n->from_player) {
        /* Distraction: go look without (much) suspicion. */
        if (calm_state(g->state) || g->state == AS_SUSPICIOUS) {
            g->suspicion += 0.25f * (1.0f - a->distractible);
            set_state(g, AS_DISTRACTED);
            g->stim = p;
            g->wait_t = 4.0f + 2.0f * rngf(w);
            bark(w, g, BARK_NOISE, false);
            if (!set_goal(w, g, p, false))
                g->yaw = yaw_to(g->pos, p);
        }
        return;
    }
    float amount;
    switch (n->kind) {
    case NH_NOISE_FOOTSTEP:
        amount = (0.18f * close + 0.05f) * (dt_p * 8.0f);
        break;
    case NH_NOISE_GLASS:
    case NH_NOISE_TAKEDOWN:
    case NH_NOISE_ALARM:
        amount = 0.75f;
        break;
    default:
        amount = 0.25f + 0.35f * close;
        break;
    }
    g->suspicion += amount;
    g->stim = p;
    if (g->state == AS_ALERT || g->state == AS_SEARCH) {
        g->lkp = p;
        g->lkp_age = 0.0f;
    }
}

static void see_anomalies(AiWorld *w, AiGuard *g, int gi)
{
    if (g->asleep || down_state(g->state) || g->arch == ARCH_DOG)
        return;
    NhVec3 eye = ai_guard_eye(g);
    NhVec3 fwd = ai_guard_dir(g);
    for (int i = 0; i < w->nan; i++) {
        AiAnomaly *an = &w->an[i];
        if (!an->active || (an->seen_mask & (1u << gi)))
            continue;
        NhVec3 to = nh_v3_sub(an->pos, eye);
        float d = nh_v3_len(to);
        if (d > 11.0f)
            continue;
        if (d > 1.5f && nh_v3_dot(nh_v3_norm(nh_v3(to.x, 0, to.z)), fwd) < 0.45f)
            continue;
        float t;
        if (lv_raycast(w->L, eye, an->pos, &t) || ai_smoke_blocks(w, eye, an->pos))
            continue;
        an->seen_mask |= 1u << gi;
        if (A(g)->civilian) {
            if (an->kind == AN_BODY)
                go_alert(w, g, an->pos, false);
            continue;
        }
        switch (an->kind) {
        case AN_BODY:
            bark(w, g, BARK_MAN_DOWN, true);
            g->awareness = 1.0f;
            g->lkp = an->pos;
            g->radio_t = 1.0f;
            start_search(w, g, an->pos, true, true);
            g->suspicion = 0.9f;
            return;
        case AN_LOOT_MISSING:
        case AN_SAFE_OPEN:
            bark(w, g, BARK_LOOT, true);
            g->awareness = NH_MIN(1.0f, g->awareness + 0.5f);
            g->radio_t = 1.5f;
            start_search(w, g, an->pos, true, false);
            g->suspicion = 0.8f;
            return;
        default:
            if (!calm_state(g->state) && g->state != AS_SUSPICIOUS)
                continue;
            bark(w, g,
                 an->kind == AN_DOOR_OPEN     ? BARK_DOOR
                 : an->kind == AN_LIGHTS_OFF  ? BARK_LIGHTS
                 : an->kind == AN_CAMERA_OFF  ? BARK_CAMERA
                                              : BARK_HUH,
                 true);
            g->suspicion = NH_MAX(g->suspicion, an->kind == AN_GLASS_BROKEN ? 0.7f : 0.45f);
            g->awareness = NH_MIN(1.0f, g->awareness + 0.15f);
            start_investigate(w, g, an->pos);
            g->anomaly = (int16_t)(i + 1);
            return;
        }
    }
}

/* ---- per-state behaviour ------------------------------------------------- */

static void patrol_next(AiWorld *w, AiGuard *g)
{
    if (g->nroute <= 1)
        return;
    g->route_i = (uint8_t)((g->route_i + 1) % g->nroute);
    if (g->arch == ARCH_LAZY && rngf(w) < 0.3f)
        g->route_i = (uint8_t)((g->route_i + 1) % g->nroute);
}

static void do_patrol(AiWorld *w, AiGuard *g, float dt)
{
    AiPatrolPt *pt = &g->route[g->route_i];
    if (g->state == AS_IDLE) {
        g->wait_t -= dt;
        g->yaw = nh_angle_approach(g->yaw, pt->yaw, 2.0f * dt);
        if (g->action == ACT_LOOK)
            g->head_yaw = sinf(g->state_t * 1.1f) * 0.9f;
        if (g->action == ACT_NAP)
            g->asleep = true;
        if (g->wait_t <= 0.0f) {
            g->asleep = false;
            patrol_next(w, g);
            set_state(g, AS_PATROL);
        }
        return;
    }
    if (!g->has_goal && !g->arrived) {
        if (!set_goal(w, g, pt->pos, false)) {
            patrol_next(w, g);
            return;
        }
    }
    if (g->arrived) {
        g->arrived = false;
        set_state(g, AS_IDLE);
        g->action = pt->action;
        g->wait_t = pt->wait * (0.8f + 0.4f * rngf(w));
        if (g->action == ACT_CHAT && rngf(w) < 0.5f)
            bark(w, g, BARK_CHAT, false);
    }
}

static void do_station(AiWorld *w, AiGuard *g, float dt)
{
    AiPatrolPt *pt = &g->route[0];
    if (nh_v3_dist_xz(g->pos, pt->pos) > 0.6f) {
        if (!g->has_goal)
            set_goal(w, g, pt->pos, false);
        return;
    }
    stop(g);
    g->yaw = nh_angle_approach(g->yaw, pt->yaw, 2.0f * dt);
    if (g->arch != ARCH_CAMOP)
        g->head_yaw = sinf(g->state_t * 0.5f) * 0.8f;
}

static void return_to_route(AiWorld *w, AiGuard *g)
{
    set_state(g, AS_RETURN);
    g->suspicion = NH_MIN(g->suspicion, 0.2f);
    g->anomaly = 0;
    int ri = g->nroute > 1 ? g->route_i : 0;
    set_goal(w, g, g->route[ri].pos, false);
}

static void do_search(AiWorld *w, AiGuard *g, const AiPlayerView *pv, float dt)
{
    g->search_t -= dt;
    if (g->search_t <= 0.0f || g->search_i >= g->nsearch) {
        bark(w, g, BARK_GIVEUP, false);
        g->awareness = NH_MIN(1.0f, g->awareness + 0.2f);
        return_to_route(w, g);
        return;
    }
    if (g->wait_t > 0.0f) {
        g->wait_t -= dt;
        g->head_yaw = sinf(g->state_t * 1.6f) * 1.0f;
        if (g->wait_t <= 0.0f) {
            g->search_i++;
            g->head_yaw = 0.0f;
            if (g->search_i < g->nsearch)
                set_goal(w, g, g->search[g->search_i], false);
        }
        return;
    }
    if (g->arrived || g->path_failed || (!g->has_goal && g->search_i < g->nsearch)) {
        bool arrived = g->arrived;
        g->arrived = false;
        if (!arrived && !g->path_failed) {
            set_goal(w, g, g->search[g->search_i], false);
            return;
        }
        g->path_failed = false;
        int prop = g->search_prop[g->search_i];
        if (arrived && prop >= 0) {
            const LvProp *p = &w->L->props[prop];
            g->yaw = yaw_to(g->pos, nh_v3(p->x, p->y, p->z));
            for (int v = 0; v < w->nviews; v++) {
                const AiPlayerView *hv = &w->views[v];
                if (hv->hiding_prop == prop) {
                    g->target = (uint8_t)v;
                    emit(w, EV_FOUND_HIDING, (int)(g - w->g), v, hv->pos);
                    bark(w, g, BARK_FOUND, true);
                    go_alert(w, g, hv->pos, true);
                    return;
                }
            }
            (void)pv;
        }
        g->wait_t = prop >= 0 ? 1.2f : 1.4f + rngf(w) * 1.2f;
    }
}

static void do_flee(AiWorld *w, AiGuard *g, float dt)
{
    (void)dt;
    /* Run to the nearest guard who can do something about it. */
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < w->n; i++) {
        AiGuard *o = &w->g[i];
        if (!o->active || o == g || A(o)->civilian || o->arch == ARCH_DOG || down_state(o->state))
            continue;
        float d = nh_v3_dist(o->pos, g->pos);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best < 0) {
        if (g->state_t > 8.0f)
            ai_raise_alarm(w, g->lkp);
        return;
    }
    AiGuard *o = &w->g[best];
    if (bd < 2.0f) {
        /* Report: the guard goes alert at the last known position. */
        go_alert(w, o, g->lkp, false);
        stop(g);
        set_state(g, AS_STATION);
        g->route[0] = (AiPatrolPt){ g->pos, g->yaw, ACT_WAIT, 0 };
        return;
    }
    if (!g->has_goal || g->repath_t <= 0.0f) {
        set_goal(w, g, o->pos, true);
        g->repath_t = 1.0f;
    }
}

static void do_dog_follow(AiWorld *w, AiGuard *g, float dt)
{
    AiGuard *h = g->partner >= 0 ? &w->g[g->partner] : NULL;
    if (!h || down_state(h->state)) {
        do_station(w, g, dt);
        return;
    }
    NhVec3 back = nh_dir_from_yaw_pitch(h->yaw, 0.0f);
    NhVec3 spot = nh_v3_add(h->pos, nh_v3(-back.x * 1.1f + back.z * 0.6f, 0, -back.z * 1.1f - back.x * 0.6f));
    float d = nh_v3_dist_xz(g->pos, spot);
    if (d > 0.7f) {
        if (!g->has_goal || g->repath_t <= 0.0f) {
            set_goal(w, g, spot, d > 4.0f);
            g->repath_t = 0.6f;
        }
    } else {
        stop(g);
        g->yaw = nh_angle_approach(g->yaw, h->yaw, 3.0f * dt);
    }
}

static void radio_call(AiWorld *w, int gi)
{
    AiGuard *src = &w->g[gi];
    emit(w, EV_RADIO, gi, BARK_RADIO_SPOTTED, src->pos);
    w->alerts++;
    int flank = 0;
    /* The two nearest responders run; everyone else walks over. */
    float near[2] = { 1e9f, 1e9f };
    for (int i = 0; i < w->n; i++) {
        AiGuard *o = &w->g[i];
        if (i == gi || !o->active || down_state(o->state) || !A(o)->radio || o->arch == ARCH_CAMOP)
            continue;
        float d = nh_v3_dist(o->pos, src->lkp);
        if (d < near[0]) {
            near[1] = near[0];
            near[0] = d;
        } else if (d < near[1])
            near[1] = d;
    }
    for (int i = 0; i < w->n; i++) {
        AiGuard *o = &w->g[i];
        if (i == gi || !o->active || down_state(o->state) || !A(o)->radio)
            continue;
        if (o->arch == ARCH_CAMOP)
            continue; /* stays on the cameras */
        if (o->state == AS_ALERT)
            continue;
        o->awareness = NH_MIN(1.0f, o->awareness + 0.3f);
        o->asleep = false;
        NhVec3 c = src->lkp;
        if (o->arch == ARCH_ELITE || o->arch == ARCH_POLICE) {
            /* Flank: approach from the side instead of following the leader. */
            NhVec3 dir = nh_v3_norm(nh_v3_sub(src->lkp, src->pos));
            float side = (flank++ & 1) ? 4.0f : -4.0f;
            NhVec3 off = nh_v3_add(c, nh_v3(-dir.z * side, 0, dir.x * side));
            NhVec3 s;
            if (nh_nav_snap(w->nav, off, &s))
                c = s;
        }
        o->lkp = src->lkp;
        start_search(w, o, c, true, nh_v3_dist(o->pos, src->lkp) <= near[1]);
        o->suspicion = NH_MAX(o->suspicion, 0.7f);
    }
    if (w->alerts >= w->alarm_threshold)
        ai_raise_alarm(w, src->lkp);
}

static void supervisor_checkin(AiWorld *w, float dt)
{
    w->checkin_t -= dt;
    if (w->checkin_t > 0.0f)
        return;
    w->checkin_t = w->checkin_period;
    int sup = -1;
    for (int i = 0; i < w->n; i++)
        if (w->g[i].active && w->g[i].arch == ARCH_SUPERVISOR && !down_state(w->g[i].state)) {
            sup = i;
            break;
        }
    if (sup < 0)
        return;
    AiGuard *s = &w->g[sup];
    bark(w, s, BARK_CHECKIN, true);
    int missing = -1;
    for (int i = 0; i < w->n; i++) {
        AiGuard *o = &w->g[i];
        if (i == sup || !o->active || !A(o)->radio)
            continue;
        if (down_state(o->state)) {
            if (!o->missed_checkins++)
                missing = i;
        }
    }
    if (missing >= 0) {
        AiGuard *m = &w->g[missing];
        emit(w, EV_CHECKIN_MISSED, sup, (uint8_t)missing, m->pos);
        w->alerts++;
        s->awareness = 1.0f;
        start_search(w, s, m->route[m->route_i].pos, true, true);
        if (w->alerts >= w->alarm_threshold)
            ai_raise_alarm(w, m->pos);
    } else {
        for (int i = 0; i < w->n; i++)
            if (i != sup && w->g[i].active && A(&w->g[i])->radio && !down_state(w->g[i].state)) {
                bark(w, &w->g[i], BARK_CHECKIN_OK, true);
                break;
            }
    }
}

static void pick_anim(AiGuard *g)
{
    int an;
    if (g->state == AS_KO)
        an = ANIM_KO;
    else if (g->state == AS_CARRIED)
        an = ANIM_DRAGGED;
    else if (g->asleep)
        an = ANIM_NAP;
    else if (g->speed > 0.2f)
        an = g->run && g->speed > 2.2f ? ANIM_RUN : (g->torch_on ? ANIM_WALK_TORCH : ANIM_WALK);
    else if (g->radio_t > 0.0f)
        an = ANIM_RADIO;
    else
        switch (g->state) {
        case AS_SUSPICIOUS:
            an = ANIM_SUSPICIOUS;
            break;
        case AS_ALERT:
            an = ANIM_ALERT;
            break;
        case AS_SEARCH:
        case AS_INVESTIGATE:
        case AS_DISTRACTED:
            an = ANIM_SEARCH;
            break;
        case AS_IDLE:
            an = g->action == ACT_LOOK    ? ANIM_LOOK
                 : g->action == ACT_SMOKE ? ANIM_SMOKE
                 : g->action == ACT_CHAT  ? ANIM_CHAT
                 : g->action == ACT_RADIO ? ANIM_RADIO
                                          : ANIM_IDLE;
            break;
        case AS_STATION:
            an = g->arch == ARCH_CAMOP ? ANIM_IDLE : ANIM_LOOK;
            break;
        default:
            an = ANIM_IDLE;
            break;
        }
    g->anim = (uint8_t)an;
}

static void guard_tick(AiWorld *w, int gi, const AiPlayerView *pv, float dt)
{
    AiGuard *g = &w->g[gi];
    const AiArchInfo *a = A(g);
    g->state_t += dt;
    g->bark_cd = NH_MAX(0.0f, g->bark_cd - dt);
    g->repath_t -= dt;
    if (down_state(g->state)) {
        g->speed = 0.0f;
        g->torch_on = false;
        pick_anim(g);
        return;
    }
    /* Perception, staggered: each guard thinks every other tick. */
    bool think = ((w->tick + (uint32_t)gi) & 1u) == 0;
    if (think) {
        float dtp = dt * 2.0f;
        float d;
        float vis = ai_visibility(w, g, pv, &d);
        g->vis = vis;
        float gain = ai_gain_rate(w, g, pv, vis, d);
        /* Dogs smell you whatever the light. */
        if (g->arch == ARCH_DOG && !pv->in_vent) {
            float sd = nh_v3_dist(g->pos, pv->pos);
            int bl = sd < 6.0f ? ai_count_blockers(w->L, g->pos, pv->pos) : 9;
            float reach = bl == 0 ? 6.0f : 3.0f; /* under a door, only up close */
            if (bl <= 1 && sd < reach)
                gain = NH_MAX(gain, 0.8f * (1.0f - sd / reach) + 0.2f);
        }
        if (gain > 0.0f) {
            g->suspicion += gain * dtp;
            g->seen_t = 0.0f;
            g->stim = pv->pos;
            if (g->suspicion >= SUS_INVESTIGATE || g->state == AS_ALERT || g->state == AS_SEARCH) {
                g->lkp = pv->pos;
                g->lkp_age = 0.0f;
                g->saw_player = true;
            }
        } else {
            g->seen_t += dtp;
            g->lkp_age += dtp;
            if (calm_state(g->state) || g->state == AS_SUSPICIOUS)
                g->suspicion = NH_MAX(0.0f, g->suspicion - a->decay * dtp);
        }
        /* Torch beams sweeping across a wall in front of them (every player's). */
        for (int v = 0; v < w->nviews; v++) {
            const AiPlayerView *bv = &w->views[v];
            if (!bv->torch_on || !bv->beam_valid || bv->cuffed || g->asleep || g->arch == ARCH_DOG)
                continue;
            NhVec3 eye = ai_guard_eye(g);
            NhVec3 to = nh_v3_sub(bv->beam_hit, eye);
            float bd = nh_v3_len(to);
            if (bd < 22.0f && bd > 0.5f &&
                nh_v3_dot(nh_v3_norm(nh_v3(to.x, 0, to.z)), ai_guard_dir(g)) > cosf(a->fov_deg * 0.5f * NH_PI / 180.0f)) {
                NhVec3 back = nh_v3_sub(bv->beam_hit, nh_v3_scale(to, 0.15f / bd));
                float t;
                if (!lv_raycast(w->L, eye, back, &t)) {
                    g->suspicion += 0.45f * a->gain * w->difficulty * dtp * (1.0f - bd / 22.0f + 0.3f);
                    if (gain <= 0.0f)
                        g->stim = bv->beam_hit;
                }
            }
        }
        for (int v = 0; v < w->nviews; v++)
            for (int i = 0; i < w->views[v].nnoise; i++)
                hear(w, g, &w->views[v].noises[i], dtp);
        for (int i = 0; i < w->npending; i++)
            hear(w, g, &w->pending[i], dtp);
        see_anomalies(w, g, gi);
        if (g->suspicion > 1.0f)
            g->suspicion = 1.0f;
        /* Torch on in the dark or when hunting. */
        g->lit_t -= dtp;
        if (g->lit_t <= 0.0f) {
            /* Torch on where it's too dark to see: the same light meter the
             * player's HUD uses, sampled ahead of the guard. */
            NhBakeParams bp;
            nh_bake_default_params(&bp);
            NhVec3 ahead = nh_v3_add(g->pos, nh_v3_scale(nh_dir_from_yaw_pitch(g->yaw, 0), 1.5f));
            ahead.y += 1.0f;
            float lv = nh_bake_light_level(w->L, ahead, &bp);
            g->room_lit = lv > (g->torch_on ? 0.32f : 0.24f); /* hysteresis */
            g->lit_t = 0.5f + 0.1f * (float)(gi & 3);
        }
        bool hunting = g->state == AS_ALERT || g->state == AS_SEARCH || g->state == AS_INVESTIGATE || w->alarm;
        g->torch_on = a->torch ? (!g->room_lit || hunting) && !g->asleep : (g->arch == ARCH_LAZY && hunting);
        g->awareness = NH_MAX(0.0f, g->awareness - 0.004f * dtp);
    }
    if (g->radio_t > 0.0f) {
        g->radio_t -= dt;
        if (g->radio_t <= 0.0f)
            radio_call(w, gi);
    }

    /* Suspicion thresholds escalate the state. */
    if (g->suspicion >= SUS_ALERT && g->state != AS_ALERT && g->state != AS_FLEE) {
        go_alert(w, g, g->stim, g->vis > 0.0f);
    } else if (g->suspicion >= SUS_INVESTIGATE &&
               (calm_state(g->state) || g->state == AS_SUSPICIOUS)) {
        if (a->civilian)
            go_alert(w, g, g->stim, true);
        else
            start_investigate(w, g, g->stim);
    } else if (g->suspicion >= SUS_SUSPICIOUS && calm_state(g->state) && g->state != AS_DISTRACTED) {
        g->ret_state = g->state == AS_IDLE ? AS_PATROL : g->state;
        set_state(g, AS_SUSPICIOUS);
        stop(g);
        bark(w, g, BARK_HUH, false);
    }

    switch (g->state) {
    case AS_PATROL:
    case AS_IDLE:
        if (g->arch == ARCH_DOG && g->partner >= 0)
            do_dog_follow(w, g, dt);
        else
            do_patrol(w, g, dt);
        break;
    case AS_STATION:
        if (g->arch == ARCH_DOG && g->partner >= 0)
            do_dog_follow(w, g, dt);
        else
            do_station(w, g, dt);
        break;
    case AS_SUSPICIOUS:
        stop(g);
        g->yaw = nh_angle_approach(g->yaw, yaw_to(g->pos, g->stim), 3.0f * dt);
        g->head_yaw = nh_approach(g->head_yaw, 0.0f, 2.0f * dt);
        if (g->state_t > 3.0f && g->suspicion < SUS_SUSPICIOUS * 0.7f) {
            bark(w, g, BARK_NOTHING, false);
            g->suspicion = NH_MIN(g->suspicion, 0.1f);
            if (g->ret_state == AS_STATION)
                set_state(g, AS_STATION);
            else
                return_to_route(w, g);
        }
        break;
    case AS_INVESTIGATE:
    case AS_DISTRACTED:
        if (g->wait_t <= 0.0f && g->state == AS_INVESTIGATE && !g->has_goal && !g->arrived && !g->path_failed)
            set_goal(w, g, g->stim, false);
        if (g->arrived || g->path_failed) {
            g->arrived = false;
            g->path_failed = false;
            if (g->wait_t <= 0.0f)
                g->wait_t = 3.0f + 2.0f * rngf(w);
            /* Close the door that was left open. */
            if (g->anomaly) {
                AiAnomaly *an = &w->an[g->anomaly - 1];
                if (an->kind == AN_DOOR_OPEN && an->id < w->L->ndoors) {
                    w->L->doors[an->id].target = 0.0f;
                    an->active = 0;
                }
                g->anomaly = 0;
            }
        }
        if (!g->has_goal && g->wait_t > 0.0f) {
            g->wait_t -= dt;
            g->head_yaw = sinf(g->state_t * 1.3f) * 0.9f;
            if (g->wait_t <= 0.0f) {
                g->head_yaw = 0.0f;
                if (g->state == AS_INVESTIGATE && g->suspicion >= SUS_INVESTIGATE * 0.8f)
                    start_search(w, g, g->stim, false, false);
                else {
                    bark(w, g, BARK_NOTHING, false);
                    return_to_route(w, g);
                }
            }
        }
        break;
    case AS_ALERT: {
        bool sees = g->vis > 0.0f && g->seen_t < 0.2f;
        float pd = nh_v3_dist(g->pos, pv->pos);
        uint8_t bit = (uint8_t)(1u << g->target);
        if (sees && pd < CATCH_DIST && g->arch != ARCH_DOG && !(w->caught_mask & bit) && !pv->cuffed) {
            w->caught_mask |= bit;
            if (!w->caught)
                w->caught = gi + 1;
            emit(w, EV_CAUGHT, gi, g->target, pv->pos);
        }
        if (g->arch == ARCH_DOG && sees && pd < 2.0f && g->bark_cd <= 0.0f) {
            emit(w, EV_DOG_BARK, gi, 0, g->pos);
            bark(w, g, BARK_DOG, true);
            if (g->partner >= 0 && !down_state(w->g[g->partner].state))
                go_alert(w, &w->g[g->partner], pv->pos, false);
        }
        if (sees) {
            if (g->repath_t <= 0.0f || !g->has_goal) {
                set_goal(w, g, g->lkp, true);
                g->repath_t = 0.5f;
            }
            if (pd < 0.9f) {
                stop(g);
                g->yaw = nh_angle_approach(g->yaw, yaw_to(g->pos, pv->pos), 8.0f * dt);
            }
        } else if (g->arrived || g->path_failed || (!g->has_goal && g->seen_t > 0.5f)) {
            g->arrived = false;
            g->path_failed = false;
            bark(w, g, BARK_LOST, false);
            start_search(w, g, g->lkp, true, false);
            g->suspicion = 0.85f;
        } else if (!g->has_goal) {
            set_goal(w, g, g->lkp, true);
        }
        break;
    }
    case AS_SEARCH:
        do_search(w, g, pv, dt);
        if (g->vis > 0.0f && g->suspicion >= SUS_ALERT)
            go_alert(w, g, pv->pos, true);
        break;
    case AS_RETURN:
        if (g->arrived || g->path_failed || !g->has_goal) {
            g->arrived = false;
            g->path_failed = false;
            set_state(g, (g->arch == ARCH_CAMOP || g->nroute == 1) ? AS_STATION : AS_PATROL);
        }
        break;
    case AS_FLEE:
        do_flee(w, g, dt);
        break;
    default:
        break;
    }
    if (g->state != AS_IDLE && g->state != AS_SEARCH && g->state != AS_INVESTIGATE &&
        g->state != AS_DISTRACTED && g->state != AS_STATION)
        g->head_yaw = nh_approach(g->head_yaw, 0.0f, 2.0f * dt);
    move_guard(w, g, dt);
    pick_anim(g);
}

/* Light falling on the player from the guards' own torches. */
static float torch_light_on_player(const AiWorld *w, const AiPlayerView *pv)
{
    if (pv->in_vent || pv->hiding_prop >= 0)
        return 0.0f;
    float best = 0.0f;
    NhVec3 chest = nh_v3(pv->pos.x, pv->pos.y + pv->height * 0.6f, pv->pos.z);
    for (int i = 0; i < w->n; i++) {
        const AiGuard *g = &w->g[i];
        if (!g->active || !g->torch_on || down_state(g->state))
            continue;
        NhVec3 src = nh_v3(g->pos.x, g->pos.y + 1.1f, g->pos.z);
        NhVec3 to = nh_v3_sub(chest, src);
        float d = nh_v3_len(to);
        if (d > 10.0f || d < 0.1f)
            continue;
        NhVec3 dir = nh_dir_from_yaw_pitch(g->yaw + g->head_yaw, -0.15f);
        float c = nh_v3_dot(nh_v3_scale(to, 1.0f / d), dir);
        if (c < 0.88f) /* ~28 degree half-angle */
            continue;
        float t;
        if (lv_raycast(w->L, src, chest, &t))
            continue;
        float l = (0.25f + 0.75f * (1.0f - d / 10.0f)) * nh_smoothstep(0.88f, 0.95f, c);
        best = NH_MAX(best, l);
    }
    return best;
}

void ai_tick(AiWorld *w, const AiPlayerView *pv) { ai_tick_views(w, pv, 1); }

void ai_release_caught(AiWorld *w, int view)
{
    if (view >= 0 && view < AI_MAX_VIEWS)
        w->caught_mask &= (uint8_t)~(1u << view);
}

/* Which player a guard watches: the one it sees best; while hunting with
 * nobody in sight it keeps its quarry; otherwise the nearest. */
static int pick_target(const AiWorld *w, const AiGuard *g, const AiPlayerView *views, int n)
{
    if (n <= 1)
        return 0;
    int best = -1;
    float bv = 0.0f;
    for (int v = 0; v < n; v++) {
        float d;
        float vis = ai_visibility(w, g, &views[v], &d);
        if (vis > bv) {
            bv = vis;
            best = v;
        }
    }
    if (best >= 0)
        return best;
    int cur = g->target < n ? g->target : 0;
    if ((g->state == AS_ALERT || g->state == AS_SEARCH) && !views[cur].cuffed)
        return cur;
    float bd = 1e9f;
    for (int v = 0; v < n; v++) {
        float d = nh_v3_dist(g->pos, views[v].pos) + (views[v].cuffed ? 1000.0f : 0.0f);
        if (d < bd) {
            bd = d;
            best = v;
        }
    }
    return best < 0 ? 0 : best;
}

void ai_tick_views(AiWorld *w, const AiPlayerView *views_in, int n)
{
    n = NH_CLAMP(n, 1, AI_MAX_VIEWS);
    const float dt = NH_TICK_DT;
    w->nev = 0;
    for (int i = 0; i < (int)NH_ARRAY_LEN(w->smoke); i++)
        if (w->smoke[i].t > 0.0f)
            w->smoke[i].t -= dt;
    for (int i = 0; i < w->n; i++) {
        AiGuard *g = &w->g[i];
        if (g->ko_timer > 0.0f && !down_state(g->state)) {
            g->ko_timer -= dt;
            g->speed *= 0.5f;
            if (g->ko_timer <= 0.0f)
                ai_knockout(w, i);
        }
    }
    AiPlayerView lit[AI_MAX_VIEWS];
    for (int v = 0; v < n; v++) {
        lit[v] = views_in[v];
        lit[v].light = NH_MAX(lit[v].light, torch_light_on_player(w, &views_in[v]));
        w->view_light[v] = lit[v].light;
    }
    w->player_light = lit[0].light;
    w->views = lit;
    w->nviews = n;
    for (int i = 0; i < w->n; i++) {
        AiGuard *g = &w->g[i];
        if (!g->active)
            continue;
        if (n > 1 && ((w->tick + (uint32_t)i) & 1u) == 0 && !down_state(g->state))
            g->target = (uint8_t)pick_target(w, g, lit, n);
        guard_tick(w, i, &lit[NH_MIN(g->target, n - 1)], dt);
    }
    /* Pending distractions are heard on the tick each guard thinks; keep them
     * for two ticks so the staggered halves both get them. */
    if (w->tick & 1u)
        w->npending = 0;
    supervisor_checkin(w, dt);
    /* Keep guards from walking through each other. */
    for (int i = 0; i < w->n; i++)
        for (int j = i + 1; j < w->n; j++) {
            AiGuard *a = &w->g[i], *b = &w->g[j];
            if (!a->active || !b->active || down_state(a->state) || down_state(b->state) || a->floor != b->floor)
                continue;
            float dx = b->pos.x - a->pos.x, dz = b->pos.z - a->pos.z;
            float d2 = dx * dx + dz * dz;
            if (d2 < 0.36f && d2 > 1e-6f) {
                float d = sqrtf(d2), push = (0.6f - d) * 0.5f;
                dx /= d;
                dz /= d;
                a->pos.x -= dx * push;
                a->pos.z -= dz * push;
                b->pos.x += dx * push;
                b->pos.z += dz * push;
                lv_collide_cylinder(w->L, &a->pos, GUARD_R, a->pos.y + 0.45f, a->pos.y + 1.7f);
                lv_collide_cylinder(w->L, &b->pos, GUARD_R, b->pos.y + 0.45f, b->pos.y + 1.7f);
            }
        }
    float mx = 0.0f;
    for (int i = 0; i < w->n; i++) {
        const AiGuard *g = &w->g[i];
        if (!g->active || down_state(g->state))
            continue;
        float s = g->state == AS_ALERT ? 1.0f : g->suspicion;
        mx = NH_MAX(mx, s);
    }
    w->max_suspicion = mx;
    w->views = NULL; /* points at this frame's copies */
    w->nviews = 0;
    w->tick++;
}

/* ---- game hooks ------------------------------------------------------------ */

bool ai_can_takedown(const AiWorld *w, int gi, NhVec3 player, float *dist)
{
    const AiGuard *g = &w->g[gi];
    if (!g->active || down_state(g->state) || g->state == AS_FLEE)
        return false;
    float d = nh_v3_dist_xz(g->pos, player);
    if (dist)
        *dist = d;
    if (d > 1.6f || fabsf(player.y - g->pos.y) > 0.8f)
        return false;
    if (g->asleep)
        return true;
    /* From behind (outside a 200 degree frontal arc), or when they've no idea. */
    NhVec3 fwd = nh_dir_from_yaw_pitch(g->yaw, 0.0f);
    NhVec3 to = nh_v3_norm(nh_v3(player.x - g->pos.x, 0, player.z - g->pos.z));
    bool behind = nh_v3_dot(fwd, to) < -0.17f;
    if (g->state == AS_ALERT)
        return false;
    return behind || (g->vis <= 0.0f && g->suspicion < SUS_SUSPICIOUS);
}

void ai_knockout(AiWorld *w, int gi)
{
    AiGuard *g = &w->g[gi];
    if (g->door_close) {
        w->L->doors[g->door_close - 1].target = 0.0f;
        g->door_close = 0;
    }
    set_state(g, AS_KO);
    stop(g);
    g->radio_t = 0.0f;
    g->suspicion = 0.0f;
    g->torch_on = false;
    g->head_yaw = 0.0f;
    emit(w, EV_TAKEDOWN, gi, 0, g->pos);
}

int ai_add_anomaly(AiWorld *w, int kind, NhVec3 pos, int id)
{
    for (int i = 0; i < w->nan; i++)
        if (w->an[i].active && w->an[i].kind == kind && w->an[i].id == id) {
            w->an[i].pos = pos;
            return i;
        }
    int slot = -1;
    for (int i = 0; i < w->nan; i++)
        if (!w->an[i].active) {
            slot = i;
            break;
        }
    if (slot < 0) {
        if (w->nan >= AI_ANOMALY_MAX)
            return -1;
        slot = w->nan++;
    }
    w->an[slot] = (AiAnomaly){ pos, (uint8_t)kind, 1, (uint16_t)id, 0 };
    return slot;
}

void ai_clear_anomaly(AiWorld *w, int kind, int id)
{
    for (int i = 0; i < w->nan; i++)
        if (w->an[i].active && w->an[i].kind == kind && w->an[i].id == id)
            w->an[i].active = 0;
}

void ai_distraction(AiWorld *w, NhVec3 pos, float radius, int kind)
{
    if (w->npending >= (int)NH_ARRAY_LEN(w->pending))
        return;
    w->pending[w->npending++] = (NhNoise){ pos, radius, (uint8_t)kind, 0, 0 };
}

void ai_send_guard(AiWorld *w, int gi, NhVec3 pos, float seconds)
{
    AiGuard *g = &w->g[gi];
    if (!g->active || !calm_state(g->state))
        return;
    set_state(g, AS_DISTRACTED);
    g->stim = pos;
    g->wait_t = seconds;
    set_goal(w, g, pos, false);
}

void ai_raise_alarm(AiWorld *w, NhVec3 where)
{
    if (w->alarm)
        return;
    w->alarm = true;
    emit(w, EV_ALARM, 0, 0, where);
    /* The three nearest responders converge; everyone else goes on edge. */
    int order[AI_MAX_GUARDS], n = 0;
    for (int i = 0; i < w->n; i++) {
        AiGuard *g = &w->g[i];
        if (!g->active || down_state(g->state) || A(g)->civilian || g->arch == ARCH_CAMOP)
            continue;
        g->awareness = 1.0f;
        g->asleep = false;
        order[n++] = i;
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (nh_v3_dist(w->g[order[j]].pos, where) < nh_v3_dist(w->g[order[i]].pos, where)) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
    for (int i = 0; i < n && i < 3; i++) {
        AiGuard *g = &w->g[order[i]];
        if (g->state != AS_ALERT) {
            g->lkp = where;
            start_search(w, g, where, true, true);
        }
    }
}

void ai_report(AiWorld *w, NhVec3 where)
{
    w->alerts++;
    emit(w, EV_RADIO, 0, BARK_CAMERA, where);
    /* The two nearest free guards come running; the rest walk over. */
    int best[2] = { -1, -1 };
    float bd[2] = { 1e9f, 1e9f };
    for (int i = 0; i < w->n; i++) {
        AiGuard *g = &w->g[i];
        if (!g->active || down_state(g->state) || A(g)->civilian || g->arch == ARCH_CAMOP || g->arch == ARCH_DOG ||
            g->state == AS_ALERT)
            continue;
        float d = nh_v3_dist(g->pos, where);
        if (d < bd[0]) {
            bd[1] = bd[0];
            best[1] = best[0];
            bd[0] = d;
            best[0] = i;
        } else if (d < bd[1]) {
            bd[1] = d;
            best[1] = i;
        }
    }
    for (int k = 0; k < 2; k++) {
        if (best[k] < 0)
            continue;
        AiGuard *g = &w->g[best[k]];
        g->lkp = where;
        g->awareness = NH_MIN(1.0f, g->awareness + 0.4f);
        start_search(w, g, where, true, true);
        g->suspicion = NH_MAX(g->suspicion, 0.7f);
    }
    if (w->alerts >= w->alarm_threshold)
        ai_raise_alarm(w, where);
}

int ai_spawn_police(AiWorld *w, NhVec3 at, NhVec3 lkp)
{
    int i = ai_add_guard(w, ARCH_POLICE, at, 0.0f, NULL, 0);
    if (i < 0)
        return -1;
    AiGuard *g = &w->g[i];
    g->awareness = 1.0f;
    g->lkp = lkp;
    start_search(w, g, lkp, true, true);
    g->search_t = 600.0f; /* they don't give up */
    bark(w, g, BARK_POLICE, true);
    return i;
}

void ai_tranquilise(AiWorld *w, int gi, float seconds)
{
    AiGuard *g = &w->g[gi];
    if (!g->active || down_state(g->state))
        return;
    g->ko_timer = seconds;
    g->suspicion = NH_MAX(g->suspicion, 0.4f);
    bark(w, g, BARK_HUH, true);
}

void ai_carry(AiWorld *w, int gi, bool carried)
{
    AiGuard *g = &w->g[gi];
    if (g->state != AS_KO && g->state != AS_CARRIED)
        return;
    g->state = carried ? AS_CARRIED : AS_KO;
}
