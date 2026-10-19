#include "nh_meshgen.h"

#include <math.h>

static bool alloc_mesh(NhMeshData *m, NhArena *arena, uint32_t vcount, uint32_t icount)
{
    if (vcount > 65535u)
        return false;
    m->pos = nh_arena_alloc(arena, sizeof(NhVec3) * vcount);
    m->nrm = nh_arena_alloc(arena, sizeof(NhVec3) * vcount);
    m->uv = nh_arena_alloc(arena, sizeof(NhVec2) * vcount);
    m->idx = nh_arena_alloc(arena, sizeof(uint16_t) * icount);
    m->vcount = vcount;
    m->icount = icount;
    return m->pos && m->nrm && m->uv && m->idx;
}

/* Emit two CCW triangles (front faces) for every cell of a (cols x rows)
 * vertex lattice. Vertex (c, r) is at index r * cols + c. */
static void lattice_indices(uint16_t *idx, int cols, int rows)
{
    for (int r = 0; r + 1 < rows; r++) {
        for (int c = 0; c + 1 < cols; c++) {
            uint16_t i0 = (uint16_t)(r * cols + c);
            uint16_t i1 = (uint16_t)(i0 + 1);
            uint16_t i2 = (uint16_t)(i0 + cols);
            uint16_t i3 = (uint16_t)(i2 + 1);
            *idx++ = i0;
            *idx++ = i2;
            *idx++ = i1;
            *idx++ = i1;
            *idx++ = i2;
            *idx++ = i3;
        }
    }
}

bool nh_mesh_torus(NhMeshData *out, NhArena *arena, float major_r, float minor_r, int rings, int sides,
                   float uv_scale_u, float uv_scale_v)
{
    int cols = rings + 1, rows = sides + 1; /* duplicate seam vertices for clean UVs */
    if (!alloc_mesh(out, arena, (uint32_t)(cols * rows), (uint32_t)(rings * sides * 6)))
        return false;

    for (int r = 0; r < rows; r++) {
        float v = (float)r / (float)sides;
        float phi = v * 2.0f * NH_PI;
        for (int c = 0; c < cols; c++) {
            float u = (float)c / (float)rings;
            float theta = u * 2.0f * NH_PI;
            NhVec3 ring_dir = nh_v3(cosf(theta), 0.0f, sinf(theta));
            NhVec3 n = nh_v3_add(nh_v3_scale(ring_dir, cosf(phi)), nh_v3(0.0f, sinf(phi), 0.0f));
            int i = r * cols + c;
            out->pos[i] = nh_v3_add(nh_v3_scale(ring_dir, major_r), nh_v3_scale(n, minor_r));
            out->nrm[i] = n;
            out->uv[i].x = u * uv_scale_u;
            out->uv[i].y = v * uv_scale_v;
        }
    }
    lattice_indices(out->idx, cols, rows);
    return true;
}

bool nh_mesh_grid(NhMeshData *out, NhArena *arena, float size_x, float size_z, int cells_x, int cells_z,
                  float uv_per_unit)
{
    int cols = cells_x + 1, rows = cells_z + 1;
    if (!alloc_mesh(out, arena, (uint32_t)(cols * rows), (uint32_t)(cells_x * cells_z * 6)))
        return false;

    for (int r = 0; r < rows; r++) {
        /* Rows run from far (-Z) to near (+Z) so lattice winding faces +Y. */
        float z = -size_z * 0.5f + size_z * (float)r / (float)cells_z;
        for (int c = 0; c < cols; c++) {
            float x = -size_x * 0.5f + size_x * (float)c / (float)cells_x;
            int i = r * cols + c;
            out->pos[i] = nh_v3(x, 0.0f, z);
            out->nrm[i] = nh_v3(0.0f, 1.0f, 0.0f);
            out->uv[i].x = x * uv_per_unit;
            out->uv[i].y = z * uv_per_unit;
        }
    }
    lattice_indices(out->idx, cols, rows);
    return true;
}
