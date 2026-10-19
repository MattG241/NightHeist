/* Night Heist - OpenGL 3.3 renderer that emulates the GX pipeline.
 *
 * Rules that keep it honest:
 *  - lighting is evaluated per VERTEX with the exact GX equation (nh_light.h),
 *    never per pixel; normals are not renormalised (GX doesn't either), and
 *    channel colours are quantised to 8 bits like GX's rasteriser inputs
 *  - fragments follow the same TEV stage chain the Wii backend programs:
 *      prev = cookieA * C1; prev += cookieB * A1 * K; prev += C0; out = prev * albedo
 *    with per-stage clamping, and projective texgen divided per pixel
 *  - textures arrive as GX tiled bytes and are decoded with the shared codec
 *  - everything renders into a 640x480 offscreen buffer scaled to the window */
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

#define GL_FUNCS(X)                                                  \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                   \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)             \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                   \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                             \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                       \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                             \
    X(PFNGLBUFFERDATAPROC, glBufferData)                             \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                       \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)           \
    X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer)         \
    X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f)                     \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)   \
    X(PFNGLCREATESHADERPROC, glCreateShader)                         \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                         \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                       \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                           \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                 \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                       \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                         \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                           \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                         \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)               \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                             \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)             \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                               \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                               \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                             \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                               \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                               \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                 \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv)                 \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                   \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                   \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)         \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                 \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                 \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)           \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)   \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)     \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                   \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                           \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                     \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                           \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                             \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                       \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                       \
    X(PFNGLVIEWPORTPROC, glViewport)                                 \
    X(PFNGLCLEARCOLORPROC, glClearColor)                             \
    X(PFNGLCLEARPROC, glClear)                                       \
    X(PFNGLENABLEPROC, glEnable)                                     \
    X(PFNGLDISABLEPROC, glDisable)                                   \
    X(PFNGLBLENDFUNCPROC, glBlendFunc)                               \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc)                               \
    X(PFNGLDEPTHMASKPROC, glDepthMask)                               \
    X(PFNGLCULLFACEPROC, glCullFace)                                 \
    X(PFNGLFRONTFACEPROC, glFrontFace)                               \
    X(PFNGLDRAWELEMENTSPROC, glDrawElements)                         \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays)                             \
    X(PFNGLREADPIXELSPROC, glReadPixels)                             \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei)

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
    "layout(location=2) in uint a_bone;\n"
    "layout(location=3) in vec2 a_uv;\n"
    "layout(location=4) in vec4 a_col;\n"
    "uniform mat4 u_proj;\n"
    "uniform mat4 u_mv[10];\n"
    "uniform mat3 u_nm[10];\n"
    "uniform int u_skinned;\n"
    "uniform mat4 u_tgA, u_tgB;\n" /* texgen: model pos -> (s,t,q) */
    "uniform int u_nlights;\n"
    "uniform vec3 u_lpos[8], u_ldir[8], u_lcol[8], u_la[8], u_lk[8];\n" /* 0-5 dyn, 6 torch, 7 guard */
    "uniform int u_torch, u_guard;\n"
    "uniform vec4 u_amb, u_tint;\n"
    "uniform int u_baked, u_unlit;\n"
    "out vec4 v_c0;\n"
    "out vec4 v_c1;\n"
    "out vec2 v_uv;\n"
    "out vec3 v_stqA, v_stqB;\n"
    "out float v_depth;\n"
    "float lightf(int i, vec3 P, vec3 N) {\n"
    "  vec3 L = u_lpos[i] - P; float d = length(L); L /= max(d, 1e-6);\n"
    "  float diff = max(dot(N, L), 0.0);\n"
    "  float c = max(dot(-L, u_ldir[i]), 0.0);\n"
    "  float ang = max(u_la[i].x + u_la[i].y * c + u_la[i].z * c * c, 0.0);\n"
    "  float dist = u_lk[i].x + u_lk[i].y * d + u_lk[i].z * d * d;\n"
    "  return diff * ang / max(dist, 1e-6);\n"
    "}\n"
    "vec3 q8(vec3 c) { return floor(clamp(c, 0.0, 1.0) * 255.0 + 0.5) / 255.0; }\n"
    "void main() {\n"
    "  int b = u_skinned != 0 ? int(a_bone) : 0;\n"
    "  vec3 mp = a_pos / 128.0;\n"
    "  vec3 P = (u_mv[b] * vec4(mp, 1.0)).xyz;\n"
    "  vec3 N = u_nm[b] * (a_nrm / 64.0);\n"
    "  v_uv = a_uv / 256.0;\n"
    "  v_depth = -P.z;\n"
    "  if (u_unlit != 0) {\n"
    "    v_c0 = vec4(q8(u_baked != 0 ? a_col.rgb : u_tint.rgb), u_tint.a);\n"
    "    v_c1 = vec4(0.0);\n"
    "  } else {\n"
    "    vec3 sum = u_baked != 0 ? a_col.rgb : u_amb.rgb;\n"
    "    for (int i = 0; i < u_nlights; i++) sum += u_lcol[i] * lightf(i, P, N);\n"
    "    v_c0 = vec4(q8(u_tint.rgb * clamp(sum, 0.0, 1.0)), u_tint.a);\n"
    "    vec3 t = u_torch != 0 ? q8(u_tint.rgb * clamp(u_lcol[6] * lightf(6, P, N), 0.0, 1.0)) : vec3(0.0);\n"
    "    float g = u_guard != 0 ? clamp(lightf(7, P, N), 0.0, 1.0) : 0.0;\n"
    "    v_c1 = vec4(t, floor(g * 255.0 + 0.5) / 255.0);\n"
    "  }\n"
    "  v_stqA = (u_tgA * vec4(mp, 1.0)).xyz;\n"
    "  v_stqB = (u_tgB * vec4(mp, 1.0)).xyz;\n"
    "  gl_Position = u_proj * vec4(P, 1.0);\n"
    "}\n";

