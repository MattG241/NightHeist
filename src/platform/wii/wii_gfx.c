/* Night Heist - GX renderer.
 *
 * Geometry: NhVertex data in separate s16/s8 arrays (positions, normals, UVs)
 * plus an RGBA8 colour array for baked light, referenced by 16-bit indices
 * from a display list compiled once per mesh. Rigid skinning sends a matrix
 * index per vertex (GX_VA_PTNMTXIDX) selecting one of ten loaded matrices.
 *
 * Lighting and TEV (lit materials):
 *   COLOR0 = tint * clamp(amb + lights 0..5)   amb = baked vertex colour or register
 *   COLOR1 = tint * clamp(torch light 6)       ALPHA1 = clamp(guard torch light 7)
 *   stage 0: prev = cookie(texcoord1) * COLOR1
 *   stage 1: reg0 = cookie(texcoord2) * ALPHA1          (only with a guard torch)
 *   stage 2: prev = prev + reg0 * K0 (guard colour)     (only with a guard torch)
 *   stage 3: prev = prev + COLOR0
 *   stage 4: out  = prev * albedo;  alpha = albedo.a * tint.a
 * Cookie coordinates come from GX_TG_POS through a light-space projection
 * matrix (texture matrices 0 and 1), divided per pixel by the hardware. */
#include <gccore.h>
#include <malloc.h>
#include <math.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>

#include "core/nh_math.h"
#include "platform/gfx.h"
#include "wii_internal.h"

#define FIFO_SIZE (1024 * 1024)
#define NUM_XFB 3
#define MAX_Q2D 4096
#define MAX_Q3D 3072
#define MAX_BEGIN_VERTS 65532 /* GX_Begin takes a u16 count; keep whole triangles */
#define WIDE_VI_WIDTH 678

/* GX treats clockwise screen-space winding as front-facing; our meshes are
 * counter-clockwise (OpenGL convention), so cull GX "front" faces. */
#define CULL_3D GX_CULL_FRONT

typedef struct WiiMesh {
    s16 *pos;
    s8 *nrm;
    s16 *uv;
    GXColor *col;
    void *dl;
    u32 dl_size, vcount, tris, bytes;
    bool skinned, used;
} WiiMesh;

typedef struct WiiTexture {
    GXTexObj obj;
    void *data;
    u32 bytes;
    bool used;
} WiiTexture;

typedef struct Quad {
    GfxTexture tex;
    u16 flags;
    f32 p[4][3];
    f32 uv[4][2];
    GXColor c;
} Quad;

static GXRModeObj *s_rmode;
static void *s_xfb[NUM_XFB];
static bool s_pending; /* a finished frame is waiting for the GPU */
static int s_pending_fb;
static int s_fb;
static void *s_fifo;
static bool s_wide;
static bool s_first_frame = true;

static WiiMesh s_meshes[GFX_MAX_MESHES];
static WiiTexture s_textures[GFX_MAX_TEXTURES];
static GXTexObj s_white_obj;
static u8 s_white_data[32] ATTRIBUTE_ALIGN(32);

static Mtx s_view;
static Mtx44 s_proj;
static u8 s_light_mask;
static GfxProjector s_projA, s_projB;
static GfxFog s_fog;
static float s_znear = 0.1f, s_zfar = 100.0f;
static GfxMode s_mode;
static bool s_colors_dirty;

static Quad s_q2d[MAX_Q2D];
static int s_nq2d;
static Quad s_q3d[MAX_Q3D];
static int s_nq3d;

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
    for (int i = 0; i < NUM_XFB; i++) {
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

    /* 1x1 white (I8 block) for untextured materials. */
    memset(s_white_data, 255, sizeof(s_white_data));
    DCFlushRange(s_white_data, sizeof(s_white_data));
    GX_InitTexObj(&s_white_obj, s_white_data, 8, 4, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);

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

static void set_vtx_format(bool colors, bool skinned)
{
    GX_ClearVtxDesc();
    if (skinned)
        GX_SetVtxDesc(GX_VA_PTNMTXIDX, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_POS, GX_INDEX16);
    GX_SetVtxDesc(GX_VA_NRM, GX_INDEX16);
    if (colors)
        GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX16);
    GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX16);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, NH_POS_FRAC);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, NH_UV_FRAC);
}

