#include "guards.h"

#include <math.h>
#include <string.h>

#include "res.h"

static const char *const k_guard_clips[ANIM_COUNT] = {
    [ANIM_IDLE] = "idle",       [ANIM_WALK] = "walk",        [ANIM_WALK_TORCH] = "walk_torch",
    [ANIM_RUN] = "run",         [ANIM_LOOK] = "look_around", [ANIM_SUSPICIOUS] = "suspicious",
    [ANIM_ALERT] = "alert",     [ANIM_SEARCH] = "search",    [ANIM_RADIO] = "radio",
    [ANIM_SMOKE] = "smoke",     [ANIM_CHAT] = "chat",        [ANIM_NAP] = "nap",
    [ANIM_GRAB] = "grab",       [ANIM_KO] = "knockout",      [ANIM_DRAGGED] = "dragged",
};

static const char *const k_dog_clips[ANIM_COUNT] = {
    [ANIM_IDLE] = "dog_idle",   [ANIM_WALK] = "dog_walk",    [ANIM_WALK_TORCH] = "dog_walk",
    [ANIM_RUN] = "dog_run",     [ANIM_LOOK] = "dog_sniff",   [ANIM_SUSPICIOUS] = "dog_sniff",
    [ANIM_ALERT] = "dog_bark",  [ANIM_SEARCH] = "dog_sniff", [ANIM_RADIO] = "dog_idle",
    [ANIM_SMOKE] = "dog_idle",  [ANIM_CHAT] = "dog_idle",    [ANIM_NAP] = "dog_sleep",
    [ANIM_GRAB] = "dog_bark",   [ANIM_KO] = "dog_sleep",     [ANIM_DRAGGED] = "dog_sleep",
};

static const NhAnimClip *clip_for(const AiGuard *g, int anim)
{
    const char *n = g->arch == ARCH_DOG ? k_dog_clips[anim] : k_guard_clips[anim];
    return n ? res_anim(n) : NULL;
}

static const ResModel *model_for(const AiGuard *g)
{
    return res_model(g->arch == ARCH_DOG ? "dog" : "guard");
}

void guards_vis_init(GuardsVis *gv, const AiWorld *ai)
{
    memset(gv, 0, sizeof(*gv));
    for (int i = 0; i < ai->n; i++) {
        GuardVis *v = &gv->g[i];
        v->clip = v->prev_clip = ai->g[i].anim;
        v->blend = 1.0f;
        v->t = (float)i * 0.37f; /* desynchronise idle loops */
        v->prev_pos = ai->g[i].pos;
        v->prev_yaw = ai->g[i].yaw;
    }
}

void guards_vis_pre_tick(GuardsVis *gv, const AiWorld *ai)
{
    for (int i = 0; i < ai->n; i++) {
        gv->g[i].prev_pos = ai->g[i].pos;
        gv->g[i].prev_yaw = ai->g[i].yaw;
    }
}

void guards_vis_tick(GuardsVis *gv, const AiWorld *ai, float dt)
{
    for (int i = 0; i < ai->n; i++) {
        const AiGuard *g = &ai->g[i];
        GuardVis *v = &gv->g[i];
        if (g->anim != v->clip) {
            v->prev_clip = v->clip;
            v->prev_t = v->t;
            v->clip = g->anim;
            v->t = 0.0f;
            v->blend = 0.0f;
        }
        /* Locomotion clips play at the speed the guard actually moves. */
        float rate = 1.0f;
        if (v->clip == ANIM_WALK || v->clip == ANIM_WALK_TORCH)
            rate = NH_CLAMP(g->speed / 1.4f, 0.5f, 1.6f);
        else if (v->clip == ANIM_RUN)
            rate = NH_CLAMP(g->speed / 4.0f, 0.6f, 1.4f);
        if (g->arch == ARCH_DOG && v->clip == ANIM_WALK)
            rate = NH_CLAMP(g->speed / 1.6f, 0.5f, 2.0f);
        v->t += dt * rate;
        v->prev_t += dt;
        v->blend = NH_MIN(1.0f, v->blend + dt / 0.25f);
    }
}

