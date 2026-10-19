/* Night Heist - OpenGL 3.3 renderer that emulates the GX pipeline.
 *
 * Rules that keep it honest:
 *  - lighting is evaluated per VERTEX with the exact GX equation (nh_light.h),
 *    never per pixel; normals are not renormalised (GX doesn't either)
 *  - fragments are texture * vertex colour, i.e. one TEV GX_MODULATE stage
 *  - textures arrive as GX tiled bytes and are decoded with the same codec,
 *    so RGB565/IA8 precision loss matches the console
 *  - everything renders into a 640x480 offscreen buffer that is scaled to the
 *    window, so resolution and aliasing look like the Wii's EFB. */
#include <GL/glcorearb.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/nh_math.h"
#include "desktop_internal.h"
#include "platform/gfx.h"
#include "platform/platform.h"

/* ---- minimal GL loader --------------------------------------------------- */

#define GL_FUNCS(X)                                                                    \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                     \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                     \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                               \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                               \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                               \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                         \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                             \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                     \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                           \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                           \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                         \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                             \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                   \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                         \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                           \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                               \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                             \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                           \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                 \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                               \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                               \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                 \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                                               \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                                                 \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                                 \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                   \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv)                                   \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                     \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                     \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                           \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                   \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                   \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                             \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                     \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                       \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                                     \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                                             \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                                             \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                                               \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                                         \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                         \
    X(PFNGLVIEWPORTPROC, glViewport)                                                   \
    X(PFNGLCLEARCOLORPROC, glClearColor)                                               \
    X(PFNGLCLEARPROC, glClear)                                                         \
    X(PFNGLENABLEPROC, glEnable)                                                       \
    X(PFNGLDISABLEPROC, glDisable)                                                     \
    X(PFNGLBLENDFUNCPROC, glBlendFunc)                                                 \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc)                                                 \
    X(PFNGLDEPTHMASKPROC, glDepthMask)                                                 \
    X(PFNGLCULLFACEPROC, glCullFace)                                                   \
    X(PFNGLFRONTFACEPROC, glFrontFace)                                                 \
    X(PFNGLDRAWELEMENTSPROC, glDrawElements)                                           \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays)                                               \
    X(PFNGLREADPIXELSPROC, glReadPixels)                                               \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei)                                             \
    X(PFNGLFINISHPROC, glFinish)                                                       \
    X(PFNGLGETERRORPROC, glGetError)

#define X(type, name) static type p_##name;
GL_FUNCS(X)
#undef X
/* Assign through void** (the dlsym idiom) to stay ISO C clean. */
#define X(type, name) | (!(*(void **)&p_##name = SDL_GL_GetProcAddress(#name)))
static bool load_gl(void)
{
    return !(0 GL_FUNCS(X));
}
#undef X

/* ---- shaders -------------------------------------------------------------- */

static const char *k_vs3d =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_nrm;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "uniform mat4 u_proj;\n"
    "uniform mat4 u_mv;\n"
    "uniform mat3 u_nrm;\n"
    "uniform int u_nlights;\n"
    "uniform vec3 u_lpos[8];\n"
    "uniform vec3 u_ldir[8];\n"
    "uniform vec3 u_lcol[8];\n"
    "uniform vec3 u_la[8];\n"
    "uniform vec3 u_lk[8];\n"
    "uniform vec4 u_amb;\n"
    "uniform vec4 u_mat;\n"
    "out vec4 v_col;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec3 P = (u_mv * vec4(a_pos, 1.0)).xyz;\n"
    "  vec3 N = u_nrm * a_nrm;\n" /* GX does not renormalise */
    "  vec3 sum = u_amb.rgb;\n"
    "  for (int i = 0; i < u_nlights; i++) {\n"
    "    vec3 L = u_lpos[i] - P;\n"
    "    float d = length(L);\n"
    "    L /= max(d, 1e-6);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    float c = max(dot(-L, u_ldir[i]), 0.0);\n"
    "    float ang = max(u_la[i].x + u_la[i].y * c + u_la[i].z * c * c, 0.0);\n"
    "    float dist = u_lk[i].x + u_lk[i].y * d + u_lk[i].z * d * d;\n"
    "    sum += u_lcol[i] * diff * ang / max(dist, 1e-6);\n"
    "  }\n"
    "  vec3 lit = u_mat.rgb * clamp(sum, 0.0, 1.0);\n"
    "  v_col = vec4(floor(lit * 255.0 + 0.5) / 255.0, u_mat.a);\n" /* 8-bit vertex colour like GX */
    "  v_uv = a_uv;\n"
    "  gl_Position = u_proj * vec4(P, 1.0);\n"
    "}\n";