static void *alloc_copy(const void *src, u32 bytes)
{
    void *dst = memalign(32, NH_ALIGN_UP(bytes, 32));
    if (dst && src) {
        memcpy(dst, src, bytes);
        DCFlushRange(dst, NH_ALIGN_UP(bytes, 32));
    }
    return dst;
}

GfxMesh gfx_mesh_create(const NhMesh *m, bool dynamic_colors)
{
    (void)dynamic_colors;
    if (m->icount == 0 || m->vcount == 0)
        return 0;
    int slot = 0;
    for (int i = 1; i < GFX_MAX_MESHES; i++)
        if (!s_meshes[i].used) {
            slot = i;
            break;
        }
    if (!slot)
        return 0;
    WiiMesh *wm = &s_meshes[slot];
    memset(wm, 0, sizeof(*wm));
    u32 n = m->vcount;
    /* De-interleave into GX attribute arrays. */
    s16 *pos = memalign(32, NH_ALIGN_UP(n * 6, 32));
    s8 *nrm = memalign(32, NH_ALIGN_UP(n * 3, 32));
    s16 *uv = memalign(32, NH_ALIGN_UP(n * 4, 32));
    if (!pos || !nrm || !uv)
        return 0;
    for (u32 i = 0; i < n; i++) {
        const NhVertex *v = &m->verts[i];
        pos[i * 3] = v->pos[0];
        pos[i * 3 + 1] = v->pos[1];
        pos[i * 3 + 2] = v->pos[2];
        nrm[i * 3] = v->nrm[0];
        nrm[i * 3 + 1] = v->nrm[1];
        nrm[i * 3 + 2] = v->nrm[2];
        uv[i * 2] = v->uv[0];
        uv[i * 2 + 1] = v->uv[1];
    }
    DCFlushRange(pos, NH_ALIGN_UP(n * 6, 32));
    DCFlushRange(nrm, NH_ALIGN_UP(n * 3, 32));
    DCFlushRange(uv, NH_ALIGN_UP(n * 4, 32));
    wm->pos = pos;
    wm->nrm = nrm;
    wm->uv = uv;
    wm->col = m->colors ? alloc_copy(m->colors, n * 4) : NULL;

    bool colors = wm->col != NULL;
    u32 per_vert = 2 + 2 + 2 + (colors ? 2 : 0) + (m->skinned ? 1 : 0);
    u32 begins = (m->icount + MAX_BEGIN_VERTS - 1) / MAX_BEGIN_VERTS;
    u32 cap = (u32)NH_ALIGN_UP(begins * 3 + m->icount * per_vert + 256, 32);
    wm->dl = memalign(32, cap);
    if (!wm->dl)
        return 0;
    DCInvalidateRange(wm->dl, cap);
    set_vtx_format(colors, m->skinned);
    GX_BeginDispList(wm->dl, cap);
    for (u32 start = 0; start < m->icount; start += MAX_BEGIN_VERTS) {
        u32 cnt = NH_MIN(m->icount - start, (u32)MAX_BEGIN_VERTS);
        GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)cnt);
        for (u32 i = start; i < start + cnt; i++) {
            u16 v = m->idx[i];
            if (m->skinned)
                GX_MatrixIndex1x8((u8)(m->verts[v].bone * 3));
            GX_Position1x16(v);
            GX_Normal1x16(v);
            if (colors)
                GX_Color1x16(v);
            GX_TexCoord1x16(v);
        }
        GX_End();
    }
    wm->dl_size = GX_EndDispList();
    if (wm->dl_size == 0)
        return 0;
    DCFlushRange(wm->dl, wm->dl_size);
    wm->vcount = n;
    wm->tris = m->icount / 3;
    wm->skinned = m->skinned;
    wm->bytes = n * (6 + 3 + 4 + (colors ? 4 : 0)) + wm->dl_size;
    wm->used = true;
    return (GfxMesh)slot;
}

