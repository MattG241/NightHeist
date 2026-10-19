/* Night Heist - renderer interface.
 *
 * Shaped around what GX can do so the desktop backend can't drift into
 * features the Wii lacks:
 *  - per-vertex lighting only (nh_light.h model), 8 hardware lights:
 *      slots 0-5  dynamic lights in colour channel 0 (plus baked vertex colour
 *                 as the per-vertex ambient)
 *      slot 6     player torch, colour channel 1 (RGB)
 *      slot 7     one guard torch, alpha channel 1
 *  - projected torch cookies via texture-coordinate generation from position
 *    with a light-space matrix, combined in TEV:
 *      out = albedo * tint * ( cookieA * C1 + cookieB * A1 * guardColour + clamp(amb + lights) )
 *  - textures only in GX formats (nh_texconv.h)
 *  - meshes are compact NhVertex arrays compiled once (display lists on Wii)
 *  - rigid skinning through up to NH_MAX_BONES position matrices per draw
 *  - a 640x480 framebuffer; the desktop window just scales it up */
#ifndef NH_GFX_H
#define NH_GFX_H

#include "core/nh_light.h"
#include "core/nh_mesh.h"
#include "core/nh_texconv.h"

#define GFX_SCREEN_W 640
#define GFX_SCREEN_H 480
#define GFX_MAX_MESHES 1024
#define GFX_MAX_TEXTURES 256
#define GFX_MAX_DYN_LIGHTS 6

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
    uint32_t mesh_bytes;  /* GPU-side geometry memory */
    uint32_t tex_bytes;   /* GPU-side texture memory */
} GfxStats;

/* Material flags */
enum {
    GFX_MAT_UNLIT = 1u << 0,    /* albedo * tint only (signs, screens, sky) */
    GFX_MAT_BAKED = 1u << 1,    /* vertex colours are the baked ambient; else `ambient` */
    GFX_MAT_BLEND = 1u << 2,    /* alpha blend, no depth write */
    GFX_MAT_ADD = 1u << 3,      /* additive, no depth write */
    GFX_MAT_NOCULL = 1u << 4,
    GFX_MAT_NOTORCH = 1u << 5,  /* not lit by projectors */
    GFX_MAT_ALPHATEST = 1u << 6,
    GFX_MAT_WARM = 1u << 7,     /* shows hot in thermal mode (people, dogs) */
    GFX_MAT_NOFOG = 1u << 8,
};

typedef struct GfxMaterial {
    GfxTexture tex;
    NhColor tint;
    NhColor ambient; /* used when not GFX_MAT_BAKED */
    uint16_t flags;
} GfxMaterial;

typedef struct GfxProjector {
    bool enabled;
    NhVec3 pos, dir;
    float fov_deg;       /* full cone angle of the cookie */
    NhLight light;       /* per-vertex term: colour, spot and distance attenuation */
    GfxTexture cookie;   /* I8 cookie, black at the edges */
} GfxProjector;

typedef struct GfxFog {
    bool enabled;
    float start, end; /* view-space distance */
    NhColor color;
} GfxFog;

typedef enum GfxMode { GFX_MODE_NORMAL, GFX_MODE_THERMAL } GfxMode;

GfxTexture gfx_texture_create(const GfxTextureDesc *desc);
void gfx_texture_destroy(GfxTexture t);
/* The mesh data is copied; the caller can reset its arena afterwards.
 * Pass dynamic_colors to allow gfx_mesh_set_colors (baked light changes). */
GfxMesh gfx_mesh_create(const NhMesh *mesh, bool dynamic_colors);
void gfx_mesh_set_colors(GfxMesh m, const NhColor *colors);
void gfx_mesh_destroy(GfxMesh m);

/* Aspect ratio of the display: 4:3, or 16:9 when the console is set to
 * widescreen (the 640x480 framebuffer is then stretched anamorphically). */
float gfx_display_aspect(void);

void gfx_begin_frame(NhColor clear);

/* 3D pass */
void gfx_set_camera(const NhMat34 *view, float fovy_deg, float znear, float zfar);
void gfx_set_fog(const GfxFog *fog);
void gfx_set_lights(const NhLight *lights, int count); /* <= GFX_MAX_DYN_LIGHTS */
void gfx_set_projectors(const GfxProjector *torch, const GfxProjector *guard);
void gfx_set_mode(GfxMode mode);
void gfx_draw(GfxMesh mesh, const GfxMaterial *mat, const NhMat34 *model);
/* bones[i] maps bind-pose model space to world for vertices with bone == i. */
void gfx_draw_skinned(GfxMesh mesh, const GfxMaterial *mat, const NhMat34 *bones, int nbones);
/* Batched textured quads in world space (sprites, decals, glows, particles),
 * drawn after the opaque pass in submission order. Corners CCW. */
void gfx_draw_quad3d(GfxTexture tex, const NhVec3 corners[4], float u0, float v0, float u1, float v1, NhColor color,
                     uint16_t flags);

/* 2D pass (640x480 pixel coordinates, origin top-left, alpha blended).
 * Quads are batched and drawn at gfx_end_frame, after all 3D. */
void gfx_draw_quad2d(GfxTexture tex, float x, float y, float w, float h, float u0, float v0, float u1,
                     float v1, NhColor color);

/* Flush, present and wait for vertical sync. */
void gfx_end_frame(void);
GfxStats gfx_last_stats(void);

#endif