static const char *k_fs3d =
    "#version 330 core\n"
    "in vec4 v_c0;\n"
    "in vec4 v_c1;\n"
    "in vec2 v_uv;\n"
    "in vec3 v_stqA, v_stqB;\n"
    "in float v_depth;\n"
    "uniform sampler2D u_tex, u_cookie;\n"
    "uniform int u_unlit, u_cookieA, u_cookieB, u_alphatest, u_thermal, u_warm, u_fogon;\n"
    "uniform vec3 u_gcol, u_fogcol;\n"
    "uniform vec2 u_fog;\n"
    "out vec4 o_col;\n"
    "float cookie(vec3 stq) {\n"
    "  if (stq.z <= 0.0) return 0.0;\n"
    "  return texture(u_cookie, stq.xy / stq.z).r;\n"
    "}\n"
    "void main() {\n"
    "  vec4 tex = texture(u_tex, v_uv);\n"
    "  vec3 c;\n"
    "  if (u_unlit != 0) {\n"
    "    c = tex.rgb * v_c0.rgb;\n"
    "  } else {\n"
    "    vec3 prev = (u_cookieA != 0 ? cookie(v_stqA) : 1.0) * v_c1.rgb;\n" /* TEV stage 0 */
    "    float rb = (u_cookieB != 0 ? cookie(v_stqB) : 1.0) * v_c1.a;\n"    /* stage 1 -> reg0 */
    "    prev = clamp(prev + rb * u_gcol, 0.0, 1.0);\n"                     /* stage 2 */
    "    prev = clamp(prev + v_c0.rgb, 0.0, 1.0);\n"                        /* stage 3 */
    "    c = prev * tex.rgb;\n"                                             /* stage 4 */
    "    if (u_thermal != 0) {\n"
    "      c = u_warm != 0 ? clamp(vec3(0.902, 0.4, 0.102) + prev * 0.5, 0.0, 1.0)\n"
    "                      : clamp((vec3(0.078, 0.122, 0.439) + prev * 0.5) * 0.5, 0.0, 1.0);\n"
    "    }\n"
    "  }\n"
    "  float a = tex.a * v_c0.a;\n"
    "  if (u_alphatest != 0 && a < 0.5) discard;\n"
    "  if (u_fogon != 0) c = mix(c, u_fogcol, clamp((v_depth - u_fog.x) / (u_fog.y - u_fog.x), 0.0, 1.0));\n"
    "  o_col = vec4(c, a);\n"
    "}\n";