void gfx_mesh_set_colors(GfxMesh h, const NhColor *colors)
{
    if (!h || h >= GFX_MAX_MESHES || !s_meshes[h].used || !s_meshes[h].col)
        return;
    WiiMesh *wm = &s_meshes[h];
    memcpy(wm->col, colors, wm->vcount * 4);
    DCFlushRange(wm->col, NH_ALIGN_UP(wm->vcount * 4, 32));
    s_colors_dirty = true;
}

void gfx_mesh_destroy(GfxMesh h)
{
    if (!h || h >= GFX_MAX_MESHES || !s_meshes[h].used)
        return;
    GX_DrawDone(); /* the GPU may still be reading it */
    WiiMesh *wm = &s_meshes[h];
    free(wm->pos);
    free(wm->nrm);
    free(wm->uv);
    free(wm->col);
    free(wm->dl);
    memset(wm, 0, sizeof(*wm));
}

GfxTexture gfx_texture_create(const GfxTextureDesc *d)
{
    if (d->levels < 1)
        return 0;
    int slot = 0;
    for (int i = 1; i < GFX_MAX_TEXTURES; i++)
        if (!s_textures[i].used) {
            slot = i;
            break;
        }
    if (!slot)
        return 0;
    WiiTexture *t = &s_textures[slot];
    u32 bytes = (u32)nh_tex_total_size(d->format, d->width, d->height, d->levels);
    t->data = alloc_copy(d->data, bytes);
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
    t->bytes = bytes;
    t->used = true;
    GX_InvalidateTexAll();
    return (GfxTexture)slot;
}

void gfx_texture_destroy(GfxTexture h)
{
    if (!h || h >= GFX_MAX_TEXTURES || !s_textures[h].used)
        return;
    GX_DrawDone();
    free(s_textures[h].data);
    memset(&s_textures[h], 0, sizeof(s_textures[h]));
    GX_InvalidateTexAll();
}

static GXTexObj *tex_obj(GfxTexture h)
{
    if (h && h < GFX_MAX_TEXTURES && s_textures[h].used)
        return &s_textures[h].obj;
    return &s_white_obj;
}

/* ---- frame --------------------------------------------------------------- */

void gfx_begin_frame(NhColor clear)
{
    s_cur = (GfxStats){ 0 };
    s_nq2d = 0;
    s_nq3d = 0;
    s_projA.enabled = s_projB.enabled = false;
    s_fog.enabled = false;
    s_mode = GFX_MODE_NORMAL;
    s_light_mask = 0;
    /* The EFB is cleared by the copy at the end of the frame, so this colour
     * applies from the next frame on. */
    GX_SetCopyClear(to_gx(clear), GX_MAX_Z24);
    GX_SetViewport(0, 0, s_rmode->fbWidth, s_rmode->efbHeight, 0, 1);
    if (s_colors_dirty) {
        GX_InvVtxCache();
        s_colors_dirty = false;
    }
}

void gfx_set_camera(const NhMat34 *view, float fovy_deg, float znear, float zfar)
{
    guPerspective(s_proj, fovy_deg, gfx_display_aspect(), znear, zfar);
    GX_LoadProjectionMtx(s_proj, GX_PERSPECTIVE);
    memcpy(s_view, view->m, sizeof(Mtx));
    s_znear = znear;
    s_zfar = zfar;
}

void gfx_set_fog(const GfxFog *fog)
{
    if (fog)
        s_fog = *fog;
    else
        s_fog.enabled = false;
}

static void load_light(const NhLight *l, u8 slot)
{
    guVector p = { l->pos.x, l->pos.y, l->pos.z }, vp;
    guVector d = { l->dir.x, l->dir.y, l->dir.z }, vd;
    guVecMultiply(s_view, &p, &vp); /* GX lights live in view space */
    guVecMultiplySR(s_view, &d, &vd);
    GXLightObj lo;
    GX_InitLightPos(&lo, vp.x, vp.y, vp.z);
    GX_InitLightDir(&lo, vd.x, vd.y, vd.z);
    GXColor c = { l->color.r, l->color.g, l->color.b, 255 };
    GX_InitLightColor(&lo, c);
    GX_InitLightAttn(&lo, l->a[0], l->a[1], l->a[2], l->k[0], l->k[1], l->k[2]);
    GX_LoadLightObj(&lo, slot);
}

