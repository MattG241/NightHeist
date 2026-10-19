#include "world.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/nh_proptab.h"
#include "platform/platform.h"
#include "res.h"

#define BATCH_VCAP 20000u
#define BATCH_ICAP 40000u

static uint8_t mid(const char *name)
{
    return (uint8_t)res_material(name);
}

static const char *door_model(int kind)
{
    switch (kind) {
    case LV_DOOR_METAL: return "door_metal";
    case LV_DOOR_GLASS: return "door_glass";
    case LV_DOOR_VAULT: return "door_vault";
    case LV_DOOR_SHUTTER: return "door_shutter";
    default: return "door_wood";
    }
}

static void prop_matrix(const LvProp *p, NhMat34 *m)
{
    NhMat34 r, t;
    nh_m34_rot_y(&r, p->yaw);
    nh_m34_translate(&t, p->x, p->y, p->z);
    nh_m34_mul(m, &t, &r);
}

/* Merge a static prop's model into the room's batches. */
static void merge_prop(const LvProp *p, NhBatchSet *bs, int room)
{
    const ResModel *rm = res_model(nh_prop_info[p->type].name);
    if (!rm)
        return;
    NhMat34 m;
    prop_matrix(p, &m);
    for (int k = 0; k < rm->model.nparts; k++) {
        const NhMesh *src = &rm->model.parts[k].mesh;
        int mat = rm->part_mat[k];
        if (p->type == PROP_PAINTING && strcmp(res_mat_name(mat), "painting0") == 0) {
            char nm[16];
            snprintf(nm, sizeof(nm), "painting%d", p->state % 6);
            mat = res_material(nm);
        }
        NhMesh *dst = nh_batches_get(bs, room, NH_LAYER_ROOM, mat, src->vcount);
        if (!dst)
            continue;
        uint32_t base = dst->vcount;
        for (uint32_t i = 0; i < src->vcount; i++) {
            NhVec3 pos = nh_m34_mul_point(&m, nh_vert_pos(&src->verts[i]));
            NhVec3 n = nh_m34_mul_dir(&m, nh_vert_nrm(&src->verts[i]));
            NhColor c = src->colors ? src->colors[i] : nh_rgba(255, 255, 255, 255);
            nh_mesh_vert(dst, pos, n, src->verts[i].uv[0] / NH_UV_SCALE, src->verts[i].uv[1] / NH_UV_SCALE, c, 0);
        }
        for (uint32_t i = 0; i + 2 < src->icount; i += 3)
            nh_mesh_tri(dst, (uint16_t)(base + src->idx[i]), (uint16_t)(base + src->idx[i + 1]),
                        (uint16_t)(base + src->idx[i + 2]));
    }
}

static void tint_colors(NhMesh *m, NhColor t)
{
    for (uint32_t i = 0; i < m->vcount; i++) {
        NhColor *c = &m->colors[i];
        c->r = (uint8_t)(c->r * t.r / 255);
        c->g = (uint8_t)(c->g * t.g / 255);
        c->b = (uint8_t)(c->b * t.b / 255);
    }
}

static bool copy_batch(World *w, const NhBatch *b, NhArena *arena)
{
    if (w->nbatches >= WORLD_BATCHES_MAX || b->mesh.vcount == 0)
        return false;
    WorldBatch *wb = &w->batches[w->nbatches];
    memset(wb, 0, sizeof(*wb));
    NhMesh *m = &wb->cpu;
    if (!nh_mesh_begin(m, arena, b->mesh.vcount, b->mesh.icount, true))
        return false;
    wb->albedo = nh_arena_alloc(arena, sizeof(NhColor) * b->mesh.vcount);
    if (!wb->albedo)
        return false;
    memcpy(m->verts, b->mesh.verts, sizeof(NhVertex) * b->mesh.vcount);
    memcpy(m->idx, b->mesh.idx, sizeof(uint16_t) * b->mesh.icount);
    memcpy(wb->albedo, b->mesh.colors, sizeof(NhColor) * b->mesh.vcount);
    m->vcount = b->mesh.vcount;
    m->icount = b->mesh.icount;
    nh_mesh_compute_bounds(m);
    wb->bmin = m->bmin;
    wb->bmax = m->bmax;
    wb->room = b->room;
    wb->layer = b->layer;
    wb->mat = b->material;
    w->nbatches++;
    return true;
}