static const char *k_vsq =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec4 a_col;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "uniform vec4 u_vrow2;\n" /* view matrix row 2, for fog depth */
    "out vec4 v_col;\n"
    "out vec2 v_uv;\n"
    "out float v_depth;\n"
    "void main() {\n"
    "  v_col = a_col; v_uv = a_uv;\n"
    "  v_depth = -dot(u_vrow2, vec4(a_pos, 1.0));\n"
    "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char *k_fsq =
    "#version 330 core\n"
    "in vec4 v_col;\n"
    "in vec2 v_uv;\n"
    "in float v_depth;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_fogon;\n"
    "uniform vec2 u_fog;\n"
    "out vec4 o_col;\n"
    "void main() {\n"
    "  vec4 c = texture(u_tex, v_uv) * v_col;\n"
    "  if (u_fogon != 0) c.rgb *= 1.0 - clamp((v_depth - u_fog.x) / (u_fog.y - u_fog.x), 0.0, 1.0);\n"
    "  o_col = c;\n"
    "}\n";

/* ---- state ---------------------------------------------------------------- */

typedef struct GlMesh {
    GLuint vao, vbo, cbo, ibo;
    GLsizei icount;
    uint32_t vcount, tris, bytes;
    bool skinned, has_colors, used;
} GlMesh;

typedef struct GlTex {
    GLuint id;
    uint32_t bytes;
    bool used;
} GlTex;

typedef struct QVert {
    float x, y, z;
    uint8_t r, g, b, a;
    float u, v;
} QVert;

#define MAX_Q3D 4096
#define MAX_Q2D 4096

typedef struct Quad {
    GfxTexture tex;
    uint16_t flags;
    QVert v[4];
} Quad;

typedef struct Prog3D {
    GLuint id;
    GLint proj, mv, nm, skinned, tgA, tgB, nlights, lpos, ldir, lcol, la, lk, torch, guard, amb, tint, baked,
        unlit, tex, cookie, cookieA, cookieB, alphatest, thermal, warm, fogon, gcol, fogcol, fog;
} Prog3D;

static SDL_GLContext s_ctx;
static Prog3D P;
static GLuint s_progq;
static GLint uq_mvp, uq_vrow2, uq_tex, uq_fogon, uq_fog;
static GLuint s_fbo, s_fbo_color, s_fbo_depth;
static GLuint s_vaoq, s_vboq;
static GLuint s_white;

static GlMesh s_meshes[GFX_MAX_MESHES];
static GlTex s_tex[GFX_MAX_TEXTURES];

static NhMat34 s_view;
static float s_proj[16];
static GfxProjector s_projA, s_projB;
static GfxFog s_fog;
static GfxMode s_mode;
static NhLight s_dyn[GFX_MAX_DYN_LIGHTS];
static int s_ndyn;

static Quad s_q3d[MAX_Q3D];
static int s_nq3d;
static Quad s_q2d[MAX_Q2D];
static int s_nq2d;
static QVert s_qscratch[MAX_Q2D * 6];

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
        char log[2048];
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
        char log[2048];
        p_glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "program link failed:\n%s\n", log);
        return 0;
    }
    return p;
}