static const char *k_vs2d =
    "#version 330 core\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec4 a_col;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "uniform vec2 u_scale;\n"
    "out vec4 v_col;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  v_col = a_col;\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = vec4(a_pos.x * u_scale.x - 1.0, 1.0 - a_pos.y * u_scale.y, 0.0, 1.0);\n"
    "}\n";

/* TEV stage 0 = GX_MODULATE: texture colour * rasterised colour. */
static const char *k_fs_modulate =
    "#version 330 core\n"
    "in vec4 v_col;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 o_col;\n"
    "void main() { o_col = texture(u_tex, v_uv) * v_col; }\n";

/* ---- state ---------------------------------------------------------------- */

typedef struct GlMesh {
    GLuint vao, vbo, ibo;
    GLsizei icount;
    uint32_t tris;
} GlMesh;

typedef struct Vert2D {
    float x, y;
    uint8_t r, g, b, a;
    float u, v;
} Vert2D;

#define MAX_QUADS 4096

typedef struct Quad2D {
    GfxTexture tex;
    Vert2D v[4];
} Quad2D;

static SDL_GLContext s_ctx;
static GLuint s_prog3d, s_prog2d;
static GLint u3_proj, u3_mv, u3_nrm, u3_nlights, u3_lpos, u3_ldir, u3_lcol, u3_la, u3_lk, u3_amb, u3_mat, u3_tex;
static GLint u2_scale, u2_tex;
static GLuint s_fbo, s_fbo_color, s_fbo_depth;
static GLuint s_vao2d, s_vbo2d;

static GlMesh s_meshes[GFX_MAX_MESHES];
static int s_mesh_count = 1;
static GLuint s_textures[GFX_MAX_TEXTURES];
static int s_texture_count = 1;

static NhMat34 s_view;
static bool s_3d_state_set;
static Quad2D s_quads[MAX_QUADS];
static int s_quad_count;
static Vert2D s_vert_scratch[MAX_QUADS * 6];

static GfxStats s_stats, s_cur;
static uint64_t s_frame_start;
static uint64_t s_frames;

static GLuint compile(GLenum type, const char *src)
{
    GLuint sh = p_glCreateShader(type);
    p_glShaderSource(sh, 1, &src, NULL);
    p_glCompileShader(sh);
    GLint ok = 0;
    p_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        p_glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile failed:\n%s\n", log);
        return 0;
    }
    return sh;
}

static GLuint link(const char *vs, const char *fs)
{
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f)
        return 0;
    GLuint p = p_glCreateProgram();
    p_glAttachShader(p, v);
    p_glAttachShader(p, f);
    p_glLinkProgram(p);
    GLint ok = 0;
    p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        p_glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "program link failed:\n%s\n", log);
        return 0;
    }
    return p;
}