bool world_build(World *w, NhArena *arena, NhArena *scratch)
{
    uint64_t t0 = plat_time_us();
    Level *L = &w->L;
    nh_bake_default_params(&w->bake);
    for (int i = 0; i < MAT_COUNT; i++)
        w->mat_map[i] = i ? mid(nh_mat_names[i]) : 0;
    NhKitMaterials km = { mid("ceiling"), mid("roof"), mid("concrete"), mid("metal"), mid("metal"),
                          mid("facade"), mid("plaster"), mid("concrete"), mid("grass"), w->mat_map };
    w->tex_glow = res_texture("glow");
    w->tex_glass = res_texture("glass");
    /* Facade material comes from the outside room's wall material. */
    if (L->rooms[LV_ROOM_OUTSIDE].wall_mat)
        km.facade = w->mat_map[L->rooms[LV_ROOM_OUTSIDE].wall_mat];
    if (!L->rooms[LV_ROOM_OUTSIDE].floor_mat)
        L->rooms[LV_ROOM_OUTSIDE].floor_mat = MAT_ASPHALT;

    w->nbatches = 0;
    for (int r = LV_ROOM_OUTSIDE; r < L->nrooms; r++) {
        size_t mark = nh_arena_mark(scratch);
        NhBatchSet bs;
        nh_batches_init(&bs, scratch, BATCH_VCAP, BATCH_ICAP);
        nh_kit_build_room(L, r, &km, &bs);
        /* Wall paint tint. */
        int wallm = w->mat_map[L->rooms[r].wall_mat];
        for (int i = 0; i < bs.n; i++)
            if (bs.b[i].material == wallm && bs.b[i].layer == NH_LAYER_ROOM)
                tint_colors(&bs.b[i].mesh, L->rooms[r].wall_tint);
        for (int i = 0; i < L->nprops; i++) {
            const LvProp *p = &L->props[i];
            if (p->room == r && !(p->flags & (LV_P_DYNAMIC | LV_P_HIDDEN)))
                merge_prop(p, &bs, r);
        }
        if (bs.overflow)
            fprintf(stderr, "world: room %d geometry overflow\n", r);
        w->room_first[r] = (uint16_t)w->nbatches;
        for (int i = 0; i < bs.n; i++)
            copy_batch(w, &bs.b[i], arena);
        w->room_count[r] = (uint16_t)(w->nbatches - w->room_first[r]);
        nh_kit_room_bounds(L, r, &w->room_min[r], &w->room_max[r]);
        nh_arena_reset_to(scratch, mark);
    }
    for (int i = 0; i < w->nbatches; i++) {
        WorldBatch *wb = &w->batches[i];
        nh_bake_mesh(L, wb->room, &wb->cpu, wb->albedo, &w->bake);
        wb->gm = gfx_mesh_create(&wb->cpu, true);
    }
    for (int i = 0; i < L->nprops; i++) {
        const LvProp *p = &L->props[i];
        w->prop_ambient[i] = world_ambient_at(w, nh_v3(p->x, p->y + 0.5f, p->z));
        if (p->flags & LV_P_DYNAMIC)
            res_model(nh_prop_info[p->type].name);
    }
    for (int i = 0; i < L->ndoors; i++)
        res_model(door_model(L->doors[i].kind));
    nh_portals_build(L, &w->portals);
    w->build_ms = (uint32_t)((plat_time_us() - t0) / 1000);
    return true;
}

void world_free(World *w)
{
    for (int i = 0; i < w->nbatches; i++)
        gfx_mesh_destroy(w->batches[i].gm);
    w->nbatches = 0;
}

void world_rebake(World *w)
{
    for (int i = 0; i < w->nbatches; i++) {
        WorldBatch *wb = &w->batches[i];
        nh_bake_mesh(&w->L, wb->room, &wb->cpu, wb->albedo, &w->bake);
        gfx_mesh_set_colors(wb->gm, wb->cpu.colors);
    }
    for (int i = 0; i < w->L.nprops; i++) {
        const LvProp *p = &w->L.props[i];
        w->prop_ambient[i] = world_ambient_at(w, nh_v3(p->x, p->y + 0.5f, p->z));
    }
}

void world_update(World *w, float dt)
{
    for (int i = 0; i < w->L.ndoors; i++) {
        LvDoor *d = &w->L.doors[i];
        d->open = nh_approach(d->open, d->target, dt * (d->kind == LV_DOOR_VAULT ? 0.25f : 2.2f));
    }
}

static uint8_t to8(float v)
{
    v = v * 255.0f + 0.5f;
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

NhColor world_ambient_at(const World *w, NhVec3 p)
{
    NhVec3 c = nh_bake_sample(&w->L, p, nh_v3(0, 0, 0), 0, &w->bake);
    return nh_rgba(to8(fminf(c.x, 1.0f)), to8(fminf(c.y, 1.0f)), to8(fminf(c.z, 1.0f)), 255);
}

void world_view_proj(const WorldView *v, float out[16])
{
    float f = 1.0f / tanf(NH_DEG2RAD(v->fovy) * 0.5f);
    float n = v->znear, fa = v->zfar;
    float P[16] = { f / v->aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (fa + n) / (n - fa), 2.0f * fa * n / (n - fa), 0, 0, -1, 0 };
    float V[16];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            V[r * 4 + c] = v->view.m[r][c];
    V[12] = V[13] = V[14] = 0;
    V[15] = 1;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += P[r * 4 + k] * V[k * 4 + c];
            out[r * 4 + c] = s;
        }
}