#define U(name) p_glGetUniformLocation(P.id, name)

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

    P.id = link(k_vs3d, k_fs3d);
    s_progq = link(k_vsq, k_fsq);
    if (!P.id || !s_progq)
        return false;
    P.proj = U("u_proj");
    P.mv = U("u_mv");
    P.nm = U("u_nm");
    P.skinned = U("u_skinned");
    P.tgA = U("u_tgA");
    P.tgB = U("u_tgB");
    P.nlights = U("u_nlights");
    P.lpos = U("u_lpos");
    P.ldir = U("u_ldir");
    P.lcol = U("u_lcol");
    P.la = U("u_la");
    P.lk = U("u_lk");
    P.torch = U("u_torch");
    P.guard = U("u_guard");
    P.amb = U("u_amb");
    P.tint = U("u_tint");
    P.baked = U("u_baked");
    P.unlit = U("u_unlit");
    P.tex = U("u_tex");
    P.cookie = U("u_cookie");
    P.cookieA = U("u_cookieA");
    P.cookieB = U("u_cookieB");
    P.alphatest = U("u_alphatest");
    P.thermal = U("u_thermal");
    P.warm = U("u_warm");
    P.fogon = U("u_fogon");
    P.gcol = U("u_gcol");
    P.fogcol = U("u_fogcol");
    P.fog = U("u_fog");
    uq_mvp = p_glGetUniformLocation(s_progq, "u_mvp");
    uq_vrow2 = p_glGetUniformLocation(s_progq, "u_vrow2");
    uq_tex = p_glGetUniformLocation(s_progq, "u_tex");
    uq_fogon = p_glGetUniformLocation(s_progq, "u_fogon");
    uq_fog = p_glGetUniformLocation(s_progq, "u_fog");

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

    p_glGenVertexArrays(1, &s_vaoq);
    p_glBindVertexArray(s_vaoq);
    p_glGenBuffers(1, &s_vboq);
    p_glBindBuffer(GL_ARRAY_BUFFER, s_vboq);
    p_glBufferData(GL_ARRAY_BUFFER, sizeof(s_qscratch), NULL, GL_STREAM_DRAW);
    p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QVert), (void *)offsetof(QVert, x));
    p_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(QVert), (void *)offsetof(QVert, r));
    p_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(QVert), (void *)offsetof(QVert, u));
    p_glEnableVertexAttribArray(0);
    p_glEnableVertexAttribArray(1);
    p_glEnableVertexAttribArray(2);
    p_glBindVertexArray(0);

    static const uint8_t white[4] = { 255, 255, 255, 255 };
    p_glGenTextures(1, &s_white);
    p_glBindTexture(GL_TEXTURE_2D, s_white);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

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

GfxMesh gfx_mesh_create(const NhMesh *m, bool dynamic_colors)
{
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
    GlMesh *gm = &s_meshes[slot];
    memset(gm, 0, sizeof(*gm));
    gm->used = true;
    gm->skinned = m->skinned;
    gm->vcount = m->vcount;
    p_glGenVertexArrays(1, &gm->vao);
    p_glBindVertexArray(gm->vao);
    p_glGenBuffers(1, &gm->vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, gm->vbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(NhVertex) * m->vcount), m->verts, GL_STATIC_DRAW);
    GLsizei st = sizeof(NhVertex);
    p_glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, st, (void *)offsetof(NhVertex, pos));
    p_glVertexAttribPointer(1, 3, GL_BYTE, GL_FALSE, st, (void *)offsetof(NhVertex, nrm));
    p_glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, st, (void *)offsetof(NhVertex, bone));
    p_glVertexAttribPointer(3, 2, GL_SHORT, GL_FALSE, st, (void *)offsetof(NhVertex, uv));
    for (int a = 0; a < 4; a++)
        p_glEnableVertexAttribArray((GLuint)a);
    if (m->colors) {
        gm->has_colors = true;
        p_glGenBuffers(1, &gm->cbo);
        p_glBindBuffer(GL_ARRAY_BUFFER, gm->cbo);
        p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(NhColor) * m->vcount), m->colors,
                       dynamic_colors ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        p_glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(NhColor), (void *)0);
        p_glEnableVertexAttribArray(4);
    }
    p_glGenBuffers(1, &gm->ibo);
    p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm->ibo);
    p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint16_t) * m->icount), m->idx, GL_STATIC_DRAW);
    p_glBindVertexArray(0);
    gm->icount = (GLsizei)m->icount;
    gm->tris = m->icount / 3;
    /* Count what the Wii would hold: arrays + display list indices. */
    gm->bytes = m->vcount * (6 + 3 + 4 + (m->colors ? 4 : 0)) + m->icount * (m->skinned ? 9 : 8);
    return (GfxMesh)slot;
}

void gfx_mesh_set_colors(GfxMesh h, const NhColor *colors)
{
    if (!h || h >= GFX_MAX_MESHES || !s_meshes[h].used || !s_meshes[h].has_colors)
        return;
    GlMesh *gm = &s_meshes[h];
    p_glBindBuffer(GL_ARRAY_BUFFER, gm->cbo);
    p_glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(NhColor) * gm->vcount), colors);
}

void gfx_mesh_destroy(GfxMesh h)
{
    if (!h || h >= GFX_MAX_MESHES || !s_meshes[h].used)
        return;
    GlMesh *gm = &s_meshes[h];
    p_glDeleteBuffers(1, &gm->vbo);
    if (gm->cbo)
        p_glDeleteBuffers(1, &gm->cbo);
    p_glDeleteBuffers(1, &gm->ibo);
    p_glDeleteVertexArrays(1, &gm->vao);
    memset(gm, 0, sizeof(*gm));
}

