/* Night Heist - compact vertex format and mesh builder.
 *
 * NhVertex is the in-memory and on-disk vertex for all 3D geometry. It maps
 * directly onto GX vertex attributes (s16 positions, s8 normals, s16 UVs) so
 * the Wii backend can DMA it without conversion:
 *   position = pos / 2^NH_POS_FRAC metres   (+-256 m at 1/128 m precision)
 *   normal   = nrm / 2^NH_NRM_FRAC          (GX's fixed s8 normal scale)
 *   uv       = uv  / 2^NH_UV_FRAC           (+-128 repeats at 1/256 precision)
 * Colours live in a separate RGBA8 array so baked lighting can be rewritten
 * (lights switched off, power cut) without touching geometry. */
#ifndef NH_MESH_H
#define NH_MESH_H

#include "nh_arena.h"
#include "nh_math.h"

#define NH_POS_FRAC 7
#define NH_NRM_FRAC 6
#define NH_UV_FRAC 8
#define NH_POS_SCALE 128.0f
#define NH_UV_SCALE 256.0f
#define NH_MAX_BONES 10 /* GX position matrix slots usable per draw */

typedef struct NhVertex {
    int16_t pos[3];
    int8_t nrm[3];
    uint8_t bone; /* matrix slot for skinned meshes, 0..NH_MAX_BONES-1 */
    int16_t uv[2];
} NhVertex;

typedef struct NhMesh {
    NhVertex *verts;
    NhColor *colors; /* may be NULL (white) */
    uint16_t *idx;   /* triangle list */
    uint32_t vcount, icount;
    uint32_t vcap, icap;
    bool skinned;
    NhVec3 bmin, bmax; /* model-space bounds */
} NhMesh;

/* Fixed-capacity builder over arena memory. Overflow sets a flag and drops
 * further geometry instead of corrupting memory. */
bool nh_mesh_begin(NhMesh *m, NhArena *arena, uint32_t vcap, uint32_t icap, bool with_colors);
bool nh_mesh_overflowed(const NhMesh *m);
/* Returns the new vertex index or 0xFFFF on overflow. */
uint16_t nh_mesh_vert(NhMesh *m, NhVec3 p, NhVec3 n, float u, float v, NhColor c, uint8_t bone);
void nh_mesh_tri(NhMesh *m, uint16_t a, uint16_t b, uint16_t c);
/* Quad with CCW winding p0,p1,p2,p3 (seen from the front), subdivided su x sv
 * times so per-vertex lighting has enough samples. UVs given at corners. */
void nh_mesh_quad(NhMesh *m, NhVec3 p0, NhVec3 p1, NhVec3 p2, NhVec3 p3, NhVec3 n, float u0, float v0, float u1,
                  float v1, int su, int sv, NhColor c, uint8_t bone);
/* Axis-aligned box (outward faces), UVs in world metres * uv_scale. */
void nh_mesh_box(NhMesh *m, NhVec3 bmin, NhVec3 bmax, float uv_scale, NhColor c, uint8_t bone);
void nh_mesh_compute_bounds(NhMesh *m);

static inline NhVec3 nh_vert_pos(const NhVertex *v)
{
    return nh_v3(v->pos[0] / NH_POS_SCALE, v->pos[1] / NH_POS_SCALE, v->pos[2] / NH_POS_SCALE);
}
static inline NhVec3 nh_vert_nrm(const NhVertex *v)
{
    return nh_v3(v->nrm[0] / 64.0f, v->nrm[1] / 64.0f, v->nrm[2] / 64.0f);
}

#endif