bool desktop_gfx_init(void)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    int win_w = g_desktop_opts.wide ? 1280 * 4 / 3 : 1280, win_h = 960;
    g_window = SDL_CreateWindow("Night Heist", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    s_ctx = SDL_GL_CreateContext(g_window);
    if (!s_ctx || !load_gl()) {
        fprintf(stderr, "OpenGL 3.3 core context unavailable: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(g_desktop_opts.vsync ? 1 : 0);

    s_prog3d = link(k_vs3d, k_fs_modulate);
    s_prog2d = link(k_vs2d, k_fs_modulate);
    if (!s_prog3d || !s_prog2d)
        return false;
    u3_proj = p_glGetUniformLocation(s_prog3d, "u_proj");
    u3_mv = p_glGetUniformLocation(s_prog3d, "u_mv");
    u3_nrm = p_glGetUniformLocation(s_prog3d, "u_nrm");
    u3_nlights = p_glGetUniformLocation(s_prog3d, "u_nlights");
    u3_lpos = p_glGetUniformLocation(s_prog3d, "u_lpos");
    u3_ldir = p_glGetUniformLocation(s_prog3d, "u_ldir");
    u3_lcol = p_glGetUniformLocation(s_prog3d, "u_lcol");
    u3_la = p_glGetUniformLocation(s_prog3d, "u_la");
    u3_lk = p_glGetUniformLocation(s_prog3d, "u_lk");
    u3_amb = p_glGetUniformLocation(s_prog3d, "u_amb");
    u3_mat = p_glGetUniformLocation(s_prog3d, "u_mat");
    u3_tex = p_glGetUniformLocation(s_prog3d, "u_tex");
    u2_scale = p_glGetUniformLocation(s_prog2d, "u_scale");
    u2_tex = p_glGetUniformLocation(s_prog2d, "u_tex");

    /* 640x480 "EFB": RGB8 colour + 24-bit depth, like GX_PF_RGB8_Z24. */
    p_glGenTextures(1, &s_fbo_color);
    p_glBindTexture(GL_TEXTURE_2D, s_fbo_color);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, GFX_SCREEN_W, GFX_SCREEN_H, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    p_glGenRenderbuffers(1, &s_fbo_depth);
    p_glBindRenderbuffer(GL_RENDERBUFFER, s_fbo_depth);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, GFX_SCREEN_W, GFX_SCREEN_H);
    p_glGenFramebuffers(1, &s_fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_fbo_color, 0);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_fbo_depth);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "offscreen framebuffer incomplete\n");
        return false;
    }

    p_glGenVertexArrays(1, &s_vao2d);
    p_glBindVertexArray(s_vao2d);
    p_glGenBuffers(1, &s_vbo2d);
    p_glBindBuffer(GL_ARRAY_BUFFER, s_vbo2d);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof(s_vert_scratch), NULL, GL_STREAM_DRAW);
    p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert2D), (void *)offsetof(Vert2D, x));
    p_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vert2D), (void *)offsetof(Vert2D, r));
    p_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vert2D), (void *)offsetof(Vert2D, u));
    p_glEnableVertexAttribArray(0);
    p_glEnableVertexAttribArray(1);
    p_glEnableVertexAttribArray(2);
    p_glBindVertexArray(0);

    s_frame_start = plat_time_us();
    return true;
}

void desktop_gfx_shutdown(void)
{
    if (s_ctx)
        SDL_GL_DeleteContext(s_ctx);
    if (g_window)
        SDL_DestroyWindow(g_window);
    s_ctx = NULL;
    g_window = NULL;
}

void desktop_gfx_viewport(int *x, int *y, int *w, int *h)
{
    int ww, wh;
    SDL_GetWindowSize(g_window, &ww, &wh);
    float aspect = gfx_display_aspect();
    int vw = ww, vh = (int)((float)ww / aspect);
    if (vh > wh) {
        vh = wh;
        vw = (int)((float)wh * aspect);
    }
    *x = (ww - vw) / 2;
    *y = (wh - vh) / 2;
    *w = vw;
    *h = vh;
}

uint64_t desktop_frame_count(void)
{
    return s_frames;
}

float gfx_display_aspect(void)
{
    return g_desktop_opts.wide ? 16.0f / 9.0f : 4.0f / 3.0f;
}

/* ---- resources ------------------------------------------------------------ */

