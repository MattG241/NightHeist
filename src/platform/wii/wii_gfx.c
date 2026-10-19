/* Night Heist - GX renderer.
 *
 * 3D: indexed float vertex arrays compiled into display lists at load, one
 * TEV stage (texture * lit vertex colour), hardware per-vertex lights.
 * 2D: batched immediate-mode quads drawn after the 3D pass. */
#include <gccore.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>

#include "core/nh_math.h"
#include "platform/gfx.h"
#include "wii_internal.h"

#define FIFO_SIZE (256 * 1024)
#define MAX_QUADS 4096
#define MAX_BEGIN_VERTS 65532 /* GX_Begin takes a u16 count; keep whole triangles */
#define WIDE_VI_WIDTH 678

/* GX treats clockwise screen-space winding as front-facing; our meshes are
 * counter-clockwise (OpenGL convention), so cull GX "front" faces. */
#define CULL_3D GX_CULL_FRONT

typedef struct WiiMesh {
    f32 *pos, *nrm, *uv;
    void *dl;
    u32 dl_size;
    u32 tris;
} WiiMesh;

typedef struct WiiTexture {
    GXTexObj obj;
    void *data;
} WiiTexture;

typedef struct Quad2D {
    GfxTexture tex;
    f32 x, y, w, h, u0, v0, u1, v1;
    GXColor c;
} Quad2D;

static GXRModeObj *s_rmode;
static void *s_xfb[2];
static int s_fb;
static void *s_fifo;
static bool s_wide;
static bool s_first_frame = true;

static WiiMesh s_meshes[GFX_MAX_MESHES];
static int s_mesh_count = 1; /* slot 0 = invalid */
static WiiTexture s_textures[GFX_MAX_TEXTURES];
static int s_texture_count = 1;

static Mtx s_view;
static u8 s_light_mask;
static GXColor s_ambient;
static bool s_3d_state_set;

static Quad2D s_quads[MAX_QUADS];
static int s_quad_count;

static GfxStats s_stats, s_cur;
static u64 s_frame_start;

static GXColor to_gx(NhColor c)
{
    GXColor g = { c.r, c.g, c.b, c.a };
    return g;
}

bool wii_gfx_init(void)
{
    VIDEO_Init();
    s_rmode = VIDEO_GetPreferredMode(NULL); /* picks 480p when set up and on component */

    s_wide = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    if (s_wide) {
        /* Standard trick for anamorphic 16:9: widen the VI output window. */
        s_rmode->viWidth = WIDE_VI_WIDTH;
        s_rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - WIDE_VI_WIDTH) / 2;
    }

    VIDEO_Configure(s_rmode);
    for (int i = 0; i < 2; i++) {
        s_xfb[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(s_rmode));
        VIDEO_ClearFrameBuffer(s_rmode, s_xfb[i], COLOR_BLACK);
    }
    VIDEO_SetNextFramebuffer(s_xfb[0]);
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (s_rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    s_fifo = memalign(32, FIFO_SIZE);
    if (!s_fifo)
        return false;
    memset(s_fifo, 0, FIFO_SIZE);
    GX_Init(s_fifo, FIFO_SIZE);

    GXColor black = { 0, 0, 0, 255 };
    GX_SetCopyClear(black, GX_MAX_Z24);
    GX_SetViewport(0, 0, s_rmode->fbWidth, s_rmode->efbHeight, 0, 1);
    f32 yscale = GX_GetYScaleFactor(s_rmode->efbHeight, s_rmode->xfbHeight);
    u32 xfb_height = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, s_rmode->fbWidth, s_rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, s_rmode->fbWidth, s_rmode->efbHeight);
    GX_SetDispCopyDst(s_rmode->fbWidth, xfb_height);
    GX_SetCopyFilter(s_rmode->aa, s_rmode->sample_pattern, GX_TRUE, s_rmode->vfilter);
    GX_SetFieldMode(s_rmode->field_rendering,
                    (s_rmode->viHeight == 2 * s_rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(s_rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetCullMode(CULL_3D);
    GX_CopyDisp(s_xfb[s_fb], GX_TRUE);
    GX_InvVtxCache();
    GX_InvalidateTexAll();

    s_frame_start = gettime();
    return true;
}

void wii_gfx_shutdown(void)
{
    GX_DrawDone();
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

float gfx_display_aspect(void)
{
    return s_wide ? 16.0f / 9.0f : 4.0f / 3.0f;
}

/* ---- resources ----------------------------------------------------------- */

static void setup_3d_vertex_format(void)
{
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_INDEX16);
    GX_SetVtxDesc(GX_VA_NRM, GX_INDEX16);
    GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX16);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
}

static void *copy_aligned(const void *src, u32 bytes)
{
    void *dst = memalign(32, bytes);
    if (dst) {
        memcpy(dst, src, bytes);
        DCFlushRange(dst, bytes);
    }
    return dst;
}

GfxMesh gfx_mesh_create(const NhMeshData *m)
{
    if (s_mesh_count >= GFX_MAX_MESHES || m->icount == 0)
        return 0;
    WiiMesh *wm = &s_meshes[s_mesh_count];
    wm->pos = copy_aligned(m->pos, m->vcount * 12);
    wm->nrm = copy_aligned(m->nrm, m->vcount * 12);
    wm->uv = copy_aligned(m->uv, m->vcount * 8);
    if (!wm->pos || !wm->nrm || !wm->uv)
        return 0;

    /* 3 bytes per GX_Begin header, 6 bytes per vertex (three u16 indices),
     * plus slack for padding and any state GX flushes into the list. */
    u32 begins = (m->icount + MAX_BEGIN_VERTS - 1) / MAX_BEGIN_VERTS;
    u32 cap = (u32)NH_ALIGN_UP(begins * 3 + m->icount * 6 + 256, 32);
    wm->dl = memalign(32, cap);
    if (!wm->dl)
        return 0;
    DCInvalidateRange(wm->dl, cap);

    setup_3d_vertex_format();
    GX_BeginDispList(wm->dl, cap);
    for (u32 start = 0; start < m->icount; start += MAX_BEGIN_VERTS) {
        u32 n = NH_MIN(m->icount - start, (u32)MAX_BEGIN_VERTS);
        GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)n);
        for (u32 i = start; i < start + n; i++) {
            u16 v = m->idx[i];
            GX_Position1x16(v);
            GX_Normal1x16(v);
            GX_TexCoord1x16(v);
        }
        GX_End();
    }
    wm->dl_size = GX_EndDispList();
    if (wm->dl_size == 0)
        return 0;
    DCFlushRange(wm->dl, wm->dl_size);
    wm->tris = m->icount / 3;
    return (GfxMesh)s_mesh_count++;
}

