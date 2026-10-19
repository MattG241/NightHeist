/* Tests for the level model, kit, portals, bake, player controller and the
 * converted asset files. Uses the hand-built test building. */
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/nh_assets.h"
#include "core/nh_bake.h"
#include "core/nh_fixedstep.h"
#include "core/nh_kit.h"
#include "core/nh_player.h"
#include "core/nh_portal.h"
#include "core/nh_proptab.h"
#include "core/nh_rooms.h"
#include "game/testlevel.h"
#include "nh_test.h"

static Level s_L;
static _Alignas(32) uint8_t s_mem[24 << 20];

static Level *fresh_level(void)
{
    testlevel_build(&s_L);
    return &s_L;
}

void test_level(void)
{
    Level *L = fresh_level();
    /* Lobby (3..8, 3..7) and corridor (9..10) are separated by a wall with a door. */
    const LvEdge *e = lv_edge_side_c(L, 0, 8, 4, 0);
    CHECK(e && e->type == LV_E_WALL);
    e = lv_edge_side_c(L, 0, 8, 5, 0);
    CHECK(e && e->type == LV_E_DOOR);
    /* Exterior wall facing the street, window on the lobby front. */
    CHECK(lv_edge_side_c(L, 0, 5, 3, 3)->type == LV_E_DOOR);
    CHECK(lv_edge_side_c(L, 0, 3, 3, 3)->type == LV_E_WINDOW);
    CHECK(lv_edge_side_c(L, 0, 6, 3, 3)->type == LV_E_WALL);
    /* Inside a room: open. */
    CHECK(lv_edge_side_c(L, 0, 4, 4, 0)->type == LV_E_OPEN);
    /* Roof edges get a parapet rail. */
    CHECK(lv_edge_side_c(L, 2, 3, 5, 2)->type == LV_E_RAIL);
    /* Stair hole: rails around it except the exit. */
    CHECK(L->cells[1][10][4].flags & LV_C_HOLE);
    CHECK(lv_edge_side_c(L, 1, 4, 10, 0)->type == LV_E_RAIL);
    CHECK(lv_edge_side_c(L, 1, 4, 11, 1)->type == LV_E_OPEN); /* exit to (4,12) */

    /* Raycasts: blocked by walls, through open doors and windows. */
    NhVec3 lobby = nh_v3(12, 1.5f, 10), corridor = nh_v3(20, 1.5f, 10);
    CHECK(lv_raycast(L, lobby, corridor, NULL));
    NhVec3 at_door_l = nh_v3(15, 1.2f, 11), at_door_r = nh_v3(19, 1.2f, 11);
    L->doors[1].open = 0.0f;
    CHECK(lv_raycast(L, at_door_l, at_door_r, NULL));
    L->doors[1].open = 1.0f;
    CHECK(!lv_raycast(L, at_door_l, at_door_r, NULL));
    NhVec3 street = nh_v3(7, 1.6f, 2), through_window = nh_v3(7, 1.6f, 9);
    CHECK(!lv_raycast(L, street, through_window, NULL));
    CHECK(lv_raycast(L, nh_v3(7, 0.4f, 2), nh_v3(7, 0.4f, 9), NULL)); /* below the sill */
    /* Floors block between storeys, but not over the stair hole. */
    CHECK(lv_raycast(L, nh_v3(12, 1.5f, 10), nh_v3(12, 5.0f, 10), NULL));
    CHECK(!lv_raycast(L, nh_v3(9, 1.5f, 21), nh_v3(9, 5.0f, 21), NULL));

    /* Ground: stairs ramp between floors. */
    CHECK_NEAR(lv_ground_height(L, 9, 17.0f, 0.0f), 0.0f, 1e-4);
    CHECK_NEAR(lv_ground_height(L, 9, 18.5f, 0.0f), 0.3f, 0.01);
    float mid = lv_ground_height(L, 9, 21.0f, 1.5f);
    CHECK(mid > 1.0f && mid < 2.6f);
    CHECK_NEAR(lv_ground_height(L, 9, 25.0f, 3.6f), 3.6f, 0.01);
    /* Upper floor over the hole: you'd fall onto the ramp. */
    CHECK(lv_ground_height(L, 9, 20.0f, 3.6f) < 3.0f);

    /* Collision pushes a cylinder out of a wall. */
    NhVec3 p = nh_v3(17.95f, 0, 9.0f); /* lobby side of the lobby/corridor wall */
    CHECK(lv_collide_cylinder(L, &p, 0.3f, 0.2f, 1.7f));
    CHECK(p.x <= 18.0f - 0.3f - 0.07f);
    /* Props are solid. */
    NhVec3 q = nh_v3(12.0f, 0, 12.0f); /* inside the reception desk */
    CHECK(lv_collide_cylinder(L, &q, 0.3f, 0.2f, 1.7f));
    CHECK(nh_v3_dist_xz(q, nh_v3(12, 0, 12)) > 0.3f);
}