/* AABB against the six frustum planes (Gribb/Hartmann). */
static bool aabb_visible(const float *vp, NhVec3 lo, NhVec3 hi)
{
    for (int p = 0; p < 6; p++) {
        int row = p >> 1;
        float s = (p & 1) ? -1.0f : 1.0f;
        float a = vp[12] + s * vp[row * 4 + 0], b = vp[13] + s * vp[row * 4 + 1];
        float c = vp[14] + s * vp[row * 4 + 2], d = vp[15] + s * vp[row * 4 + 3];
        float x = a > 0 ? hi.x : lo.x, y = b > 0 ? hi.y : lo.y, z = c > 0 ? hi.z : lo.z;
        if (a * x + b * y + c * z + d < 0)
            return false;
    }
    return true;
}

static void draw_model(const World *w, const ResModel *rm, const NhMat34 *m, NhColor ambient)
{
    (void)w;
    for (int k = 0; k < rm->model.nparts; k++) {
        GfxMaterial mat = *res_mat(rm->part_mat[k]);
        if (!(mat.flags & GFX_MAT_UNLIT)) {
            mat.flags &= (uint16_t)~GFX_MAT_BAKED;
            mat.flags |= GFX_MAT_VCOLOR;
            mat.ambient = ambient;
        }
        gfx_draw(rm->parts[k], &mat, m);
    }
}

static void draw_doors(World *w)
{
    const Level *L = &w->L;
    for (int i = 0; i < L->ndoors; i++) {
        const LvDoor *d = &L->doors[i];
        if (!w->visible[d->room_a == 0 ? LV_ROOM_OUTSIDE : d->room_a] &&
            !w->visible[d->room_b == 0 ? LV_ROOM_OUTSIDE : d->room_b])
            continue;
        const ResModel *rm = res_model(door_model(d->kind));
        if (!rm)
            continue;
        float base = lv_floor_y(d->floor), ang = d->open * NH_PI * 0.5f;
        NhVec3 hinge;
        float yaw;
        if (d->axis == 0) {
            hinge = nh_v3((float)d->x * LV_CELL, base, (float)d->z * LV_CELL + 0.5f);
            yaw = -NH_PI * 0.5f + ang;
        } else {
            hinge = nh_v3((float)d->x * LV_CELL + 0.5f, base, (float)d->z * LV_CELL);
            yaw = -ang;
        }
        if (d->kind == LV_DOOR_SHUTTER) {
            /* Shutters roll up instead of swinging. */
            yaw = d->axis == 0 ? -NH_PI * 0.5f : 0.0f;
            hinge.y += d->open * (LV_DOOR_H - 0.1f);
        }
        NhMat34 r, t, m;
        nh_m34_rot_y(&r, yaw);
        nh_m34_translate(&t, hinge.x, hinge.y, hinge.z);
        nh_m34_mul(&m, &t, &r);
        NhVec3 mid = nh_v3(hinge.x, base + 1.2f, hinge.z);
        draw_model(w, rm, &m, world_ambient_at(w, mid));
    }
}