GfxTexture gfx_texture_create(const GfxTextureDesc *d)
{
    if (d->levels < 1)
        return 0;
    int slot = 0;
    for (int i = 1; i < GFX_MAX_TEXTURES; i++)
        if (!s_tex[i].used) {
            slot = i;
            break;
        }
    if (!slot)
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
    s_tex[slot].id = tex;
    s_tex[slot].used = true;
    s_tex[slot].bytes = (uint32_t)nh_tex_total_size(d->format, d->width, d->height, d->levels);
    return (GfxTexture)slot;
}

void gfx_texture_destroy(GfxTexture t)
{
    if (!t || t >= GFX_MAX_TEXTURES || !s_tex[t].used)
        return;
    p_glDeleteTextures(1, &s_tex[t].id);
    memset(&s_tex[t], 0, sizeof(s_tex[t]));
}

static GLuint tex_id(GfxTexture t)
{
    return (t && t < GFX_MAX_TEXTURES && s_tex[t].used) ? s_tex[t].id : s_white;
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
    s_nq2d = 0;
    s_nq3d = 0;
    p_glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    p_glViewport(0, 0, GFX_SCREEN_W, GFX_SCREEN_H);
    p_glDepthMask(GL_TRUE);
    p_glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f);
    p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    s_projA.enabled = s_projB.enabled = false;
    s_fog.enabled = false;
    s_mode = GFX_MODE_NORMAL;
    s_ndyn = 0;
}

void gfx_set_camera(const NhMat34 *view, float fovy_deg, float znear, float zfar)
{
    float f = 1.0f / tanf(NH_DEG2RAD(fovy_deg) * 0.5f);
    float aspect = gfx_display_aspect();
    float proj[16] = { f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (zfar + znear) / (znear - zfar),
                       2.0f * zfar * znear / (znear - zfar), 0, 0, -1, 0 };
    memcpy(s_proj, proj, sizeof(proj));
    s_view = *view;
}

void gfx_set_fog(const GfxFog *fog)
{
    if (fog)
        s_fog = *fog;
    else
        s_fog.enabled = false;
}

void gfx_set_lights(const NhLight *lights, int count)
{
    s_ndyn = NH_MIN(count, GFX_MAX_DYN_LIGHTS);
    for (int i = 0; i < s_ndyn; i++)
        s_dyn[i] = lights[i];
}

void gfx_set_projectors(const GfxProjector *torch, const GfxProjector *guard)
{
    s_projA.enabled = false;
    s_projB.enabled = false;
    if (torch && torch->enabled)
        s_projA = *torch;
    if (guard && guard->enabled)
        s_projB = *guard;
}

void gfx_set_mode(GfxMode mode)
{
    s_mode = mode;
}

static void light_to_view(const NhLight *l, float *pos, float *dir, float *col, float *la, float *lk)
{
    NhVec3 p = nh_m34_mul_point(&s_view, l->pos), d = nh_m34_mul_dir(&s_view, l->dir);
    pos[0] = p.x;
    pos[1] = p.y;
    pos[2] = p.z;
    dir[0] = d.x;
    dir[1] = d.y;
    dir[2] = d.z;
    col[0] = l->color.r / 255.0f;
    col[1] = l->color.g / 255.0f;
    col[2] = l->color.b / 255.0f;
    for (int k = 0; k < 3; k++) {
        la[k] = l->a[k];
        lk[k] = l->k[k];
    }
}

/* Texgen matrix for a projector: model position -> (s*q, t*q, q). */
static void projector_texgen(const GfxProjector *pr, const NhMat34 *model, float out[16])
{
    NhMat34 lv, lm, t;
    NhVec3 up = fabsf(pr->dir.y) > 0.95f ? nh_v3(1, 0, 0) : nh_v3(0, 1, 0);
    nh_m34_look_at(&lv, pr->pos, up, nh_v3_add(pr->pos, pr->dir));
    nh_m34_mul(&lm, &lv, model);
    float f = 1.0f / tanf(NH_DEG2RAD(pr->fov_deg) * 0.5f);
    NhMat34 pc = { { { 0.5f * f, 0, -0.5f, 0 }, { 0, -0.5f * f, -0.5f, 0 }, { 0, 0, -1.0f, 0 } } };
    nh_m34_mul(&t, &pc, &lm);
    m34_to_44(&t, out);
}

