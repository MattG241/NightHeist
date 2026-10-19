#include "nh_mesh.h"

#include <math.h>
#include <string.h>

#define OVERFLOW_FLAG 0x80000000u

bool nh_mesh_begin(NhMesh *m, NhArena *arena, uint32_t vcap, uint32_t icap, bool with_colors)
{
    memset(m, 0, sizeof(*m));
    if (vcap > 65535u)
        vcap = 65535u;
    m->verts = nh_arena_alloc(arena, sizeof(NhVertex) * vcap);
    m->idx = nh_arena_alloc(arena, sizeof(uint16_t) * icap);
    m->colors = with_colors ? nh_arena_alloc(arena, sizeof(NhColor) * vcap) : NULL;
    m->vcap = vcap;
    m->icap = icap;
    return m->verts && m->idx && (!with_colors || m->colors);
}

bool nh_mesh_overflowed(const NhMesh *m)
{
    return (m->vcap & OVERFLOW_FLAG) != 0;
}

static int16_t q16(float v, float scale)
{
    float s = v * scale;
    s = s < 0.0f ? s - 0.5f : s + 0.5f;
    if (s > 32767.0f)
        s = 32767.0f;
    if (s < -32768.0f)
        s = -32768.0f;
    return (int16_t)s;
}

static int8_t q8(float v)
{
    float s = v * 64.0f;
    s = s < 0.0f ? s - 0.5f : s + 0.5f;
    if (s > 127.0f)
        s = 127.0f;
    if (s < -128.0f)
        s = -128.0f;
    return (int8_t)s;
}

uint16_t nh_mesh_vert(NhMesh *m, NhVec3 p, NhVec3 n, float u, float v, NhColor c, uint8_t bone)
{
    uint32_t cap = m->vcap & ~OVERFLOW_FLAG;
    if (m->vcount >= cap) {
        m->vcap |= OVERFLOW_FLAG;
        return 0xFFFF;
    }
    NhVertex *vx = &m->verts[m->vcount];
    vx->pos[0] = q16(p.x, NH_POS_SCALE);
    vx->pos[1] = q16(p.y, NH_POS_SCALE);
    vx->pos[2] = q16(p.z, NH_POS_SCALE);
    vx->nrm[0] = q8(n.x);
    vx->nrm[1] = q8(n.y);
    vx->nrm[2] = q8(n.z);
    vx->bone = bone;
    vx->uv[0] = q16(u, NH_UV_SCALE);
    vx->uv[1] = q16(v, NH_UV_SCALE);
    if (m->colors)
        m->colors[m->vcount] = c;
    return (uint16_t)m->vcount++;
}

void nh_mesh_tri(NhMesh *m, uint16_t a, uint16_t b, uint16_t c)
{
    if (a == 0xFFFF || b == 0xFFFF || c == 0xFFFF)
        return;
    if (m->icount + 3 > m->icap) {
        m->vcap |= OVERFLOW_FLAG;
        return;
    }
    m->idx[m->icount++] = a;
    m->idx[m->icount++] = b;
    m->idx[m->icount++] = c;
}

void nh_mesh_quad(NhMesh *m, NhVec3 p0, NhVec3 p1, NhVec3 p2, NhVec3 p3, NhVec3 n, float u0, float v0, float u1,
                  float v1, int su, int sv, NhColor c, uint8_t bone)
{
    if (su < 1)
        su = 1;
    if (sv < 1)
        sv = 1;
    uint32_t base = m->vcount;
    /* Bilinear lattice: s runs p0->p1, t runs p0->p3. */
    for (int j = 0; j <= sv; j++) {
        float t = (float)j / (float)sv;
        NhVec3 a = nh_v3_lerp(p0, p3, t), b = nh_v3_lerp(p1, p2, t);
        for (int i = 0; i <= su; i++) {
            float s = (float)i / (float)su;
            nh_mesh_vert(m, nh_v3_lerp(a, b, s), n, nh_lerpf(u0, u1, s), nh_lerpf(v0, v1, t), c, bone);
        }
    }
    if (nh_mesh_overflowed(m))
        return;
    for (int j = 0; j < sv; j++) {
        for (int i = 0; i < su; i++) {
            uint16_t i0 = (uint16_t)(base + (uint32_t)(j * (su + 1) + i));
            uint16_t i1 = (uint16_t)(i0 + 1);
            uint16_t i3 = (uint16_t)(i0 + su + 1);
            uint16_t i2 = (uint16_t)(i3 + 1);
            nh_mesh_tri(m, i0, i1, i2);
            nh_mesh_tri(m, i0, i2, i3);
        }
    }
}

void nh_mesh_box(NhMesh *m, NhVec3 a, NhVec3 b, float s, NhColor c, uint8_t bone)
{
    /* +X */
    nh_mesh_quad(m, nh_v3(b.x, a.y, b.z), nh_v3(b.x, a.y, a.z), nh_v3(b.x, b.y, a.z), nh_v3(b.x, b.y, b.z),
                 nh_v3(1, 0, 0), b.z * s, a.y * s, a.z * s, b.y * s, 1, 1, c, bone);
    /* -X */
    nh_mesh_quad(m, nh_v3(a.x, a.y, a.z), nh_v3(a.x, a.y, b.z), nh_v3(a.x, b.y, b.z), nh_v3(a.x, b.y, a.z),
                 nh_v3(-1, 0, 0), a.z * s, a.y * s, b.z * s, b.y * s, 1, 1, c, bone);
    /* +Y */
    nh_mesh_quad(m, nh_v3(a.x, b.y, b.z), nh_v3(b.x, b.y, b.z), nh_v3(b.x, b.y, a.z), nh_v3(a.x, b.y, a.z),
                 nh_v3(0, 1, 0), a.x * s, b.z * s, b.x * s, a.z * s, 1, 1, c, bone);
    /* -Y */
    nh_mesh_quad(m, nh_v3(a.x, a.y, a.z), nh_v3(b.x, a.y, a.z), nh_v3(b.x, a.y, b.z), nh_v3(a.x, a.y, b.z),
                 nh_v3(0, -1, 0), a.x * s, a.z * s, b.x * s, b.z * s, 1, 1, c, bone);
    /* +Z */
    nh_mesh_quad(m, nh_v3(a.x, a.y, b.z), nh_v3(b.x, a.y, b.z), nh_v3(b.x, b.y, b.z), nh_v3(a.x, b.y, b.z),
                 nh_v3(0, 0, 1), a.x * s, a.y * s, b.x * s, b.y * s, 1, 1, c, bone);
    /* -Z */
    nh_mesh_quad(m, nh_v3(b.x, a.y, a.z), nh_v3(a.x, a.y, a.z), nh_v3(a.x, b.y, a.z), nh_v3(b.x, b.y, a.z),
                 nh_v3(0, 0, -1), b.x * s, a.y * s, a.x * s, b.y * s, 1, 1, c, bone);
}

void nh_mesh_compute_bounds(NhMesh *m)
{
    if (m->vcount == 0) {
        m->bmin = m->bmax = nh_v3(0, 0, 0);
        return;
    }
    NhVec3 lo = nh_vert_pos(&m->verts[0]), hi = lo;
    for (uint32_t i = 1; i < m->vcount; i++) {
        NhVec3 p = nh_vert_pos(&m->verts[i]);
        lo = nh_v3(fminf(lo.x, p.x), fminf(lo.y, p.y), fminf(lo.z, p.z));
        hi = nh_v3(fmaxf(hi.x, p.x), fmaxf(hi.y, p.y), fmaxf(hi.z, p.z));
    }
    m->bmin = lo;
    m->bmax = hi;
}