GfxMesh gfx_mesh_create(const NhMeshData *m)
{
    if (s_mesh_count >= GFX_MAX_MESHES || m->icount == 0)
        return 0;
    GlMesh *gm = &s_meshes[s_mesh_count];
    size_t stride = sizeof(float) * 8;
    float *interleaved = malloc(stride * m->vcount);
    if (!interleaved)
        return 0;
    for (uint32_t i = 0; i < m->vcount; i++) {
        float *v = interleaved + i * 8;
        v[0] = m->pos[i].x;
        v[1] = m->pos[i].y;
        v[2] = m->pos[i].z;
        v[3] = m->nrm[i].x;
        v[4] = m->nrm[i].y;
        v[5] = m->nrm[i].z;
        v[6] = m->uv[i].x;
        v[7] = m->uv[i].y;
    }
    p_glGenVertexArrays(1, &gm->vao);
    p_glBindVertexArray(gm->vao);
    p_glGenBuffers(1, &gm->vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, gm->vbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(stride * m->vcount), interleaved, GL_STATIC_DRAW);
    p_glGenBuffers(1, &gm->ibo);
    p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm->ibo);
    p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint16_t) * m->icount), m->idx, GL_STATIC_DRAW);
    p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void *)0);
    p_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void *)(sizeof(float) * 3));
    p_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void *)(sizeof(float) * 6));
    p_glEnableVertexAttribArray(0);
    p_glEnableVertexAttribArray(1);
    p_glEnableVertexAttribArray(2);
    p_glBindVertexArray(0);
    free(interleaved);
    gm->icount = (GLsizei)m->icount;
    gm->tris = m->icount / 3;
    return (GfxMesh)s_mesh_count++;
}

GfxTexture gfx_texture_create(const GfxTextureDesc *d)
{
    if (s_texture_count >= GFX_MAX_TEXTURES || d->levels < 1)
        return 0;
    uint8_t *rgba = malloc((size_t)d->width * d->height * 4);
    if (!rgba)
        return 0;
    GLuint tex;
    p_glGenTextures(1, &tex);
    p_glBindTexture(GL_TEXTURE_2D, tex);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const uint8_t *src = d->data;
    int w = d->width, h = d->height;
    for (int lvl = 0; lvl < d->levels; lvl++) {
        nh_tex_decode(d->format, src, w, h, rgba);
        p_glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        src += nh_tex_level_size(d->format, w, h);
        w = NH_MAX(w / 2, 1);
        h = NH_MAX(h / 2, 1);
    }
    free(rgba);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, d->levels - 1);
    GLint wrap = d->repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    GLint minf = d->levels > 1 ? (d->filter ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
                               : (d->filter ? GL_LINEAR : GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, d->filter ? GL_LINEAR : GL_NEAREST);
    s_textures[s_texture_count] = tex;
    return (GfxTexture)s_texture_count++;
}

/* ---- frame ---------------------------------------------------------------- */

/* Row-major 3x4 -> row-major 4x4 (uploaded with transpose = GL_TRUE). */
static void m34_to_44(const NhMat34 *in, float out[16])
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = in->m[r][c];
    out[12] = out[13] = out[14] = 0.0f;
    out[15] = 1.0f;
}

void gfx_begin_frame(NhColor clear)
{
    s_cur = (GfxStats){ 0 };
    s_3d_state_set = false;
    s_quad_count = 0;
    p_glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    p_glViewport(0, 0, GFX_SCREEN_W, GFX_SCREEN_H);
    p_glDepthMask(GL_TRUE);
    p_glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f);
    p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gfx_set_camera(const NhMat34 *view, float fovy_deg, float znear, float zfar)
{
    float f = 1.0f / tanf(NH_DEG2RAD(fovy_deg) * 0.5f);
    float aspect = gfx_display_aspect();
    float proj[16] = { f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (zfar + znear) / (znear - zfar),
                       2.0f * zfar * znear / (znear - zfar), 0, 0, -1, 0 };
    p_glUseProgram(s_prog3d);
    p_glUniformMatrix4fv(u3_proj, 1, GL_TRUE, proj);
    s_view = *view;
}