static void setup_draw(const GfxMaterial *mat, bool skinned, const NhMat34 *model)
{
    p_glUseProgram(P.id);
    p_glUniformMatrix4fv(P.proj, 1, GL_TRUE, s_proj);
    float pos[24], dir[24], col[24], la[24], lk[24];
    memset(pos, 0, sizeof(pos));
    memset(dir, 0, sizeof(dir));
    memset(col, 0, sizeof(col));
    memset(la, 0, sizeof(la));
    memset(lk, 0, sizeof(lk));
    for (int i = 0; i < s_ndyn; i++)
        light_to_view(&s_dyn[i], pos + i * 3, dir + i * 3, col + i * 3, la + i * 3, lk + i * 3);
    bool lit = !(mat->flags & GFX_MAT_UNLIT);
    bool torch = lit && s_projA.enabled && !(mat->flags & GFX_MAT_NOTORCH);
    bool guard = lit && s_projB.enabled && !(mat->flags & GFX_MAT_NOTORCH);
    if (torch)
        light_to_view(&s_projA.light, pos + 18, dir + 18, col + 18, la + 18, lk + 18);
    if (guard)
        light_to_view(&s_projB.light, pos + 21, dir + 21, col + 21, la + 21, lk + 21);
    p_glUniform1i(P.nlights, s_ndyn);
    p_glUniform3fv(P.lpos, 8, pos);
    p_glUniform3fv(P.ldir, 8, dir);
    p_glUniform3fv(P.lcol, 8, col);
    p_glUniform3fv(P.la, 8, la);
    p_glUniform3fv(P.lk, 8, lk);
    p_glUniform1i(P.torch, torch);
    p_glUniform1i(P.guard, guard);
    /* Skinned meshes get the torches per-vertex only: GX texgen from position
     * can't follow per-vertex matrices without extra matrix indices. */
    bool cookies = !skinned;
    p_glUniform1i(P.cookieA, torch && cookies && s_projA.cookie);
    p_glUniform1i(P.cookieB, guard && cookies && s_projB.cookie);
    float tg[16] = { 0 };
    if (torch && cookies)
        projector_texgen(&s_projA, model, tg);
    p_glUniformMatrix4fv(P.tgA, 1, GL_TRUE, tg);
    memset(tg, 0, sizeof(tg));
    if (guard && cookies)
        projector_texgen(&s_projB, model, tg);
    p_glUniformMatrix4fv(P.tgB, 1, GL_TRUE, tg);
    p_glUniform3f(P.gcol, s_projB.light.color.r / 255.0f, s_projB.light.color.g / 255.0f,
                  s_projB.light.color.b / 255.0f);
    p_glUniform4f(P.amb, mat->ambient.r / 255.0f, mat->ambient.g / 255.0f, mat->ambient.b / 255.0f, 1.0f);
    p_glUniform4f(P.tint, mat->tint.r / 255.0f, mat->tint.g / 255.0f, mat->tint.b / 255.0f, mat->tint.a / 255.0f);
    p_glUniform1i(P.baked, (mat->flags & GFX_MAT_BAKED) != 0);
    p_glUniform1i(P.unlit, !lit);
    p_glUniform1i(P.alphatest, (mat->flags & GFX_MAT_ALPHATEST) != 0);
    p_glUniform1i(P.thermal, s_mode == GFX_MODE_THERMAL);
    p_glUniform1i(P.warm, (mat->flags & GFX_MAT_WARM) != 0);
    p_glUniform1i(P.fogon, s_fog.enabled && !(mat->flags & GFX_MAT_NOFOG));
    p_glUniform3f(P.fogcol, s_fog.color.r / 255.0f, s_fog.color.g / 255.0f, s_fog.color.b / 255.0f);
    p_glUniform2f(P.fog, s_fog.start, s_fog.end > s_fog.start ? s_fog.end : s_fog.start + 1.0f);
    p_glUniform1i(P.tex, 0);
    p_glUniform1i(P.cookie, 1);
    p_glActiveTexture(GL_TEXTURE1);
    p_glBindTexture(GL_TEXTURE_2D, tex_id(s_projA.enabled ? s_projA.cookie : s_projB.cookie));
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D, tex_id(mat->tex));

    p_glEnable(GL_DEPTH_TEST);
    p_glDepthFunc(GL_LEQUAL);
    bool blend = (mat->flags & (GFX_MAT_BLEND | GFX_MAT_ADD)) != 0;
    p_glDepthMask(blend ? GL_FALSE : GL_TRUE);
    if (blend) {
        p_glEnable(GL_BLEND);
        if (mat->flags & GFX_MAT_ADD)
            p_glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else
            p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        p_glDisable(GL_BLEND);
    }
    if (mat->flags & GFX_MAT_NOCULL) {
        p_glDisable(GL_CULL_FACE);
    } else {
        p_glEnable(GL_CULL_FACE);
        p_glFrontFace(GL_CCW);
        p_glCullFace(GL_BACK);
    }
}

