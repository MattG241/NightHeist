/* Tests for navigation and guard AI: pathfinding over the test building,
 * detection maths (vision cone, distance, light, stance, occlusion), noise
 * attenuation, takedown rules, scenario runs and determinism. */
#include <math.h>
#include <string.h>

#include "core/nh_ai.h"
#include "core/nh_nav.h"
#include "game/testlevel.h"
#include "nh_test.h"

static Level s_L;
static NhNav s_nav;
static AiWorld s_ai;

static Level *setup(void)
{
    testlevel_build(&s_L);
    for (int i = 0; i < 16; i++)
        s_L.power[i] = 1;
    nh_nav_build(&s_nav, &s_L, 0.3f);
    return &s_L;
}

static bool path_valid(const NhPath *p)
{
    if (p->n < 2)
        return false;
    for (int i = 1; i < p->n; i++)
        if (nh_v3_dist(p->p[i - 1], p->p[i]) > 30.0f)
            return false;
    return true;
}

void test_nav(void)
{
    Level *L = setup();
    NhPath p;
    /* Same room */
    CHECK(nh_nav_path(&s_nav, nh_v3(8, 0, 9), nh_v3(16, 0, 9.5f), true, &p));
    CHECK(path_valid(&p));
    CHECK(p.n == 2); /* straight line after string pulling */
    CHECK_NEAR(nh_path_length(&p), nh_v3_dist(nh_v3(8, 0, 9), nh_v3(16, 0, 9.5f)), 0.01);
    /* Lobby -> office B through the corridor: goes via both door centres. */
    CHECK(nh_nav_path(&s_nav, nh_v3(10, 0, 10), nh_v3(28, 0, 18), true, &p));
    CHECK(path_valid(&p));
    bool via_door = false;
    for (int i = 0; i < p.n; i++)
        via_door |= fabsf(p.p[i].x - 18.0f) < 0.6f && fabsf(p.p[i].z - 11.0f) < 0.6f;
    CHECK(via_door);
    float len = nh_path_length(&p);
    CHECK(len > 18.0f && len < 30.0f);
    /* Upstairs via the stair: the path climbs to floor 1. */
    CHECK(nh_nav_path(&s_nav, nh_v3(10, 0, 10), nh_v3(28, LV_STOREY, 20), true, &p));
    CHECK(path_valid(&p));
    CHECK(p.p[p.n - 1].y > 3.0f);
    bool on_stair = false;
    for (int i = 0; i < p.n; i++)
        on_stair |= p.p[i].y > 0.5f && p.p[i].y < 3.1f;
    CHECK(on_stair);
    /* Every consecutive pair on one floor is walkable. */
    for (int i = 1; i < p.n; i++)
        CHECK(nh_nav_walkable(&s_nav, p.p[i]));
    /* Dogs can't open doors: the corridor is behind a door from the yard. */
    CHECK(!nh_nav_path(&s_nav, nh_v3(30, 0, 29), nh_v3(20, 0, 20), false, &p));
    CHECK(nh_nav_path(&s_nav, nh_v3(30, 0, 29), nh_v3(20, 0, 20), true, &p));
    /* Around the outside of the building (no doors needed). */
    CHECK(nh_nav_path(&s_nav, nh_v3(3, 0, 3), nh_v3(37, 0, 29), false, &p));
    /* Cache: the same query is served from the cache with zero expansions. */
    NhPath q;
    CHECK(nh_nav_path(&s_nav, nh_v3(3, 0, 3), nh_v3(37, 0, 29), false, &q));
    CHECK(s_nav.expansions == 0);
    CHECK(q.n == p.n);
    /* Solid furniture is not walkable; open floor is. */
    CHECK(!nh_nav_walkable(&s_nav, nh_v3(25.0f, 0, 22.0f))); /* server rack */
    CHECK(nh_nav_walkable(&s_nav, nh_v3(28.0f, 0, 23.3f)));  /* aisle */
    /* Snap pulls a point inside a prop to nearby floor. */
    NhVec3 s;
    CHECK(nh_nav_snap(&s_nav, nh_v3(25.0f, 0, 22.0f), &s) && nh_nav_walkable(&s_nav, s));
    /* Line clear in an open room, blocked through a wall. */
    CHECK(nh_nav_line_clear(&s_nav, nh_v3(8, 0, 9), nh_v3(16, 0, 9.5f)));
    CHECK(!nh_nav_line_clear(&s_nav, nh_v3(8, 0, 12), nh_v3(16, 0, 12))); /* reception desk */
    CHECK(!nh_nav_line_clear(&s_nav, nh_v3(14, 0, 9), nh_v3(24, 0, 9)));
    /* Many random paths between walkable points all succeed and stay sane. */
    NhRng r;
    nh_rng_seed(&r, 77, 1);
    int ok = 0, tried = 0;
    for (int i = 0; i < 300; i++) {
        NhVec3 a = nh_v3(6.5f + nh_rng_float(&r) * 27.0f, 0, 6.5f + nh_rng_float(&r) * 19.0f);
        NhVec3 b = nh_v3(6.5f + nh_rng_float(&r) * 27.0f, nh_rng_float(&r) < 0.5f ? 0 : LV_STOREY,
                         6.5f + nh_rng_float(&r) * 19.0f);
        if (!nh_nav_walkable(&s_nav, a) || !nh_nav_walkable(&s_nav, b))
            continue;
        tried++;
        if (nh_nav_path(&s_nav, a, b, true, &p) && path_valid(&p) && nh_path_length(&p) < 120.0f)
            ok++;
    }
    CHECK(tried > 100);
    CHECK(ok == tried);
    (void)L;
}