void gfx_set_lights(const NhLight *lights, int count, NhColor ambient)
{
    float pos[NH_MAX_HW_LIGHTS * 3], dir[NH_MAX_HW_LIGHTS * 3], col[NH_MAX_HW_LIGHTS * 3];
    float la[NH_MAX_HW_LIGHTS * 3], lk[NH_MAX_HW_LIGHTS * 3];
    count = NH_MIN(count, NH_MAX_HW_LIGHTS);
    for (int i = 0; i < count; i++) {
        const NhLight *l = &lights[i];
        NhVec3 p = nh_m34_mul_point(&s_view, l->pos);
        NhVec3 d = nh_m34_mul_dir(&s_view, l->dir);
        pos[i * 3] = p.x;
        pos[i * 3 + 1] = p.y;
        pos[i * 3 + 2] = p.z;
        dir[i * 3] = d.x;
        dir[i * 3 + 1] = d.y;
        dir[i * 3 + 2] = d.z;
        col[i * 3] = l->color.r / 255.0f;
        col[i * 3 + 1] = l->color.g / 255.0f;
        col[i * 3 + 2] = l->color.b / 255.0f;
        for (int k = 0; k < 3; k++) {
            la[i * 3 + k] = l->a[k];
            lk[i * 3 + k] = l->k[k];
        }
    }
    p_glUseProgram(s_prog3d);
    p_glUniform1i(u3_nlights, count);
    if (count > 0) {
        p_glUniform3fv(u3_lpos, count, pos);
        p_glUniform3fv(u3_ldir, count, dir);
        p_glUniform3fv(u3_lcol, count, col);
        p_glUniform3fv(u3_la, count, la);
        p_glUniform3fv(u3_lk, count, lk);
    }
    p_glUniform4f(u3_amb, ambient.r / 255.0f, ambient.g / 255.0f, ambient.b / 255.0f, ambient.a / 255.0f);
}

static void set_3d_state(void)
{
    p_glUseProgram(s_prog3d);
    p_glUniform1i(u3_tex, 0);
    p_glActiveTexture(GL_TEXTURE0);
    p_glEnable(GL_DEPTH_TEST);
    p_glDepthFunc(GL_LEQUAL);
    p_glDepthMask(GL_TRUE);
    p_glDisable(GL_BLEND);
    p_glEnable(GL_CULL_FACE);
    p_glFrontFace(GL_CCW);
    p_glCullFace(GL_BACK);
    s_3d_state_set = true;
}

void gfx_draw_mesh(GfxMesh mesh, GfxTexture tex, const NhMat34 *model, NhColor material)
{
    if (mesh == 0 || mesh >= s_mesh_count || tex == 0 || tex >= s_texture_count)
        return;
    if (!s_3d_state_set)
        set_3d_state();

    NhMat34 mv, nm;
    nh_m34_mul(&mv, &s_view, model);
    nh_m34_normal_matrix(&nm, &mv);
    float mv44[16], n33[9];
    m34_to_44(&mv, mv44);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            n33[r * 3 + c] = nm.m[r][c];
    p_glUniformMatrix4fv(u3_mv, 1, GL_TRUE, mv44);
    p_glUniformMatrix3fv(u3_nrm, 1, GL_TRUE, n33);
    p_glUniform4f(u3_mat, material.r / 255.0f, material.g / 255.0f, material.b / 255.0f, material.a / 255.0f);

    const GlMesh *gm = &s_meshes[mesh];
    p_glBindTexture(GL_TEXTURE_2D, s_textures[tex]);
    p_glBindVertexArray(gm->vao);
    p_glDrawElements(GL_TRIANGLES, gm->icount, GL_UNSIGNED_SHORT, NULL);
    s_cur.triangles += gm->tris;
    s_cur.draw_calls++;
}

void gfx_draw_quad2d(GfxTexture tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                     NhColor c)
{
    if (s_quad_count >= MAX_QUADS || tex == 0 || tex >= s_texture_count)
        return;
    Quad2D *q = &s_quads[s_quad_count++];
    q->tex = tex;
    q->v[0] = (Vert2D){ x, y, c.r, c.g, c.b, c.a, u0, v0 };
    q->v[1] = (Vert2D){ x + w, y, c.r, c.g, c.b, c.a, u1, v0 };
    q->v[2] = (Vert2D){ x + w, y + h, c.r, c.g, c.b, c.a, u1, v1 };
    q->v[3] = (Vert2D){ x, y + h, c.r, c.g, c.b, c.a, u0, v1 };
}