void test_kit(void)
{
    Level *L = fresh_level();
    NhArena a;
    nh_arena_init(&a, s_mem, sizeof(s_mem));
    uint8_t map[MAT_COUNT];
    for (int i = 0; i < MAT_COUNT; i++)
        map[i] = (uint8_t)i;
    NhKitMaterials km = { 100, 101, 102, 103, 104, 105, 106, 107, 108, map };
    int bad = 0, total_tris = 0;
    for (int r = LV_ROOM_OUTSIDE; r < L->nrooms; r++) {
        size_t mark = nh_arena_mark(&a);
        NhBatchSet bs;
        nh_batches_init(&bs, &a, 20000, 40000);
        nh_kit_build_room(L, r, &km, &bs);
        CHECK(!bs.overflow);
        CHECK(bs.n > 0);
        for (int i = 0; i < bs.n; i++) {
            const NhMesh *m = &bs.b[i].mesh;
            total_tris += (int)m->icount / 3;
            for (uint32_t t = 0; t < m->icount; t += 3) {
                NhVec3 p0 = nh_vert_pos(&m->verts[m->idx[t]]), p1 = nh_vert_pos(&m->verts[m->idx[t + 1]]),
                       p2 = nh_vert_pos(&m->verts[m->idx[t + 2]]);
                NhVec3 fn = nh_v3_cross(nh_v3_sub(p1, p0), nh_v3_sub(p2, p0));
                if (nh_v3_len(fn) < 1e-6f)
                    continue; /* quantisation can collapse slivers */
                if (nh_v3_dot(fn, nh_vert_nrm(&m->verts[m->idx[t]])) <= 0.0f)
                    bad++;
            }
        }
        nh_arena_reset_to(&a, mark);
    }
    CHECK(bad == 0);
    CHECK(total_tris > 5000 && total_tris < 200000);
}

void test_portals(void)
{
    Level *L = fresh_level();
    static NhPortalSet ps;
    nh_portals_build(L, &ps);
    CHECK(ps.n > 20);
    /* Camera in the lobby looking at the lobby/corridor door (+X). */
    NhVec3 eye = nh_v3(14, 1.6f, 11);
    float vp[16];
    {
        NhMat34 view;
        nh_m34_look_at(&view, eye, nh_v3(0, 1, 0), nh_v3(20, 1.6f, 11));
        float f = 1.0f / tanf(NH_DEG2RAD(62.0f) * 0.5f), n = 0.1f, fa = 90.0f;
        float P[16] = { f / 1.333f, 0, 0, 0, 0, f, 0, 0, 0, 0, (fa + n) / (n - fa), 2 * fa * n / (n - fa), 0, 0, -1, 0 };
        float V[16] = { 0 };
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 4; c++)
                V[r * 4 + c] = view.m[r][c];
        V[15] = 1;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) {
                float s = 0;
                for (int k = 0; k < 4; k++)
                    s += P[r * 4 + k] * V[k * 4 + c];
                vp[r * 4 + c] = s;
            }
    }
    uint8_t vis[LV_ROOMS_MAX];
    int lobby = nh_room_at_point(L, eye);
    int corridor = lv_room_at(L, 0, 9, 5);
    int server = lv_room_at(L, 0, 12, 11);
    L->doors[1].open = 1.0f;
    nh_portals_visible(L, &ps, vp, eye, lobby, vis);
    CHECK(vis[lobby] && vis[corridor]);
    CHECK(!vis[server]); /* behind closed doors */
    L->doors[1].open = 0.0f;
    nh_portals_visible(L, &ps, vp, eye, lobby, vis);
    CHECK(vis[lobby] && !vis[corridor]);
}

void test_bake(void)
{
    Level *L = fresh_level();
    NhBakeParams bp;
    nh_bake_default_params(&bp);
    /* The lit lobby is brighter than the dark office A (lights off). */
    float lobby = nh_bake_light_level(L, nh_v3(12, 1.0f, 10), &bp);
    float office = nh_bake_light_level(L, nh_v3(28, 1.0f, 10), &bp);
    CHECK(lobby > office + 0.15f);
    /* Cutting the power darkens rooms on that circuit. */
    L->power[1] = 0;
    float cut = nh_bake_light_level(L, nh_v3(12, 1.0f, 10), &bp);
    CHECK(cut < lobby - 0.1f);
    L->power[1] = 1;
    /* Under a lamp is brighter than a corner of the same room. */
    float under = nh_bake_light_level(L, nh_v3(9, 1.0f, 9), &bp);
    float corner = nh_bake_light_level(L, nh_v3(17.5f, 1.0f, 15.5f), &bp);
    CHECK(under > corner);
}