static void draw_mesh(const GlMesh *gm)
{
    p_glBindVertexArray(gm->vao);
    if (!gm->has_colors)
        p_glVertexAttrib4f(4, 1.0f, 1.0f, 1.0f, 1.0f);
    p_glDrawElements(GL_TRIANGLES, gm->icount, GL_UNSIGNED_SHORT, NULL);
    s_cur.triangles += gm->tris;
    s_cur.draw_calls++;
}

static void upload_bones(const NhMat34 *bones, int n)
{
    float mv[16 * NH_MAX_BONES], nm[9 * NH_MAX_BONES];
    for (int i = 0; i < n; i++) {
        NhMat34 m, nmat;
        nh_m34_mul(&m, &s_view, &bones[i]);
        nh_m34_normal_matrix(&nmat, &m);
        m34_to_44(&m, mv + i * 16);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                nm[i * 9 + r * 3 + c] = nmat.m[r][c];
    }
    p_glUniformMatrix4fv(P.mv, n, GL_TRUE, mv);
    p_glUniformMatrix3fv(P.nm, n, GL_TRUE, nm);
}

void gfx_draw(GfxMesh mesh, const GfxMaterial *mat, const NhMat34 *model)
{
    if (!mesh || mesh >= GFX_MAX_MESHES || !s_meshes[mesh].used)
        return;
    setup_draw(mat, false, model);
    p_glUniform1i(P.skinned, 0);
    upload_bones(model, 1);
    draw_mesh(&s_meshes[mesh]);
}

void gfx_draw_skinned(GfxMesh mesh, const GfxMaterial *mat, const NhMat34 *bones, int nbones)
{
    if (!mesh || mesh >= GFX_MAX_MESHES || !s_meshes[mesh].used || nbones < 1)
        return;
    nbones = NH_MIN(nbones, NH_MAX_BONES);
    setup_draw(mat, true, &bones[0]);
    p_glUniform1i(P.skinned, 1);
    upload_bones(bones, nbones);
    draw_mesh(&s_meshes[mesh]);
}

void gfx_draw_quad3d(GfxTexture tex, const NhVec3 corners[4], float u0, float v0, float u1, float v1, NhColor c,
                     uint16_t flags)
{
    if (s_nq3d >= MAX_Q3D)
        return;
    Quad *q = &s_q3d[s_nq3d++];
    q->tex = tex;
    q->flags = flags;
    float us[4] = { u0, u1, u1, u0 }, vs[4] = { v1, v1, v0, v0 };
    for (int i = 0; i < 4; i++)
        q->v[i] = (QVert){ corners[i].x, corners[i].y, corners[i].z, c.r, c.g, c.b, c.a, us[i], vs[i] };
}

void gfx_draw_quad2d(GfxTexture tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                     NhColor c)
{
    if (s_nq2d >= MAX_Q2D)
        return;
    Quad *q = &s_q2d[s_nq2d++];
    q->tex = tex;
    q->flags = GFX_MAT_BLEND;
    q->v[0] = (QVert){ x, y, 0, c.r, c.g, c.b, c.a, u0, v0 };
    q->v[1] = (QVert){ x + w, y, 0, c.r, c.g, c.b, c.a, u1, v0 };
    q->v[2] = (QVert){ x + w, y + h, 0, c.r, c.g, c.b, c.a, u1, v1 };
    q->v[3] = (QVert){ x, y + h, 0, c.r, c.g, c.b, c.a, u0, v1 };
}

static void mat44_mul(const float *a, const float *b, float *out)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = s;
        }
}