void gfx_set_lights(const NhLight *lights, int count)
{
    count = NH_MIN(count, GFX_MAX_DYN_LIGHTS);
    s_light_mask = 0;
    for (int i = 0; i < count; i++) {
        load_light(&lights[i], (u8)(GX_LIGHT0 << i));
        s_light_mask |= (u8)(GX_LIGHT0 << i);
    }
}

void gfx_set_projectors(const GfxProjector *torch, const GfxProjector *guard)
{
    s_projA.enabled = false;
    s_projB.enabled = false;
    if (torch && torch->enabled) {
        s_projA = *torch;
        load_light(&s_projA.light, GX_LIGHT6);
    }
    if (guard && guard->enabled) {
        s_projB = *guard;
        load_light(&s_projB.light, GX_LIGHT7);
    }
}

void gfx_set_mode(GfxMode mode)
{
    s_mode = mode;
}

/* Light-space projection for a cookie: model position -> (s*q, t*q, q). */
static void projector_texmtx(const GfxProjector *pr, const NhMat34 *model, Mtx out)
{
    NhMat34 lv, lm, t;
    NhVec3 up = fabsf(pr->dir.y) > 0.95f ? nh_v3(1, 0, 0) : nh_v3(0, 1, 0);
    nh_m34_look_at(&lv, pr->pos, up, nh_v3_add(pr->pos, pr->dir));
    nh_m34_mul(&lm, &lv, model);
    float f = 1.0f / tanf(NH_DEG2RAD(pr->fov_deg) * 0.5f);
    NhMat34 pc = { { { 0.5f * f, 0, -0.5f, 0 }, { 0, -0.5f * f, -0.5f, 0 }, { 0, 0, -1.0f, 0 } } };
    nh_m34_mul(&t, &pc, &lm);
    memcpy(out, t.m, sizeof(Mtx));
}

