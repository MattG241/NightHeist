#include "nh_assets.h"

#include <math.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static float rdf(const uint8_t *p)
{
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static size_t pad4(size_t x)
{
    return (x + 3u) & ~(size_t)3u;
}

bool nh_ntx_parse(const void *data, size_t len, NhTexInfo *out)
{
    const uint8_t *p = data;
    if (len < 32 || memcmp(p, "NTX1", 4) != 0)
        return false;
    out->width = rd16(p + 4);
    out->height = rd16(p + 6);
    out->format = (NhTexFormat)p[8];
    out->levels = p[9];
    out->repeat = p[10] != 0;
    out->filter = p[11] != 0;
    out->size = rd32(p + 12);
    out->data = p + 32;
    if (out->levels < 1 || 32 + out->size > len)
        return false;
    return nh_tex_total_size(out->format, out->width, out->height, out->levels) == out->size;
}

static void bone_global(const NhModel *m, const NhQuat *rot, const NhVec3 *pos, int b, NhMat34 *out)
{
    NhMat34 local;
    nh_m34_from_quat_pos(&local, rot[b], pos[b]);
    if (m->bones[b].parent >= 0) {
        NhMat34 parent;
        bone_global(m, rot, pos, m->bones[b].parent, &parent);
        nh_m34_mul(out, &parent, &local);
    } else {
        *out = local;
    }
}

bool nh_nmd_parse(const void *data, size_t len, NhArena *arena, NhModel *out)
{
    const uint8_t *p = data;
    if (len < 48 || memcmp(p, "NMD1", 4) != 0)
        return false;
    memset(out, 0, sizeof(*out));
    uint16_t flags = rd16(p + 4);
    out->nbones = rd16(p + 6);
    out->nparts = rd16(p + 8);
    if (out->nbones > NH_SKEL_MAX || out->nparts > NH_MODEL_PARTS_MAX || out->nparts < 1)
        return false;
    out->bmin = nh_v3(rdf(p + 12), rdf(p + 16), rdf(p + 20));
    out->bmax = nh_v3(rdf(p + 24), rdf(p + 28), rdf(p + 32));
    out->skinned = out->nbones > 0;
    size_t off = 48;
    if (off + (size_t)out->nbones * 64 + (size_t)out->nparts * 64 > len)
        return false;
    for (int i = 0; i < out->nbones; i++, off += 64) {
        NhBone *b = &out->bones[i];
        memcpy(b->name, p + off, NH_NAME_LEN);
        b->name[NH_NAME_LEN - 1] = 0;
        b->parent = (int16_t)rd16(p + off + 32);
        if (b->parent >= i)
            return false; /* parents must come first */
        b->pos = nh_v3(rdf(p + off + 36), rdf(p + off + 40), rdf(p + off + 44));
        b->rot = nh_quat_norm((NhQuat){ rdf(p + off + 48), rdf(p + off + 52), rdf(p + off + 56), rdf(p + off + 60) });
    }
    /* Inverse bind matrices from the bind pose. */
    NhPose bind;
    nh_pose_bind(out, &bind);
    for (int i = 0; i < out->nbones; i++) {
        NhMat34 g;
        bone_global(out, bind.rot, bind.pos, i, &g);
        nh_m34_inverse(&out->bones[i].inv_bind, &g);
    }
    size_t part_hdr = off;
    off += (size_t)out->nparts * 64;
    for (int i = 0; i < out->nparts; i++) {
        const uint8_t *h = p + part_hdr + (size_t)i * 64;
        NhModelPart *pt = &out->parts[i];
        memcpy(pt->material, h, NH_NAME_LEN);
        pt->material[NH_NAME_LEN - 1] = 0;
        uint32_t vc = rd32(h + 32), ic = rd32(h + 36);
        pt->nslots = h[40];
        if (pt->nslots > NH_MAX_BONES || vc > 65535u)
            return false;
        memcpy(pt->slots, h + 41, NH_MAX_BONES);
        size_t need = pad4(vc * 14) + ((flags & 1) ? vc * 4 : 0) + pad4(ic * 2);
        if (off + need > len)
            return false;
        if (!nh_mesh_begin(&pt->mesh, arena, vc, ic, (flags & 1) != 0))
            return false;
        for (uint32_t v = 0; v < vc; v++) {
            const uint8_t *s = p + off + v * 14;
            NhVertex *d = &pt->mesh.verts[v];
            d->pos[0] = (int16_t)rd16(s);
            d->pos[1] = (int16_t)rd16(s + 2);
            d->pos[2] = (int16_t)rd16(s + 4);
            d->nrm[0] = (int8_t)s[6];
            d->nrm[1] = (int8_t)s[7];
            d->nrm[2] = (int8_t)s[8];
            d->bone = s[9];
            d->uv[0] = (int16_t)rd16(s + 10);
            d->uv[1] = (int16_t)rd16(s + 12);
        }
        off += pad4(vc * 14);
        if (flags & 1) {
            memcpy(pt->mesh.colors, p + off, vc * 4);
            off += vc * 4;
        }
        for (uint32_t k = 0; k < ic; k++) {
            uint16_t ix = rd16(p + off + k * 2);
            if (ix >= vc)
                return false;
            pt->mesh.idx[k] = ix;
        }
        off += pad4(ic * 2);
        pt->mesh.vcount = vc;
        pt->mesh.icount = ic;
        pt->mesh.skinned = out->skinned;
        nh_mesh_compute_bounds(&pt->mesh);
    }
    return true;
}

int nh_model_find_bone(const NhModel *m, const char *name)
{
    for (int i = 0; i < m->nbones; i++)
        if (strcmp(m->bones[i].name, name) == 0)
            return i;
    return -1;
}

bool nh_nan_parse(const void *data, size_t len, NhAnimClip *out)
{
    const uint8_t *p = data;
    if (len < 48 || memcmp(p, "NAN1", 4) != 0)
        return false;
    out->nbones = rd16(p + 4);
    out->nframes = rd16(p + 6);
    out->fps = rdf(p + 8);
    out->loop = p[12] != 0;
    memcpy(out->name, p + 16, NH_NAME_LEN);
    out->name[NH_NAME_LEN - 1] = 0;
    out->frames = p + 48;
    if (out->nbones > NH_SKEL_MAX || out->nframes < 1 || out->fps <= 0.0f)
        return false;
    return 48 + (size_t)out->nframes * (size_t)out->nbones * 14 <= len;
}

void nh_pose_bind(const NhModel *m, NhPose *out)
{
    for (int i = 0; i < m->nbones; i++) {
        out->rot[i] = m->bones[i].rot;
        out->pos[i] = m->bones[i].pos;
    }
}

float nh_anim_duration(const NhAnimClip *clip)
{
    return (float)(clip->nframes - (clip->loop ? 0 : 1)) / clip->fps;
}

static void read_frame_bone(const NhAnimClip *c, int f, int b, NhQuat *q, NhVec3 *t)
{
    const uint8_t *s = c->frames + ((size_t)f * (size_t)c->nbones + (size_t)b) * 14;
    q->x = (int16_t)rd16(s) / 32767.0f;
    q->y = (int16_t)rd16(s + 2) / 32767.0f;
    q->z = (int16_t)rd16(s + 4) / 32767.0f;
    q->w = (int16_t)rd16(s + 6) / 32767.0f;
    t->x = (int16_t)rd16(s + 8) / 256.0f;
    t->y = (int16_t)rd16(s + 10) / 256.0f;
    t->z = (int16_t)rd16(s + 12) / 256.0f;
}

void nh_anim_sample(const NhAnimClip *clip, float t, NhPose *out, int nbones)
{
    float ft = t * clip->fps;
    int n = clip->nframes;
    int f0, f1;
    float a;
    if (clip->loop) {
        ft = fmodf(ft, (float)n);
        if (ft < 0)
            ft += (float)n;
        f0 = (int)ft;
        f1 = (f0 + 1) % n;
        a = ft - (float)f0;
    } else {
        ft = NH_CLAMP(ft, 0.0f, (float)(n - 1));
        f0 = (int)ft;
        f1 = NH_MIN(f0 + 1, n - 1);
        a = ft - (float)f0;
    }
    int nb = NH_MIN(nbones, clip->nbones);
    for (int b = 0; b < nb; b++) {
        NhQuat q0, q1;
        NhVec3 t0, t1;
        read_frame_bone(clip, f0, b, &q0, &t0);
        read_frame_bone(clip, f1, b, &q1, &t1);
        out->rot[b] = nh_quat_nlerp(q0, q1, a);
        out->pos[b] = nh_v3_lerp(t0, t1, a);
    }
}

void nh_pose_blend(const NhPose *a, const NhPose *b, float w, int nbones, NhPose *out)
{
    for (int i = 0; i < nbones; i++) {
        out->rot[i] = nh_quat_nlerp(a->rot[i], b->rot[i], w);
        out->pos[i] = nh_v3_lerp(a->pos[i], b->pos[i], w);
    }
}

void nh_pose_skin(const NhModel *m, const NhPose *pose, const NhMat34 *world, NhMat34 *out_bones)
{
    NhMat34 global[NH_SKEL_MAX];
    for (int i = 0; i < m->nbones; i++) {
        NhMat34 local;
        nh_m34_from_quat_pos(&local, pose->rot[i], pose->pos[i]);
        if (m->bones[i].parent >= 0)
            nh_m34_mul(&global[i], &global[m->bones[i].parent], &local);
        else
            global[i] = local;
        NhMat34 skin;
        nh_m34_mul(&skin, &global[i], &m->bones[i].inv_bind);
        nh_m34_mul(&out_bones[i], world, &skin);
    }
}

void nh_pose_bone_global(const NhModel *m, const NhPose *pose, int bone, NhMat34 *out)
{
    bone_global(m, pose->rot, pose->pos, bone, out);
}