static void flush_quads(Quad *qs, int n, bool is3d)
{
    if (n == 0)
        return;
    p_glUseProgram(s_progq);
    p_glUniform1i(uq_tex, 0);
    float mvp[16], v44[16];
    if (is3d) {
        m34_to_44(&s_view, v44);
        mat44_mul(s_proj, v44, mvp);
        p_glUniform4f(uq_vrow2, s_view.m[2][0], s_view.m[2][1], s_view.m[2][2], s_view.m[2][3]);
        p_glUniform1i(uq_fogon, s_fog.enabled);
        p_glUniform2f(uq_fog, s_fog.start, s_fog.end > s_fog.start ? s_fog.end : s_fog.start + 1.0f);
        p_glEnable(GL_DEPTH_TEST);
        p_glDepthFunc(GL_LEQUAL);
    } else {
        float o[16] = { 2.0f / GFX_SCREEN_W, 0, 0, -1, 0, -2.0f / GFX_SCREEN_H, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1 };
        memcpy(mvp, o, sizeof(o));
        p_glUniform4f(uq_vrow2, 0, 0, 0, 0);
        p_glUniform1i(uq_fogon, 0);
        p_glDisable(GL_DEPTH_TEST);
    }
    p_glUniformMatrix4fv(uq_mvp, 1, GL_TRUE, mvp);
    p_glDepthMask(GL_FALSE);
    p_glDisable(GL_CULL_FACE);
    p_glEnable(GL_BLEND);
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindVertexArray(s_vaoq);
    p_glBindBuffer(GL_ARRAY_BUFFER, s_vboq);

    static const int order[6] = { 0, 1, 2, 0, 2, 3 };
    int i = 0;
    while (i < n) {
        GfxTexture tex = qs[i].tex;
        uint16_t fl = qs[i].flags & GFX_MAT_ADD;
        int cnt = 0;
        while (i < n && qs[i].tex == tex && (qs[i].flags & GFX_MAT_ADD) == fl && cnt < MAX_Q2D) {
            for (int k = 0; k < 6; k++)
                s_qscratch[cnt * 6 + k] = qs[i].v[order[k]];
            cnt++;
            i++;
        }
        if (fl)
            p_glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else
            p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        p_glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(QVert) * 6 * (size_t)cnt), s_qscratch);
        p_glBindTexture(GL_TEXTURE_2D, tex_id(tex));
        p_glDrawArrays(GL_TRIANGLES, 0, cnt * 6);
        s_cur.draw_calls++;
        s_cur.triangles += (uint32_t)cnt * 2;
    }
    p_glBindVertexArray(0);
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

void desktop_gfx_screenshot(const char *path)
{
    save_screenshot(path);
}

void gfx_end_frame(void)
{
    flush_quads(s_q3d, s_nq3d, true);
    flush_quads(s_q2d, s_nq2d, false);
    s_nq3d = s_nq2d = 0;
    uint64_t t0 = plat_time_us();
    s_cur.cpu_us = (uint32_t)(t0 - s_frame_start);
    for (int i = 1; i < GFX_MAX_MESHES; i++)
        if (s_meshes[i].used)
            s_cur.mesh_bytes += s_meshes[i].bytes;
    for (int i = 1; i < GFX_MAX_TEXTURES; i++)
        if (s_tex[i].used)
            s_cur.tex_bytes += s_tex[i].bytes;

    s_frames++;
    if (g_desktop_opts.frames > 0 && s_frames >= (uint64_t)g_desktop_opts.frames) {
        if (g_desktop_opts.screenshot)
            save_screenshot(g_desktop_opts.screenshot);
        plat_request_quit();
    }
    if (g_desktop_opts.shot_every > 0 && g_desktop_opts.screenshot && s_frames % (uint64_t)g_desktop_opts.shot_every == 0) {
        char path[512];
        snprintf(path, sizeof(path), "%s.%05u.bmp", g_desktop_opts.screenshot, (unsigned)s_frames);
        save_screenshot(path);
    }

    /* Scale the 640x480 image into the window, letterboxed. */
    int vx, vy, vw, vh, ww, wh, sw, sh;
    desktop_gfx_viewport(&vx, &vy, &vw, &vh);
    SDL_GL_GetDrawableSize(g_window, &ww, &wh);
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