GfxTexture gfx_texture_create(const GfxTextureDesc *d)
{
    if (s_texture_count >= GFX_MAX_TEXTURES || d->levels < 1)
        return 0;
    WiiTexture *t = &s_textures[s_texture_count];
    u32 bytes = (u32)nh_tex_total_size(d->format, d->width, d->height, d->levels);
    t->data = copy_aligned(d->data, bytes);
    if (!t->data)
        return 0;

    u8 wrap = d->repeat ? GX_REPEAT : GX_CLAMP;
    bool mips = d->levels > 1;
    GX_InitTexObj(&t->obj, t->data, d->width, d->height, (u8)d->format, wrap, wrap, mips ? GX_TRUE : GX_FALSE);
    if (mips) {
        GX_InitTexObjLOD(&t->obj, d->filter ? GX_LIN_MIP_LIN : GX_NEAR_MIP_NEAR, d->filter ? GX_LINEAR : GX_NEAR,
                         0.0f, (f32)(d->levels - 1), 0.0f, GX_FALSE, GX_TRUE, GX_ANISO_1);
    } else {
        GX_InitTexObjFilterMode(&t->obj, d->filter ? GX_LINEAR : GX_NEAR, d->filter ? GX_LINEAR : GX_NEAR);
    }
    GX_InvalidateTexAll();
    return (GfxTexture)s_texture_count++;
}

/* ---- frame --------------------------------------------------------------- */

void gfx_begin_frame(NhColor clear)
{
    s_cur = (GfxStats){ 0 };
    s_3d_state_set = false;
    s_quad_count = 0;
    /* The EFB is cleared by the copy at the end of the frame, so this colour
     * applies from the next frame on. */
    GX_SetCopyClear(to_gx(clear), GX_MAX_Z24);
    GX_SetViewport(0, 0, s_rmode->fbWidth, s_rmode->efbHeight, 0, 1);
}

void gfx_set_camera(const NhMat34 *view, float fovy_deg, float znear, float zfar)
{
    Mtx44 proj;
    guPerspective(proj, fovy_deg, gfx_display_aspect(), znear, zfar);
    GX_LoadProjectionMtx(proj, GX_PERSPECTIVE);
    memcpy(s_view, view->m, sizeof(Mtx));
}

void gfx_set_lights(const NhLight *lights, int count, NhColor ambient)
{
    count = NH_MIN(count, NH_MAX_HW_LIGHTS);
    s_light_mask = 0;
    for (int i = 0; i < count; i++) {
        const NhLight *l = &lights[i];
        guVector p = { l->pos.x, l->pos.y, l->pos.z }, vp;
        guVector d = { l->dir.x, l->dir.y, l->dir.z }, vd;
        guVecMultiply(s_view, &p, &vp);   /* GX lights live in view space */
        guVecMultiplySR(s_view, &d, &vd);
        GXLightObj lo;
        GX_InitLightPos(&lo, vp.x, vp.y, vp.z);
        GX_InitLightDir(&lo, vd.x, vd.y, vd.z);
        GX_InitLightColor(&lo, to_gx(l->color));
        GX_InitLightAttn(&lo, l->a[0], l->a[1], l->a[2], l->k[0], l->k[1], l->k[2]);
        GX_LoadLightObj(&lo, (u8)(GX_LIGHT0 << i));
        s_light_mask |= (u8)(GX_LIGHT0 << i);
    }
    s_ambient = to_gx(ambient);
    s_3d_state_set = false;
}

