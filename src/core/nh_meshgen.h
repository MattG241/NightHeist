/* Night Heist - procedural meshes (Phase 0 test geometry; real art comes
 * through the asset pipeline from Phase 1 on). */
#ifndef NH_MESHGEN_H
#define NH_MESHGEN_H

#include "nh_arena.h"
#include "nh_math.h"

/* Indexed triangle list, the common in-memory mesh form fed to gfx. */
typedef struct NhMeshData {
    NhVec3 *pos;
    NhVec3 *nrm;
    NhVec2 *uv;
    uint16_t *idx;
    uint32_t vcount;
    uint32_t icount;
} NhMeshData;

/* Torus around the Y axis. Returns false if the arena is full or the vertex
 * count would exceed 16-bit indices. */
bool nh_mesh_torus(NhMeshData *out, NhArena *arena, float major_r, float minor_r, int rings, int sides,
                   float uv_scale_u, float uv_scale_v);
/* Flat XZ grid facing +Y, centred on the origin. Tessellated so per-vertex
 * lighting has enough vertices to show a spotlight. */
bool nh_mesh_grid(NhMeshData *out, NhArena *arena, float size_x, float size_z, int cells_x, int cells_z,
                  float uv_per_unit);

#endif