void guards_root(const GuardsVis *gv, const AiWorld *ai, int i, float alpha, NhVec3 *pos, float *yaw)
{
    const AiGuard *g = &ai->g[i];
    const GuardVis *v = &gv->g[i];
    *pos = nh_v3_lerp(v->prev_pos, g->pos, alpha);
    *yaw = v->prev_yaw + nh_wrap_angle(g->yaw - v->prev_yaw) * alpha;
}

static void guard_pose(const GuardsVis *gv, const AiWorld *ai, int i, const NhModel *m, NhPose *pose)
{
    const AiGuard *g = &ai->g[i];
    const GuardVis *v = &gv->g[i];
    const NhAnimClip *cur = clip_for(g, v->clip), *prev = clip_for(g, v->prev_clip);
    nh_pose_bind(m, pose);
    if (cur)
        nh_anim_sample(cur, v->t, pose, m->nbones);
    if (prev && v->blend < 1.0f) {
        NhPose p;
        nh_pose_bind(m, &p);
        nh_anim_sample(prev, v->prev_t, &p, m->nbones);
        nh_pose_blend(&p, pose, nh_smoothstep(0.0f, 1.0f, v->blend), m->nbones, pose);
    }
    /* Head turn layered on top (looking around while standing). */
    if (g->arch != ARCH_DOG && fabsf(g->head_yaw) > 0.01f && g->state != AS_KO) {
        int hb = nh_model_find_bone(m, "head");
        if (hb >= 0) {
            float s = sinf(g->head_yaw * 0.5f), c = cosf(g->head_yaw * 0.5f);
            NhQuat q = { 0.0f, s, 0.0f, c };
            NhQuat r = pose->rot[hb];
            /* pose = yaw * pose */
            pose->rot[hb] = (NhQuat){ q.w * r.x + q.x * r.w + q.y * r.z - q.z * r.y,
                                      q.w * r.y - q.x * r.z + q.y * r.w + q.z * r.x,
                                      q.w * r.z + q.x * r.y - q.y * r.x + q.z * r.w,
                                      q.w * r.w - q.x * r.x - q.y * r.y - q.z * r.z };
        }
    }
}

static void root_matrix(NhVec3 pos, float yaw, NhMat34 *out)
{
    NhMat34 t, r;
    nh_m34_translate(&t, pos.x, pos.y, pos.z);
    nh_m34_rot_y(&r, yaw);
    nh_m34_mul(out, &t, &r);
}

void guards_torch(const GuardsVis *gv, const AiWorld *ai, int i, float alpha, NhVec3 *pos, NhVec3 *dir)
{
    const AiGuard *g = &ai->g[i];
    NhVec3 root;
    float yaw;
    guards_root(gv, ai, i, alpha, &root, &yaw);
    const ResModel *rm = model_for(g);
    NhMat34 world;
    root_matrix(root, yaw, &world);
    int fb = rm ? nh_model_find_bone(&rm->model, "farm_r") : -1;
    if (fb >= 0) {
        NhPose pose;
        guard_pose(gv, ai, i, &rm->model, &pose);
        NhMat34 bg, wb;
        nh_pose_bone_global(&rm->model, &pose, fb, &bg);
        nh_m34_mul(&wb, &world, &bg);
        *pos = nh_m34_mul_point(&wb, nh_v3(0.0f, -0.31f, -0.27f));
    } else {
        *pos = nh_m34_mul_point(&world, nh_v3(0.21f, 1.1f, -0.35f));
    }
    /* Beam follows where the guard looks, angled down a little. */
    float pitch = g->state == AS_SEARCH || g->state == AS_INVESTIGATE ? -0.22f : -0.15f;
    *dir = nh_dir_from_yaw_pitch(yaw + g->head_yaw, pitch);
}

static bool guard_visible(const AiWorld *ai, const World *w, int i)
{
    const AiGuard *g = &ai->g[i];
    if (!g->active)
        return false;
    int f = lv_floor_of(g->pos.y + 0.3f);
    int room = lv_room_at(&w->L, f, lv_cell_of(g->pos.x), lv_cell_of(g->pos.z));
    if (room == LV_ROOM_VOID)
        room = LV_ROOM_OUTSIDE;
    return w->visible[room] != 0;
}