static void set_3d_state(void)
{
    setup_3d_vertex_format();
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_ENABLE, GX_SRC_REG, GX_SRC_REG, s_light_mask, GX_DF_CLAMP, GX_AF_SPOT);
    GX_SetChanAmbColor(GX_COLOR0A0, s_ambient);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetCullMode(CULL_3D);
    GX_SetCurrentMtx(GX_PNMTX0);
    s_3d_state_set = true;
}

void gfx_draw_mesh(GfxMesh mesh, GfxTexture tex, const NhMat34 *model, NhColor material)
{
    if (mesh == 0 || mesh >= s_mesh_count || tex == 0 || tex >= s_texture_count)
        return;
    if (!s_3d_state_set)
        set_3d_state();

    NhMat34 view, mv, nm;
    memcpy(view.m, s_view, sizeof(Mtx));
    nh_m34_mul(&mv, &view, model);
    nh_m34_normal_matrix(&nm, &mv);
    GX_LoadPosMtxImm(mv.m, GX_PNMTX0);
    GX_LoadNrmMtxImm(nm.m, GX_PNMTX0);
    GX_SetChanMatColor(GX_COLOR0A0, to_gx(material));

    const WiiMesh *wm = &s_meshes[mesh];
    GX_LoadTexObj(&s_textures[tex].obj, GX_TEXMAP0);
    GX_SetArray(GX_VA_POS, wm->pos, 12);
    GX_SetArray(GX_VA_NRM, wm->nrm, 12);
    GX_SetArray(GX_VA_TEX0, wm->uv, 8);
    GX_CallDispList(wm->dl, wm->dl_size);

    s_cur.triangles += wm->tris;
    s_cur.draw_calls++;
}

void gfx_draw_quad2d(GfxTexture tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                     NhColor color)
{
    if (s_quad_count >= MAX_QUADS || tex == 0 || tex >= s_texture_count)
        return;
    Quad2D *q = &s_quads[s_quad_count++];
    *q = (Quad2D){ tex, x, y, w, h, u0, v0, u1, v1, to_gx(color) };
}

static void flush_2d(void)
{
    if (s_quad_count == 0)
        return;

    Mtx44 ortho;
    guOrtho(ortho, 0, GFX_SCREEN_H, 0, GFX_SCREEN_W, 0, 1000.0f);
    GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
    Mtx ident;
    guMtxIdentity(ident);
    GX_LoadPosMtxImm(ident, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetCullMode(GX_CULL_NONE);

    int i = 0;
    while (i < s_quad_count) {
        GfxTexture tex = s_quads[i].tex;
        int end = i;
        while (end < s_quad_count && s_quads[end].tex == tex && end - i < 16383)
            end++;
        GX_LoadTexObj(&s_textures[tex].obj, GX_TEXMAP0);
        GX_Begin(GX_QUADS, GX_VTXFMT1, (u16)((end - i) * 4));
        for (int k = i; k < end; k++) {
            const Quad2D *q = &s_quads[k];
            const f32 z = -1.0f;
            GX_Position3f32(q->x, q->y, z);
            GX_Color4u8(q->c.r, q->c.g, q->c.b, q->c.a);
            GX_TexCoord2f32(q->u0, q->v0);
            GX_Position3f32(q->x + q->w, q->y, z);
            GX_Color4u8(q->c.r, q->c.g, q->c.b, q->c.a);
            GX_TexCoord2f32(q->u1, q->v0);
            GX_Position3f32(q->x + q->w, q->y + q->h, z);
            GX_Color4u8(q->c.r, q->c.g, q->c.b, q->c.a);
            GX_TexCoord2f32(q->u1, q->v1);
            GX_Position3f32(q->x, q->y + q->h, z);
            GX_Color4u8(q->c.r, q->c.g, q->c.b, q->c.a);
            GX_TexCoord2f32(q->u0, q->v1);
        }
        GX_End();
        s_cur.draw_calls++;
        s_cur.triangles += (u32)(end - i) * 2;
        i = end;
    }
    s_quad_count = 0;
}

void gfx_end_frame(void)
{
    flush_2d();

    u64 t0 = gettime();
    s_cur.cpu_us = (u32)ticks_to_microsecs(t0 - s_frame_start);

    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(s_xfb[s_fb], GX_TRUE);
    GX_DrawDone(); /* Phase 0: CPU waits for the GPU; overlap comes later */
    s_cur.gpu_wait_us = (u32)ticks_to_microsecs(gettime() - t0);

    VIDEO_SetNextFramebuffer(s_xfb[s_fb]);
    if (s_first_frame) {
        VIDEO_SetBlack(FALSE);
        s_first_frame = false;
    }
    VIDEO_Flush();
    VIDEO_WaitVSync();
    s_fb ^= 1;

    s_stats = s_cur;
    s_frame_start = gettime();
}

GfxStats gfx_last_stats(void)
{
    return s_stats;
}