static AiPlayerView view_at(NhVec3 pos, float light)
{
    AiPlayerView pv;
    memset(&pv, 0, sizeof(pv));
    pv.pos = pos;
    pv.head = nh_v3(pos.x, pos.y + 1.65f, pos.z);
    pv.height = 1.85f;
    pv.light = light;
    pv.hiding_prop = -1;
    return pv;
}

/* One guard standing at a post in the lobby looking +X (yaw -90 degrees). */
static AiGuard *lone_guard(int arch)
{
    ai_init(&s_ai, &s_L, &s_nav, 5);
    int i = ai_add_guard(&s_ai, arch, nh_v3(8, 0, 11), -NH_PI * 0.5f, NULL, 0);
    return &s_ai.g[i];
}

void test_detection(void)
{
    setup();
    AiGuard *g = lone_guard(ARCH_GUARD);
    float d, d2;
    /* In front, lit, in the open: fully visible. */
    AiPlayerView pv = view_at(nh_v3(14, 0, 11), 1.0f);
    CHECK_NEAR(ai_visibility(&s_ai, g, &pv, &d), 1.0f, 1e-6);
    CHECK_NEAR(d, 6.0f, 0.2f);
    /* Behind the guard: invisible. */
    pv = view_at(nh_v3(6.8f, 0, 11), 1.0f);
    CHECK(ai_visibility(&s_ai, g, &pv, &d) == 0.0f);
    /* Peripheral, close: partly visible. */
    pv = view_at(nh_v3(8.5f, 0, 14.0f), 1.0f);
    float pvis = ai_visibility(&s_ai, g, &pv, &d);
    CHECK(pvis > 0.0f && pvis < 1.0f);
    /* Through the corridor wall: invisible. */
    pv = view_at(nh_v3(20, 0, 12), 1.0f);
    CHECK(ai_visibility(&s_ai, g, &pv, &d) == 0.0f);
    /* Upstairs: invisible through the floor. */
    pv = view_at(nh_v3(14, LV_STOREY, 11), 1.0f);
    CHECK(ai_visibility(&s_ai, g, &pv, &d) == 0.0f);
    /* Darkness shrinks the range: at 9 m, visible lit, invisible dark. */
    pv = view_at(nh_v3(17, 0, 11), 1.0f);
    CHECK(ai_visibility(&s_ai, g, &pv, &d) > 0.0f);
    pv.light = 0.05f;
    CHECK(ai_visibility(&s_ai, g, &pv, &d) == 0.0f);
    /* Gain: decreases with distance, increases with light, crouch and stillness reduce it. */
    pv = view_at(nh_v3(12, 0, 11), 0.6f);
    float v1 = ai_visibility(&s_ai, g, &pv, &d);
    float g_near = ai_gain_rate(&s_ai, g, &pv, v1, d);
    pv = view_at(nh_v3(15, 0, 11), 0.6f);
    float v2 = ai_visibility(&s_ai, g, &pv, &d2);
    float g_far = ai_gain_rate(&s_ai, g, &pv, v2, d2);
    CHECK(g_near > g_far && g_far > 0.0f);
    pv = view_at(nh_v3(12, 0, 11), 0.3f);
    float g_dim = ai_gain_rate(&s_ai, g, &pv, ai_visibility(&s_ai, g, &pv, &d), d);
    CHECK(g_dim < g_near);
    pv = view_at(nh_v3(12, 0, 11), 0.6f);
    pv.crouched = true;
    float g_crouch = ai_gain_rate(&s_ai, g, &pv, v1, d);
    CHECK_NEAR(g_crouch / g_near, 0.6f, 1e-4);
    pv.crouched = false;
    pv.speed = 4.0f;
    CHECK(ai_gain_rate(&s_ai, g, &pv, v1, d) > g_near * 2.0f);
    /* Zero visibility, zero gain. */
    CHECK(ai_gain_rate(&s_ai, g, &pv, 0.0f, d) == 0.0f);
    /* Hiding or in a vent: invisible even in plain sight. */
    pv = view_at(nh_v3(12, 0, 11), 1.0f);
    pv.hiding_prop = 3;
    CHECK(ai_visibility(&s_ai, g, &pv, &d) == 0.0f);
    pv.hiding_prop = -1;
    pv.in_vent = true;
    CHECK(ai_visibility(&s_ai, g, &pv, &d) == 0.0f);
    /* Thermal goggles see you in the dark at range; a normal guard doesn't. */
    pv = view_at(nh_v3(17, 0, 11), 0.0f);
    CHECK(ai_visibility(&s_ai, g, &pv, &d) == 0.0f);
    AiGuard *e = lone_guard(ARCH_ELITE);
    CHECK(ai_visibility(&s_ai, e, &pv, &d) > 0.0f);
    /* Veterans notice faster than rookies. */
    pv = view_at(nh_v3(12, 0, 11), 0.6f);
    AiGuard *v = lone_guard(ARCH_VETERAN);
    float gv = ai_gain_rate(&s_ai, v, &pv, 1.0f, 4.0f);
    AiGuard *rk = lone_guard(ARCH_ROOKIE);
    float gr = ai_gain_rate(&s_ai, rk, &pv, 1.0f, 4.0f);
    CHECK(gv > gr);

    /* Noise blockers: same room 0, one wall 1, a floor 2. */
    CHECK(ai_count_blockers(&s_L, nh_v3(8, 1, 9), nh_v3(14, 1, 14)) == 0);
    CHECK(ai_count_blockers(&s_L, nh_v3(14, 1, 9), nh_v3(24, 1, 9)) >= 1);
    CHECK(ai_count_blockers(&s_L, nh_v3(8, 1, 9), nh_v3(8, LV_STOREY + 1, 9)) == 2);
}