static int s_scheme = NH_CONTROLS_SIMPLE;

static void run_ticks(NhPlayer *p, const Level *L, NhPlayerInput *in, int n)
{
    NhControlSettings cs;
    nh_controls_default(&cs);
    cs.scheme = s_scheme;
    for (int i = 0; i < n; i++)
        nh_player_tick(p, L, in, &cs, 4.0f / 3.0f, 62.0f);
}

void test_player(void)
{
    Level *L = fresh_level();
    NhPlayer p;
    NhPlayerInput in = { 0 };
    in.ir_valid = true;
    in.ir_x = 320;
    in.ir_y = 240;

    /* Pointer scheme. Pointer inside the box: no turning. Past the right edge: turns right. */
    s_scheme = NH_CONTROLS_POINTER;
    nh_player_init(&p, nh_v3(12, 0, 10), 0.0f);
    run_ticks(&p, L, &in, 30);
    CHECK_NEAR(p.yaw, 0.0, 1e-5);
    in.ir_x = 630;
    run_ticks(&p, L, &in, 30);
    CHECK(p.yaw < -0.2f);
    /* IR lost: the camera stops turning and the cursor holds. */
    float yaw = p.yaw, cx = p.cursor_x;
    in.ir_valid = false;
    run_ticks(&p, L, &in, 30);
    CHECK_NEAR(p.yaw, yaw, 1e-6);
    CHECK_NEAR(p.cursor_x, cx, 1e-6);
    s_scheme = NH_CONTROLS_SIMPLE;

    /* Simple scheme: the pointer at the edge doesn't turn; the stick does,
     * easing in, and stops easing out. */
    nh_player_init(&p, nh_v3(12, 0, 10), 0.0f);
    in = (NhPlayerInput){ 0 };
    in.ir_valid = true;
    in.ir_x = 630;
    in.ir_y = 240;
    run_ticks(&p, L, &in, 30);
    CHECK_NEAR(p.yaw, 0.0, 1e-5);
    in.turn = 1.0f;
    run_ticks(&p, L, &in, 1);
    float first = -p.yaw;
    run_ticks(&p, L, &in, 59);
    CHECK(first > 0.0f && first < 0.01f); /* no snap */
    CHECK(p.yaw < -1.5f);                 /* about 2.6 rad/s once up to speed */
    in.turn = 0.0f;
    yaw = p.yaw;
    run_ticks(&p, L, &in, 60);
    CHECK(p.yaw < yaw && p.yaw > yaw - 0.6f); /* glides to a stop */
    float settled = p.yaw;
    run_ticks(&p, L, &in, 30);
    CHECK_NEAR(p.yaw, settled, 1e-5);

    /* Full stick walks; only holding run runs (the open run up to the stairs). */
    nh_player_init(&p, nh_v3(9.0f, 0, 17.0f), NH_PI);
    in = (NhPlayerInput){ 0 };
    in.move_y = 1.0f;
    run_ticks(&p, L, &in, 20);
    CHECK(p.speed > 2.0f && p.speed < 2.6f);
    in.run_held = true;
    run_ticks(&p, L, &in, 15);
    CHECK(p.speed > 4.0f);

    /* Walking into a wall stops at the wall. */
    nh_player_init(&p, nh_v3(16, 0, 9), -NH_PI * 0.5f); /* facing +X toward the corridor wall */
    in = (NhPlayerInput){ 0 };
    in.move_y = 1.0f;
    run_ticks(&p, L, &in, 120);
    CHECK(p.pos.x < 18.0f - 0.3f);
    CHECK(p.pos.x > 17.0f);

    /* Climbing the stairs (cells x=4, z=9..11, ascending +Z) reaches floor 1. */
    nh_player_init(&p, nh_v3(9.0f, 0, 17.0f), NH_PI); /* facing +Z */
    run_ticks(&p, L, &in, 240);
    CHECK(p.floor == 1);
    CHECK_NEAR(p.pos.y, 3.6f, 0.05);

    /* Footsteps make noise; crouch-walking makes much less. */
    nh_player_init(&p, nh_v3(12, 0, 10), NH_PI);
    float loud = 0, quiet = 0;
    in.move_y = 0.6f;
    for (int i = 0; i < 120; i++) {
        run_ticks(&p, L, &in, 1);
        for (int k = 0; k < p.nnoise; k++)
            loud = fmaxf(loud, p.noises[k].radius);
    }
    in.crouch_held = true;
    nh_player_init(&p, nh_v3(12, 0, 10), NH_PI);
    for (int i = 0; i < 180; i++) {
        run_ticks(&p, L, &in, 1);
        for (int k = 0; k < p.nnoise; k++)
            quiet = fmaxf(quiet, p.noises[k].radius);
    }
    CHECK(loud > 0.0f);
    CHECK(quiet < loud * 0.5f);
}