static void tev_reset_stage(u8 st)
{
    GX_SetTevKColorSel(st, GX_TEV_KCSEL_1);
    GX_SetTevKAlphaSel(st, GX_TEV_KASEL_1);
    GX_SetTevSwapMode(st, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GX_SetTevColorOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
}

static void apply_fog(bool on)
{
    if (on && s_fog.enabled) {
        GXColor c = to_gx(s_fog.color);
        GX_SetFog(GX_FOG_PERSP_LIN, s_fog.start, s_fog.end, s_znear, s_zfar, c);
    } else {
        GXColor c = { 0, 0, 0, 0 };
        GX_SetFog(GX_FOG_NONE, 0, 1, 0.1f, 1, c);
    }
}

static void apply_blend(u16 flags)
{
    if (flags & GFX_MAT_ADD) {
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
        GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    } else if (flags & GFX_MAT_BLEND) {
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    } else {
        GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
        GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    }
    if (flags & GFX_MAT_ALPHATEST) {
        GX_SetAlphaCompare(GX_GREATER, 127, GX_AOP_AND, GX_ALWAYS, 0);
        GX_SetZCompLoc(GX_FALSE);
    } else {
        GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GX_SetZCompLoc(GX_TRUE);
    }
    GX_SetCullMode((flags & GFX_MAT_NOCULL) ? GX_CULL_NONE : CULL_3D);
}

static void setup_material(const WiiMesh *wm, const GfxMaterial *mat, const NhMat34 *model)
{
    bool lit = !(mat->flags & GFX_MAT_UNLIT);
    bool baked = (mat->flags & GFX_MAT_BAKED) && wm->col;
    bool torch = lit && s_projA.enabled && !(mat->flags & GFX_MAT_NOTORCH);
    bool guard = lit && s_projB.enabled && !(mat->flags & GFX_MAT_NOTORCH);
    bool cookies = !wm->skinned;
    GXColor tint = to_gx(mat->tint);
    GXColor amb = to_gx(mat->ambient);
    GXColor zero = { 0, 0, 0, 0 };

    set_vtx_format(wm->col != NULL, wm->skinned);
    GX_SetArray(GX_VA_POS, wm->pos, 6);
    GX_SetArray(GX_VA_NRM, wm->nrm, 3);
    if (wm->col)
        GX_SetArray(GX_VA_CLR0, wm->col, 4);
    GX_SetArray(GX_VA_TEX0, wm->uv, 4);
    GX_LoadTexObj(tex_obj(mat->tex), GX_TEXMAP0);
    apply_blend(mat->flags);
    apply_fog(!(mat->flags & GFX_MAT_NOFOG));

    if (!lit) {
        GX_SetNumChans(1);
        GX_SetChanCtrl(GX_COLOR0, GX_DISABLE, GX_SRC_REG, baked ? GX_SRC_VTX : GX_SRC_REG, 0, GX_DF_NONE,
                       GX_AF_NONE);
        GX_SetChanCtrl(GX_ALPHA0, GX_DISABLE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
        GX_SetChanMatColor(GX_COLOR0A0, tint);
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetNumTevStages(1);
        tev_reset_stage(GX_TEVSTAGE0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        return;
    }

    GX_SetNumChans(2);
    GX_SetChanCtrl(GX_COLOR0, GX_ENABLE, baked ? GX_SRC_VTX : GX_SRC_REG, GX_SRC_REG, s_light_mask, GX_DF_CLAMP,
                   GX_AF_SPOT);
    GX_SetChanCtrl(GX_ALPHA0, GX_DISABLE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    GX_SetChanAmbColor(GX_COLOR0A0, amb);
    GX_SetChanMatColor(GX_COLOR0A0, tint);
    GX_SetChanCtrl(GX_COLOR1, torch ? GX_ENABLE : GX_DISABLE, GX_SRC_REG, GX_SRC_REG, torch ? GX_LIGHT6 : 0,
                   GX_DF_CLAMP, GX_AF_SPOT);
    GX_SetChanCtrl(GX_ALPHA1, guard ? GX_ENABLE : GX_DISABLE, GX_SRC_REG, GX_SRC_REG, guard ? GX_LIGHT7 : 0,
                   GX_DF_CLAMP, GX_AF_SPOT);
    GX_SetChanAmbColor(GX_COLOR1A1, zero);
    /* Channel 1 material: torch tint in RGB; alpha 255 lets the guard light
     * through ALPHA1, alpha 0 silences it. */
    GXColor mat1 = { torch ? tint.r : 0, torch ? tint.g : 0, torch ? tint.b : 0, guard ? 255 : 0 };
    GX_SetChanMatColor(GX_COLOR1A1, mat1);

    /* Texture coordinates: 0 = albedo, 1/2 = cookies projected from position. */
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    int ntex = 1;
    bool cookieA = torch && cookies && s_projA.cookie;
    bool cookieB = guard && cookies && s_projB.cookie;
    if (cookieA) {
        Mtx m;
        projector_texmtx(&s_projA, model, m);
        GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX3x4);
        GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX3x4, GX_TG_POS, GX_TEXMTX0);
        ntex = 2;
    }
    if (cookieB) {
        Mtx m;
        projector_texmtx(&s_projB, model, m);
        GX_LoadTexMtxImm(m, GX_TEXMTX1, GX_MTX3x4);
        GX_SetTexCoordGen(GX_TEXCOORD2, GX_TG_MTX3x4, GX_TG_POS, GX_TEXMTX1);
        ntex = 3;
    }
    GX_SetNumTexGens(ntex);
    if (cookieA || cookieB)
        GX_LoadTexObj(tex_obj(cookieA ? s_projA.cookie : s_projB.cookie), GX_TEXMAP1);

    u8 st = GX_TEVSTAGE0;
    /* stage 0: prev = cookieA * C1 (or C1 alone without a cookie) */
    tev_reset_stage(st);
    if (cookieA) {
        GX_SetTevOrder(st, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR1A1);
        GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
    } else {
        GX_SetTevOrder(st, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR1A1);
        GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
    }
    st++;
    if (guard) {
        /* stage 1: reg0 = cookieB * A1 */
        tev_reset_stage(st);
        if (cookieB) {
            GX_SetTevOrder(st, GX_TEXCOORD2, GX_TEXMAP1, GX_COLOR1A1);
            GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASA, GX_CC_ZERO);
        } else {
            GX_SetTevOrder(st, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR1A1);
            GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASA);
        }
        GX_SetTevColorOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0);
        st++;
        /* stage 2: prev += reg0 * K0 */
        tev_reset_stage(st);
        GXColor gc = to_gx(s_projB.light.color);
        GX_SetTevKColor(GX_KCOLOR0, gc);
        GX_SetTevKColorSel(st, GX_TEV_KCSEL_K0);
        GX_SetTevOrder(st, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
        GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_C0, GX_CC_KONST, GX_CC_CPREV);
        st++;
    }
    /* stage 3: prev += C0 */
    tev_reset_stage(st);
    GX_SetTevOrder(st, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevColorIn(st, GX_CC_RASC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    st++;
    /* stage 4: out = prev * albedo (or the thermal remap) */
    tev_reset_stage(st);
    GX_SetTevOrder(st, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    if (s_mode == GFX_MODE_THERMAL) {
        /* warm: K + prev/2 ; cold: (K + prev/2) / 2 -- matches the desktop shader */
        bool warm = (mat->flags & GFX_MAT_WARM) != 0;
        GXColor k = warm ? (GXColor){ 230, 102, 26, 255 } : (GXColor){ 20, 31, 112, 255 };
        GX_SetTevKColor(GX_KCOLOR1, k);
        GX_SetTevKColorSel(st, GX_TEV_KCSEL_K1);
        GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_CPREV, GX_CC_HALF, GX_CC_KONST);
        GX_SetTevColorOp(st, GX_TEV_ADD, GX_TB_ZERO, warm ? GX_CS_SCALE_1 : GX_CS_DIVIDE_2, GX_TRUE, GX_TEVPREV);
    } else {
        GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO);
    }
    GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
    st++;
    GX_SetNumTevStages(st);
}