static void flush_2d(void)
{
    if (s_quad_count == 0)
        return;
    p_glUseProgram(s_prog2d);
    p_glUniform1i(u2_tex, 0);
    p_glUniform2f(u2_scale, 2.0f / GFX_SCREEN_W, 2.0f / GFX_SCREEN_H);
    p_glDisable(GL_DEPTH_TEST);
    p_glDepthMask(GL_FALSE);
    p_glDisable(GL_CULL_FACE);
    p_glEnable(GL_BLEND);
    p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindVertexArray(s_vao2d);
    p_glBindBuffer(GL_ARRAY_BUFFER, s_vbo2d);

    static const int order[6] = { 0, 1, 2, 0, 2, 3 };
    int i = 0;
    while (i < s_quad_count) {
        GfxTexture tex = s_quads[i].tex;
        int n = 0;
        while (i < s_quad_count && s_quads[i].tex == tex) {
            for (int k = 0; k < 6; k++)
                s_vert_scratch[n * 6 + k] = s_quads[i].v[order[k]];
            n++;
            i++;
        }
        p_glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(Vert2D) * 6 * (size_t)n), s_vert_scratch);
        p_glBindTexture(GL_TEXTURE_2D, s_textures[tex]);
        p_glDrawArrays(GL_TRIANGLES, 0, n * 6);
        s_cur.draw_calls++;
        s_cur.triangles += (uint32_t)n * 2;
    }
    p_glBindVertexArray(0);
    s_quad_count = 0;
}

static void save_screenshot(const char *path)
{
    uint8_t *px = malloc(GFX_SCREEN_W * GFX_SCREEN_H * 3);
    if (!px)
        return;
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo);
    p_glPixelStorei(GL_PACK_ALIGNMENT, 1);
    p_glReadPixels(0, 0, GFX_SCREEN_W, GFX_SCREEN_H, GL_RGB, GL_UNSIGNED_BYTE, px);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, GFX_SCREEN_W, GFX_SCREEN_H, 24, SDL_PIXELFORMAT_RGB24);
    if (s) {
        for (int y = 0; y < GFX_SCREEN_H; y++) /* GL rows are bottom-up */
            memcpy((uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch,
                   px + (size_t)(GFX_SCREEN_H - 1 - y) * GFX_SCREEN_W * 3, GFX_SCREEN_W * 3);
        if (SDL_SaveBMP(s, path) != 0)
            fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
        SDL_FreeSurface(s);
    }
    free(px);
}

void gfx_end_frame(void)
{
    flush_2d();
    uint64_t t0 = plat_time_us();
    s_cur.cpu_us = (uint32_t)(t0 - s_frame_start);

    s_frames++;
    if (g_desktop_opts.frames > 0 && s_frames >= (uint64_t)g_desktop_opts.frames) {
        if (g_desktop_opts.screenshot)
            save_screenshot(g_desktop_opts.screenshot);
        plat_request_quit();
    }

    /* Scale the 640x480 image into the window, letterboxed. */
    int vx, vy, vw, vh, ww, wh;
    desktop_gfx_viewport(&vx, &vy, &vw, &vh);
    SDL_GL_GetDrawableSize(g_window, &ww, &wh);
    int sw, sh;
    SDL_GetWindowSize(g_window, &sw, &sh);
    float sx = sw > 0 ? (float)ww / (float)sw : 1.0f, sy = sh > 0 ? (float)wh / (float)sh : 1.0f;
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo);
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    p_glViewport(0, 0, ww, wh);
    p_glClearColor(0, 0, 0, 1);
    p_glClear(GL_COLOR_BUFFER_BIT);
    int x0 = (int)(vx * sx), y0 = (int)(vy * sy), x1 = (int)((vx + vw) * sx), y1 = (int)((vy + vh) * sy);
    p_glBlitFramebuffer(0, 0, GFX_SCREEN_W, GFX_SCREEN_H, x0, wh - y1, x1, wh - y0, GL_COLOR_BUFFER_BIT,
                        GL_LINEAR);
    SDL_GL_SwapWindow(g_window);
    s_cur.gpu_wait_us = (uint32_t)(plat_time_us() - t0);

    s_stats = s_cur;
    s_frame_start = plat_time_us();
}

GfxStats gfx_last_stats(void)
{
    return s_stats;
}