static void *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

void test_assets(void)
{
    NhArena a;
    nh_arena_init(&a, s_mem, sizeof(s_mem));
    size_t len;
    /* Every prop in the table has a model. */
    for (int i = 0; i < PROP_COUNT; i++) {
        char path[128];
        snprintf(path, sizeof(path), "apps/NightHeist/data/mdl/%s.nmd", nh_prop_info[i].name);
        void *d = slurp(path, &len);
        CHECK(d != NULL);
        if (!d)
            continue;
        NhModel m;
        CHECK(nh_nmd_parse(d, len, &a, &m));
        CHECK(m.nparts >= 1 && !m.skinned);
        /* Every triangle faces the way its normals say (catches inverted art). */
        int bad = 0;
        for (int k = 0; k < m.nparts; k++) {
            const NhMesh *ms = &m.parts[k].mesh;
            for (uint32_t t = 0; t + 2 < ms->icount; t += 3) {
                NhVec3 p0 = nh_vert_pos(&ms->verts[ms->idx[t]]), p1 = nh_vert_pos(&ms->verts[ms->idx[t + 1]]),
                       p2 = nh_vert_pos(&ms->verts[ms->idx[t + 2]]);
                NhVec3 fn = nh_v3_cross(nh_v3_sub(p1, p0), nh_v3_sub(p2, p0));
                NhVec3 vn = nh_v3_add(nh_v3_add(nh_vert_nrm(&ms->verts[ms->idx[t]]), nh_vert_nrm(&ms->verts[ms->idx[t + 1]])),
                                      nh_vert_nrm(&ms->verts[ms->idx[t + 2]]));
                if (nh_v3_len(fn) > 1e-7f && nh_v3_dot(fn, vn) <= 0.0f)
                    bad++;
            }
        }
        if (bad)
            fprintf(stderr, "  prop %s: %d inverted triangles\n", nh_prop_info[i].name, bad);
        CHECK(bad == 0);
        free(d);
    }
    /* The guard: skinned, split so each draw uses at most 10 bones. */
    void *d = slurp("apps/NightHeist/data/mdl/guard.nmd", &len);
    CHECK(d != NULL);
    if (d) {
        NhModel m;
        CHECK(nh_nmd_parse(d, len, &a, &m));
        CHECK(m.skinned && m.nbones == 11);
        for (int i = 0; i < m.nparts; i++) {
            CHECK(m.parts[i].nslots <= NH_MAX_BONES);
            for (uint32_t v = 0; v < m.parts[i].mesh.vcount; v++)
                CHECK(m.parts[i].mesh.verts[v].bone < m.parts[i].nslots);
        }
        CHECK(nh_model_find_bone(&m, "head") >= 0);
        /* Bind pose skinning is the identity. */
        NhPose pose;
        nh_pose_bind(&m, &pose);
        NhMat34 world, bones[NH_SKEL_MAX];
        nh_m34_identity(&world);
        nh_pose_skin(&m, &pose, &world, bones);
        for (int b = 0; b < m.nbones; b++)
            CHECK_NEAR(bones[b].m[0][0] + bones[b].m[1][1] + bones[b].m[2][2], 3.0, 1e-3);
        free(d);
    }
    d = slurp("apps/NightHeist/data/anim/walk.nan", &len);
    CHECK(d != NULL);
    if (d) {
        NhAnimClip c;
        CHECK(nh_nan_parse(d, len, &c));
        CHECK(c.nbones == 11 && c.loop && c.nframes > 10);
        NhPose p;
        nh_anim_sample(&c, 0.37f, &p, c.nbones);
        float qn = p.rot[0].x * p.rot[0].x + p.rot[0].y * p.rot[0].y + p.rot[0].z * p.rot[0].z + p.rot[0].w * p.rot[0].w;
        CHECK_NEAR(qn, 1.0, 1e-3);
        free(d);
    }
    d = slurp("apps/NightHeist/data/tex/brick.ntx", &len);
    CHECK(d != NULL);
    if (d) {
        NhTexInfo ti;
        CHECK(nh_ntx_parse(d, len, &ti));
        CHECK(ti.format == NH_TF_CMPR && ti.width == 256 && ti.levels > 4);
        static uint8_t rgba[256 * 256 * 4];
        nh_tex_decode(ti.format, ti.data, ti.width, ti.height, rgba);
        /* Brick is reddish on average. */
        double r = 0, g = 0;
        for (int i = 0; i < 256 * 256; i++) {
            r += rgba[i * 4];
            g += rgba[i * 4 + 1];
        }
        CHECK(r > g);
        free(d);
    }
}
