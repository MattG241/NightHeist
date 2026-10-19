/* Night Heist - renderer interface.
 *
 * Deliberately shaped around what GX can do so the desktop backend can't
 * drift into features the Wii lacks:
 *  - per-vertex lighting only, max 8 hardware lights (nh_light.h model)
 *  - texture * lit vertex colour (one TEV "modulate" stage for now)
 *  - textures only in GX formats (nh_texconv.h), power-of-two, <= 1024
 *  - 16-bit indices, static meshes compiled once (display lists on Wii)
 *  - a 640x480 framebuffer; the desktop window just scales it up */
#ifndef NH_GFX_H
#define NH_GFX_H

#include "core/nh_light.h"
#include "core/nh_meshgen.h"
#include "core/nh_texconv.h"

#define GFX_SCREEN_W 640
#define GFX_SCREEN_H 480
#define GFX_MAX_MESHES 256
#define GFX_MAX_TEXTURES 256

typedef uint16_t GfxMesh;    /* 0 = invalid */
typedef uint16_t GfxTexture; /* 0 = invalid */

typedef struct GfxTextureDesc {
    NhTexFormat format;
    uint16_t width, height; /* power of two */
    uint8_t levels;         /* mip levels in data, >= 1 */
    bool repeat;            /* wrap (true) or clamp */
    bool filter;            /* bilinear+trilinear (true) or nearest */
    const uint8_t *data;    /* GX tiled data, all levels; copied by the backend */
} GfxTextureDesc;

typedef struct GfxStats {
    uint32_t triangles;
    uint32_t draw_calls;
    uint32_t cpu_us;      /* CPU time of the last frame before present */
    uint32_t gpu_wait_us; /* time blocked waiting for the GPU to finish */
} GfxStats;

GfxTexture gfx_texture_create(const GfxTextureDesc *desc);
/* The mesh data is copied; the caller can reset its arena afterwards. */
GfxMesh gfx_mesh_create(const NhMeshData *mesh);

/* Aspect ratio of the display: 4:3, or 16:9 when the console is set to
 * widescreen (the 640x480 framebuffer is then stretched anamorphically). */
float gfx_display_aspect(void);

void gfx_begin_frame(NhColor clear);

/* 3D pass */
void gfx_set_camera(const NhMat34 *view, float fovy_deg, float znear, float zfar);
void gfx_set_lights(const NhLight *lights, int count, NhColor ambient);
void gfx_draw_mesh(GfxMesh mesh, GfxTexture tex, const NhMat34 *model, NhColor material);

/* 2D pass (640x480 pixel coordinates, origin top-left, alpha blended).
 * Quads are batched and drawn at gfx_end_frame, after all 3D. */
void gfx_draw_quad2d(GfxTexture tex, float x, float y, float w, float h, float u0, float v0, float u1,
                     float v1, NhColor color);

/* Flush, present and wait for vertical sync. */
void gfx_end_frame(void);
GfxStats gfx_last_stats(void);

#endif