int guards_lights(const GuardsVis *gv, const AiWorld *ai, const World *w, NhVec3 eye, float alpha,
                  GfxProjector *proj, NhLight *lights, int max)
{
    static GfxTexture cookie;
    if (!cookie)
        cookie = res_texture("cookie");
    /* Rank lit torches by distance; the nearest gets the projected cookie. */
    int order[AI_MAX_GUARDS], n = 0;
    float dist[AI_MAX_GUARDS];
    for (int i = 0; i < ai->n; i++) {
        if (!ai->g[i].active || !ai->g[i].torch_on)
            continue;
        dist[i] = nh_v3_dist(ai->g[i].pos, eye);
        if (dist[i] > 30.0f)
            continue;
        order[n++] = i;
    }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (dist[order[b]] < dist[order[a]]) {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
    memset(proj, 0, sizeof(*proj));
    int nl = 0;
    for (int k = 0; k < n; k++) {
        NhVec3 p, d;
        guards_torch(gv, ai, order[k], alpha, &p, &d);
        NhColor col = nh_rgba(235, 228, 205, 255);
        NhLight l;
        nh_light_init(&l, p, col);
        nh_light_set_spot(&l, d, 26.0f, NH_SPOT_COS);
        nh_light_set_dist(&l, 10.0f, 0.5f, NH_DIST_MEDIUM);
        if (k == 0) {
            proj->enabled = true;
            proj->pos = p;
            proj->dir = d;
            proj->fov_deg = 46.0f;
            proj->cookie = cookie;
            proj->light = l;
        } else if (nl < max) {
            lights[nl++] = l;
        }
    }
    (void)w;
    return nl;
}

void guards_draw(const GuardsVis *gv, const AiWorld *ai, const World *w, float alpha)
{
    for (int i = 0; i < ai->n; i++) {
        if (!guard_visible(ai, w, i))
            continue;
        const AiGuard *g = &ai->g[i];
        const ResModel *rm = model_for(g);
        if (!rm || !rm->loaded)
            continue;
        NhVec3 root;
        float yaw;
        guards_root(gv, ai, i, alpha, &root, &yaw);
        NhMat34 world;
        root_matrix(root, yaw, &world);
        NhPose pose;
        guard_pose(gv, ai, i, &rm->model, &pose);
        NhMat34 skin[NH_SKEL_MAX];
        nh_pose_skin(&rm->model, &pose, &world, skin);
        NhColor amb = world_ambient_at(w, nh_v3_add(root, nh_v3(0, 1.0f, 0)));
        for (int k = 0; k < rm->model.nparts; k++) {
            const NhModelPart *part = &rm->model.parts[k];
            NhMat34 slots[NH_MAX_BONES];
            for (int s = 0; s < part->nslots; s++)
                slots[s] = skin[part->slots[s]];
            GfxMaterial mat = *res_mat(rm->part_mat[k]);
            if (!(mat.flags & GFX_MAT_UNLIT)) {
                mat.flags &= (uint16_t)~GFX_MAT_BAKED;
                mat.ambient = amb;
            }
            mat.flags |= GFX_MAT_WARM | GFX_MAT_VCOLOR;
            gfx_draw_skinned(rm->parts[k], &mat, slots, part->nslots);
        }
    }
}

void guards_draw_glows(const GuardsVis *gv, const AiWorld *ai, const World *w, const NhMat34 *view, float alpha)
{
    NhVec3 right = nh_v3(view->m[0][0], view->m[0][1], view->m[0][2]);
    NhVec3 up = nh_v3(view->m[1][0], view->m[1][1], view->m[1][2]);
    NhVec3 fwd = nh_v3(-view->m[2][0], -view->m[2][1], -view->m[2][2]);
    for (int i = 0; i < ai->n; i++) {
        if (!ai->g[i].torch_on || !guard_visible(ai, w, i))
            continue;
        NhVec3 p, d;
        guards_torch(gv, ai, i, alpha, &p, &d);
        /* Brighter when the torch points at the camera. */
        float facing = NH_MAX(0.0f, -nh_v3_dot(d, fwd));
        float s = 0.12f + 0.45f * facing * facing;
        NhVec3 r = nh_v3_scale(right, s), u = nh_v3_scale(up, s);
        NhVec3 c[4] = { nh_v3_sub(nh_v3_sub(p, r), u), nh_v3_sub(nh_v3_add(p, r), u),
                        nh_v3_add(nh_v3_add(p, r), u), nh_v3_add(nh_v3_sub(p, r), u) };
        gfx_draw_quad3d(w->tex_glow, c, 0, 0, 1, 1, nh_rgba(255, 245, 215, (uint8_t)(90 + 140 * facing)),
                        GFX_MAT_ADD);
    }
}

static void floor_quad(const World *w, NhVec3 a, NhVec3 b, NhVec3 c, NhVec3 d, NhColor col)
{
    NhVec3 q[4] = { a, b, c, d };
    for (int k = 0; k < 4; k++)
        q[k].y += 0.04f;
    gfx_draw_quad3d(w->tex_glow, q, 0.5f, 0.5f, 0.5f, 0.5f, col, GFX_MAT_ADD | GFX_MAT_NOCULL);
}

void guards_draw_debug(const AiWorld *ai, const World *w)
{
    for (int i = 0; i < ai->n; i++) {
        const AiGuard *g = &ai->g[i];
        if (!g->active || g->state == AS_KO || !guard_visible(ai, w, i))
            continue;
        const AiArchInfo *a = &nh_ai_arch[g->arch];
        NhColor col = g->state == AS_ALERT                                ? nh_rgba(200, 30, 20, 60)
                      : g->suspicion > 0.3f                               ? nh_rgba(200, 150, 20, 50)
                                                                          : nh_rgba(40, 120, 200, 40);
        /* View cone as a fan of floor quads at full-light range. */
        float half = a->fov_deg * 0.5f * NH_PI / 180.0f, range = a->view_range;
        float yaw = g->yaw + g->head_yaw;
        const int seg = 8;
        for (int s = 0; s < seg; s++) {
            float a0 = yaw - half + 2.0f * half * (float)s / (float)seg;
            float a1 = yaw - half + 2.0f * half * (float)(s + 1) / (float)seg;
            NhVec3 d0 = nh_dir_from_yaw_pitch(a0, 0), d1 = nh_dir_from_yaw_pitch(a1, 0);
            NhVec3 p0 = g->pos;
            NhVec3 p1 = nh_v3_add(g->pos, nh_v3_scale(d0, range)), p2 = nh_v3_add(g->pos, nh_v3_scale(d1, range));
            floor_quad(w, p0, p1, p2, p0, col);
        }
        /* Path */
        if (g->has_goal) {
            for (int k = g->path_i; k < g->path.n; k++) {
                NhVec3 p0 = k == g->path_i ? g->pos : g->path.p[k - 1], p1 = g->path.p[k];
                NhVec3 dir = nh_v3_norm(nh_v3_sub(p1, p0));
                NhVec3 side = nh_v3(-dir.z * 0.06f, 0, dir.x * 0.06f);
                floor_quad(w, nh_v3_sub(p0, side), nh_v3_sub(p1, side), nh_v3_add(p1, side), nh_v3_add(p0, side),
                           nh_rgba(40, 220, 80, 160));
            }
        }
        /* Last known position marker. */
        if (g->state == AS_ALERT || g->state == AS_SEARCH) {
            NhVec3 p = g->lkp;
            floor_quad(w, nh_v3(p.x - 0.3f, p.y, p.z - 0.3f), nh_v3(p.x + 0.3f, p.y, p.z - 0.3f),
                       nh_v3(p.x + 0.3f, p.y, p.z + 0.3f), nh_v3(p.x - 0.3f, p.y, p.z + 0.3f),
                       nh_rgba(230, 40, 40, 180));
        }
    }
}