static void draw_glass_and_glows(World *w, const WorldView *v)
{
    const Level *L = &w->L;
    /* Window panes in visible rooms. */
    for (int f = 0; f < L->floors; f++) {
        float base = lv_floor_y(f);
        for (int z = 0; z < L->d; z++)
            for (int x = 0; x < L->w; x++)
                for (int side = 0; side < 2; side++) {
                    const LvEdge *e = lv_edge_side_c(L, f, x, z, side);
                    if (!e || e->type != LV_E_WINDOW || e->ref != LV_WIN_CLOSED)
                        continue;
                    int ra = lv_room_at(L, f, x, z), rb = lv_neighbor_room(L, f, x, z, side);
                    ra = ra ? ra : LV_ROOM_OUTSIDE;
                    rb = rb ? rb : LV_ROOM_OUTSIDE;
                    if (!w->visible[ra] && !w->visible[rb])
                        continue;
                    NhVec3 c[4];
                    float y0 = base + LV_WIN_Y0, y1 = base + LV_WIN_Y1;
                    if (side == 0) {
                        float bx = (float)(x + 1) * LV_CELL, z0 = (float)z * LV_CELL;
                        c[0] = nh_v3(bx, y0, z0 + 0.3f);
                        c[1] = nh_v3(bx, y0, z0 + 1.7f);
                        c[2] = nh_v3(bx, y1, z0 + 1.7f);
                        c[3] = nh_v3(bx, y1, z0 + 0.3f);
                    } else {
                        float bz = (float)(z + 1) * LV_CELL, x0 = (float)x * LV_CELL;
                        c[0] = nh_v3(x0 + 0.3f, y0, bz);
                        c[1] = nh_v3(x0 + 1.7f, y0, bz);
                        c[2] = nh_v3(x0 + 1.7f, y1, bz);
                        c[3] = nh_v3(x0 + 0.3f, y1, bz);
                    }
                    gfx_draw_quad3d(w->tex_glass, c, 0, 0, 1, 1, nh_rgba(90, 110, 130, 90), GFX_MAT_BLEND);
                }
    }
    /* Lamp glows (camera-facing, additive). */
    NhVec3 right = nh_v3(v->view.m[0][0], v->view.m[0][1], v->view.m[0][2]);
    NhVec3 up = nh_v3(v->view.m[1][0], v->view.m[1][1], v->view.m[1][2]);
    for (int i = 0; i < L->nlights; i++) {
        const LvLight *l = &L->lights[i];
        int room = l->room ? l->room : LV_ROOM_OUTSIDE;
        if (!w->visible[room] || !l->on || !(l->emergency || L->power[l->circuit & 15]))
            continue;
        if (l->kind == LV_LIGHT_WINDOW)
            continue;
        float s = l->kind == LV_LIGHT_SIGN ? 0.35f : (l->kind == LV_LIGHT_STREET ? 1.6f : 0.7f);
        NhVec3 p = l->pos;
        if (l->kind == LV_LIGHT_CEILING)
            p.y -= 0.12f;
        NhVec3 r = nh_v3_scale(right, s), u = nh_v3_scale(up, s);
        NhVec3 c[4] = { nh_v3_sub(nh_v3_sub(p, r), u), nh_v3_sub(nh_v3_add(p, r), u),
                        nh_v3_add(nh_v3_add(p, r), u), nh_v3_add(nh_v3_sub(p, r), u) };
        NhColor col = l->color;
        col.a = 70;
        gfx_draw_quad3d(w->tex_glow, c, 0, 0, 1, 1, col, GFX_MAT_ADD);
    }
}

void world_draw(World *w, const WorldView *v)
{
    float vp[16];
    world_view_proj(v, vp);
    const Level *L = &w->L;
    memset(w->visible, 0, sizeof(w->visible));
    if (v->in_vent) {
        w->nvisible = 0;
        for (int i = 0; i < w->nbatches; i++) {
            const WorldBatch *wb = &w->batches[i];
            if (wb->layer == NH_LAYER_VENT && L->rooms[wb->room].floor == v->vent_floor &&
                aabb_visible(vp, wb->bmin, wb->bmax))
                gfx_draw(wb->gm, res_mat(wb->mat), &(NhMat34){ { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } });
        }
        return;
    }
    w->cam_room = nh_room_at_point(L, v->eye);
    w->nvisible = nh_portals_visible(L, &w->portals, vp, v->eye, w->cam_room, w->visible);
    NhMat34 ident;
    nh_m34_identity(&ident);
    /* Opaque first, then blended surfaces (glass) so they composite over it. */
    for (int pass = 0; pass < 2; pass++) {
        for (int r = LV_ROOM_OUTSIDE; r < L->nrooms; r++) {
            if (!w->visible[r] || !aabb_visible(vp, w->room_min[r], w->room_max[r]))
                continue;
            for (int i = w->room_first[r]; i < w->room_first[r] + w->room_count[r]; i++) {
                const WorldBatch *wb = &w->batches[i];
                const GfxMaterial *m = res_mat(wb->mat);
                bool blended = (m->flags & (GFX_MAT_BLEND | GFX_MAT_ADD)) != 0;
                if (blended != (pass == 1) || wb->layer != NH_LAYER_ROOM || !aabb_visible(vp, wb->bmin, wb->bmax))
                    continue;
                gfx_draw(wb->gm, m, &ident);
            }
        }
        if (pass == 0) {
            /* Dynamic props and doors are opaque; draw them before the glass. */
            for (int i = 0; i < L->nprops; i++) {
                const LvProp *p = &L->props[i];
                if (!(p->flags & LV_P_DYNAMIC) || (p->flags & LV_P_HIDDEN) || !w->visible[p->room ? p->room : 1])
                    continue;
                const ResModel *rm = res_model(nh_prop_info[p->type].name);
                if (!rm)
                    continue;
                NhMat34 m;
                prop_matrix(p, &m);
                draw_model(w, rm, &m, w->prop_ambient[i]);
            }
            draw_doors(w);
            if (v->draw_opaque)
                v->draw_opaque(v->ctx);
        }
    }
    draw_glass_and_glows(w, v);
}