static int run(AiPlayerView *pv, int ticks, int stop_event)
{
    for (int t = 0; t < ticks; t++) {
        ai_tick(&s_ai, pv);
        for (int i = 0; i < s_L.ndoors; i++) {
            LvDoor *dr = &s_L.doors[i];
            dr->open = nh_approach(dr->open, dr->target, 1.5f / 60.0f);
        }
        for (int e = 0; e < s_ai.nev; e++)
            if (s_ai.ev[e].type == stop_event)
                return t;
    }
    return -1;
}

void test_ai(void)
{
    setup();
    /* Standing in a lit room in front of a patrolling guard: caught. */
    ai_init(&s_ai, &s_L, &s_nav, 9);
    testlevel_guards(&s_ai);
    AiPlayerView pv = view_at(nh_v3(8, 0, 13), 0.85f);
    int t = run(&pv, 60 * 20, EV_CAUGHT);
    CHECK(t > 0 && t < 60 * 12);
    /* Crouched still in a dark corner of the lobby away from the patrol: never seen. */
    ai_init(&s_ai, &s_L, &s_nav, 9);
    testlevel_guards(&s_ai);
    pv = view_at(nh_v3(6.6f, 0, 6.6f), 0.04f);
    pv.crouched = true;
    t = run(&pv, 60 * 40, EV_SPOTTED);
    CHECK(t < 0);
    CHECK(s_ai.alerts == 0);

    /* Footsteps behind a guard: suspicious, turns to look. */
    AiGuard *g = lone_guard(ARCH_GUARD);
    pv = view_at(nh_v3(5.5f, 0, 11), 0.0f);
    NhNoise n = { nh_v3(6.5f, 0, 11), 8.0f, NH_NOISE_FOOTSTEP, 0, 1 };
    pv.noises = &n;
    pv.nnoise = 1;
    run(&pv, 60, -1);
    CHECK(g->suspicion > 0.3f);
    CHECK(g->state != AS_PATROL && g->state != AS_STATION);
    /* The same footsteps on the other side of a wall barely register. */
    g = lone_guard(ARCH_GUARD);
    n.pos = nh_v3(20, 0, 11);
    pv.pos = n.pos;
    run(&pv, 60, -1);
    CHECK(g->suspicion < 0.3f);

    /* A distraction pulls a guard over without alerting them. */
    g = lone_guard(ARCH_ROOKIE);
    pv = view_at(nh_v3(30, 0, 25), 0.0f);
    ai_distraction(&s_ai, nh_v3(12, 0, 15), 10.0f, NH_NOISE_THROW);
    run(&pv, 60 * 5, -1);
    CHECK(g->state == AS_DISTRACTED || g->state == AS_RETURN);
    CHECK(g->suspicion < 1.0f);
    CHECK(nh_v3_dist(g->pos, nh_v3(12, 0, 15)) < 3.0f || g->state == AS_RETURN);

    /* Takedowns: from behind yes, from the front no, alerted never. */
    g = lone_guard(ARCH_GUARD);
    CHECK(ai_can_takedown(&s_ai, 0, nh_v3(7.0f, 0, 11), NULL));
    CHECK(!ai_can_takedown(&s_ai, 0, nh_v3(9.0f, 0, 11), NULL) || g->vis <= 0.0f);
    CHECK(!ai_can_takedown(&s_ai, 0, nh_v3(4.0f, 0, 11), NULL));
    ai_knockout(&s_ai, 0);
    CHECK(g->state == AS_KO);
    CHECK(!ai_can_takedown(&s_ai, 0, nh_v3(7.0f, 0, 11), NULL));

    /* A body found: search plus a radio call, which counts as an alert. */
    ai_init(&s_ai, &s_L, &s_nav, 3);
    ai_add_guard(&s_ai, ARCH_GUARD, nh_v3(8, 0, 11), -NH_PI * 0.5f, NULL, 0);
    ai_add_guard(&s_ai, ARCH_GUARD, nh_v3(20, 0, 20), 0, NULL, 0);
    ai_add_anomaly(&s_ai, AN_BODY, nh_v3(13, 0.3f, 11), 7);
    pv = view_at(nh_v3(30, 0, 25), 0.0f);
    t = run(&pv, 60 * 4, EV_RADIO);
    CHECK(t > 0);
    CHECK(s_ai.alerts == 1);
    CHECK(s_ai.g[0].state == AS_SEARCH);
    CHECK(s_ai.g[1].state == AS_SEARCH); /* responded to the radio */

    /* Enough alerts raise the building alarm. */
    s_ai.alarm_threshold = 1;
    s_ai.alerts = 0;
    ai_init(&s_ai, &s_L, &s_nav, 3);
    s_ai.alarm_threshold = 1;
    ai_add_guard(&s_ai, ARCH_GUARD, nh_v3(8, 0, 11), -NH_PI * 0.5f, NULL, 0);
    ai_add_anomaly(&s_ai, AN_BODY, nh_v3(13, 0.3f, 11), 7);
    run(&pv, 60 * 4, EV_ALARM);
    CHECK(s_ai.alarm);

    /* Supervisor check-in notices a guard who's been knocked out. */
    ai_init(&s_ai, &s_L, &s_nav, 4);
    s_ai.checkin_period = 2.0f;
    s_ai.checkin_t = 2.0f;
    ai_add_guard(&s_ai, ARCH_SUPERVISOR, nh_v3(12, LV_STOREY, 12.5f), 0, NULL, 0);
    ai_add_guard(&s_ai, ARCH_GUARD, nh_v3(20, 0, 20), 0, NULL, 0);
    ai_knockout(&s_ai, 1);
    t = run(&pv, 60 * 3, EV_CHECKIN_MISSED);
    CHECK(t > 0);
    CHECK(s_ai.g[0].state == AS_SEARCH);

    /* Determinism: two runs of the full guard roster end identically. */
    NhVec3 end[AI_MAX_GUARDS];
    for (int rep = 0; rep < 2; rep++) {
        setup();
        ai_init(&s_ai, &s_L, &s_nav, 1234);
        testlevel_guards(&s_ai);
        pv = view_at(nh_v3(30, 0, 18), 0.3f);
        run(&pv, 60 * 30, -1);
        for (int i = 0; i < s_ai.n; i++) {
            if (rep == 0)
                end[i] = s_ai.g[i].pos;
            else
                CHECK(nh_v3_dist(end[i], s_ai.g[i].pos) < 1e-6f);
        }
    }
    /* Patrols actually move: after 30 s every walker left its spawn. */
    setup();
    ai_init(&s_ai, &s_L, &s_nav, 99);
    testlevel_guards(&s_ai);
    NhVec3 spawn[AI_MAX_GUARDS];
    for (int i = 0; i < s_ai.n; i++)
        spawn[i] = s_ai.g[i].pos;
    pv = view_at(nh_v3(30, 0, 25), 0.0f);
    pv.in_vent = true;
    int moved = 0, patrollers = 0;
    float max_dev = 0.0f;
    for (int k = 0; k < 60 * 30; k++) {
        run(&pv, 1, -1);
        for (int i = 0; i < s_ai.n; i++)
            if (s_ai.g[i].nroute > 1) {
                float dv = nh_v3_dist(spawn[i], s_ai.g[i].pos);
                max_dev = fmaxf(max_dev, dv);
            }
    }
    for (int i = 0; i < s_ai.n; i++)
        if (s_ai.g[i].nroute > 1) {
            patrollers++;
            /* Guards never end up inside walls or furniture. */
            CHECK(nh_nav_walkable(&s_nav, s_ai.g[i].pos) || nh_nav_snap(&s_nav, s_ai.g[i].pos, &spawn[i]));
            moved += s_ai.g[i].stuck_t < 1.0f;
        }
    CHECK(patrollers >= 4);
    CHECK(moved == patrollers);
    CHECK(max_dev > 5.0f);
}