static void load_matrix(const NhMat34 *model, u32 slot)
{
    NhMat34 view, mv, nm;
    memcpy(view.m, s_view, sizeof(Mtx));
    nh_m34_mul(&mv, &view, model);
    nh_m34_normal_matrix(&nm, &mv);
    GX_LoadPosMtxImm(mv.m, slot);
    GX_LoadNrmMtxImm(nm.m, slot);
}

void gfx_draw(GfxMesh mesh, const GfxMaterial *mat, const NhMat34 *model)
{
    if (!mesh || mesh >= GFX_MAX_MESHES || !s_meshes[mesh].used)
        return;
    const WiiMesh *wm = &s_meshes[mesh];
    load_matrix(model, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    setup_material(wm, mat, model);
    GX_CallDispList(wm->dl, wm->dl_size);
    s_cur.triangles += wm->tris;
    s_cur.draw_calls++;
}

void gfx_draw_skinned(GfxMesh mesh, const GfxMaterial *mat, const NhMat34 *bones, int nbones)
{
    if (!mesh || mesh >= GFX_MAX_MESHES || !s_meshes[mesh].used || nbones < 1)
        return;
    const WiiMesh *wm = &s_meshes[mesh];
    nbones = NH_MIN(nbones, NH_MAX_BONES);
    for (int i = 0; i < nbones; i++)
        load_matrix(&bones[i], (u32)(GX_PNMTX0 + i * 3));
    GX_SetCurrentMtx(GX_PNMTX0);
    setup_material(wm, mat, &bones[0]);
    GX_CallDispList(wm->dl, wm->dl_size);
    s_cur.triangles += wm->tris;
    s_cur.draw_calls++;
}

void gfx_draw_quad3d(GfxTexture tex, const NhVec3 corners[4], float u0, float v0, float u1, float v1, NhColor c,
                     uint16_t flags)
{
    if (s_nq3d >= MAX_Q3D)
        return;
    Quad *q = &s_q3d[s_nq3d++];
    q->tex = tex;
    q->flags = flags;
    q->c = to_gx(c);
    float us[4] = { u0, u1, u1, u0 }, vs[4] = { v1, v1, v0, v0 };
    for (int i = 0; i < 4; i++) {
        q->p[i][0] = corners[i].x;
        q->p[i][1] = corners[i].y;
        q->p[i][2] = corners[i].z;
        q->uv[i][0] = us[i];
        q->uv[i][1] = vs[i];
    }
}

void gfx_draw_quad2d(GfxTexture tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                     NhColor color)
{
    if (s_nq2d >= MAX_Q2D)
        return;
    Quad *q = &s_q2d[s_nq2d++];
    q->tex = tex;
    q->flags = GFX_MAT_BLEND;
    q->c = to_gx(color);
    float xs[4] = { x, x + w, x + w, x }, ys[4] = { y, y, y + h, y + h };
    float us[4] = { u0, u1, u1, u0 }, vs[4] = { v0, v0, v1, v1 };
    for (int i = 0; i < 4; i++) {
        q->p[i][0] = xs[i];
        q->p[i][1] = ys[i];
        q->p[i][2] = -1.0f;
        q->uv[i][0] = us[i];
        q->uv[i][1] = vs[i];
    }
}

static void flush_quads(Quad *qs, int n, bool is3d)
{
    if (n == 0)
        return;
    if (is3d) {
        GX_LoadProjectionMtx(s_proj, GX_PERSPECTIVE);
        GX_LoadPosMtxImm(s_view, GX_PNMTX0);
        GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
        apply_fog(true);
    } else {
        Mtx44 ortho;
        guOrtho(ortho, 0, GFX_SCREEN_H, 0, GFX_SCREEN_W, 0, 1000.0f);
        GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
        Mtx ident;
        guMtxIdentity(ident);
        GX_LoadPosMtxImm(ident, GX_PNMTX0);
        GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        apply_fog(false);
    }
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
    tev_reset_stage(GX_TEVSTAGE0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetCullMode(GX_CULL_NONE);

    int i = 0;
    while (i < n) {
        GfxTexture tex = qs[i].tex;
        u16 add = qs[i].flags & GFX_MAT_ADD;
        int end = i;
        while (end < n && qs[end].tex == tex && (qs[end].flags & GFX_MAT_ADD) == add && end - i < 16383)
            end++;
        if (add)
            GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
        else
            GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GX_LoadTexObj(tex_obj(tex), GX_TEXMAP0);
        GX_Begin(GX_QUADS, GX_VTXFMT1, (u16)((end - i) * 4));
        for (int k = i; k < end; k++) {
            const Quad *q = &qs[k];
            for (int v = 0; v < 4; v++) {
                GX_Position3f32(q->p[v][0], q->p[v][1], q->p[v][2]);
                GX_Color4u8(q->c.r, q->c.g, q->c.b, q->c.a);
                GX_TexCoord2f32(q->uv[v][0], q->uv[v][1]);
            }
        }
        GX_End();
        s_cur.draw_calls++;
        s_cur.triangles += (u32)(end - i) * 2;
        i = end;
    }
}

void gfx_end_frame(void)
{
    flush_quads(s_q3d, s_nq3d, true);
    flush_quads(s_q2d, s_nq2d, false);
    s_nq3d = s_nq2d = 0;

    u64 t0 = gettime();
    s_cur.cpu_us = (u32)ticks_to_microsecs(t0 - s_frame_start);
    for (int i = 1; i < GFX_MAX_MESHES; i++)
        if (s_meshes[i].used)
            s_cur.mesh_bytes += s_meshes[i].bytes;
    for (int i = 1; i < GFX_MAX_TEXTURES; i++)
        if (s_textures[i].used)
            s_cur.tex_bytes += s_textures[i].bytes;

    /* CPU/GPU overlap: this frame's commands are already queued behind the
     * previous frame's copy. Wait for the previous frame only now, show it,
     * then queue this frame's copy and let the GPU chew on it while the CPU
     * simulates the next frame. Three XFBs so the one being written is never
     * the one being scanned out. */
    if (s_pending) {
        GX_WaitDrawDone();
        s_cur.gpu_wait_us = (u32)ticks_to_microsecs(gettime() - t0);
        VIDEO_SetNextFramebuffer(s_xfb[s_pending_fb]);
        if (s_first_frame) {
            VIDEO_SetBlack(FALSE);
            s_first_frame = false;
        }
        VIDEO_Flush();
        VIDEO_WaitVSync();
    }
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(s_xfb[s_fb], GX_TRUE);
    GX_SetDrawDone();
    s_pending = true;
    s_pending_fb = s_fb;
    s_fb = (s_fb + 1) % NUM_XFB;

    s_stats = s_cur;
    s_frame_start = gettime();
}

GfxStats gfx_last_stats(void)
{
    return s_stats;
}
