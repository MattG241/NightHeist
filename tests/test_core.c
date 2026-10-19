/* Unit tests for src/core: maths, RNG, arena, timestep, lighting, textures,
 * procedural meshes and the font. */
#include <stdlib.h>
#include <string.h>

#include "core/nh_arena.h"
#include "core/nh_fixedstep.h"
#include "core/nh_font.h"
#include "core/nh_light.h"
#include "core/nh_math.h"
#include "core/nh_meshgen.h"
#include "core/nh_rng.h"
#include "core/nh_texconv.h"
#include "nh_test.h"

static _Alignas(32) uint8_t s_mem[1 << 20];

void test_math(void)
{
    NhMat34 a, b, m, inv, id;
    nh_m34_rot_y(&a, 0.7f);
    nh_m34_translate(&b, 1.0f, -2.0f, 3.0f);
    nh_m34_mul(&m, &b, &a);
    CHECK(nh_m34_inverse(&inv, &m));
    nh_m34_mul(&id, &m, &inv);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            CHECK_NEAR(id.m[r][c], (r == c) ? 1.0 : 0.0, 1e-5);

    /* look_at: the target ends up straight ahead on -Z, eye at the origin. */
    NhMat34 view;
    NhVec3 eye = nh_v3(3, 2, 5), target = nh_v3(-1, 1, -4);
    nh_m34_look_at(&view, eye, nh_v3(0, 1, 0), target);
    NhVec3 e = nh_m34_mul_point(&view, eye);
    NhVec3 t = nh_m34_mul_point(&view, target);
    CHECK_NEAR(nh_v3_len(e), 0.0, 1e-5);
    CHECK_NEAR(t.x, 0.0, 1e-4);
    CHECK_NEAR(t.y, 0.0, 1e-4);
    CHECK(t.z < 0.0f);
    CHECK_NEAR(t.z, -nh_v3_len(nh_v3_sub(target, eye)), 1e-4);

    /* Normal matrix of a non-uniform scale keeps normals perpendicular. */
    NhMat34 s, nm;
    nh_m34_scale(&s, 2.0f, 1.0f, 1.0f);
    CHECK(nh_m34_normal_matrix(&nm, &s));
    NhVec3 n = nh_m34_mul_dir(&nm, nh_v3_norm(nh_v3(1, 1, 0)));
    NhVec3 tangent = nh_m34_mul_dir(&s, nh_v3(1, -1, 0));
    CHECK_NEAR(nh_v3_dot(n, tangent), 0.0, 1e-5);

    CHECK_NEAR(nh_wrap_angle(3.0f * NH_PI), -NH_PI, 1e-5);
    CHECK_NEAR(nh_wrap_angle(0.5f), 0.5, 1e-6);
}

void test_rng(void)
{
    /* Reference output of the PCG32 demo: pcg32_srandom_r(42, 54). */
    static const uint32_t expected[6] = { 0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
                                          0x83d2f293u, 0xbfa4784bu, 0xcbed606eu };
    NhRng r;
    nh_rng_seed(&r, 42u, 54u);
    for (int i = 0; i < 6; i++)
        CHECK(nh_rng_u32(&r) == expected[i]);

    NhRng a, b;
    nh_rng_seed(&a, 1234u, 1u);
    nh_rng_seed(&b, 1234u, 1u);
    int counts[10] = { 0 };
    bool same = true;
    for (int i = 0; i < 10000; i++) {
        uint32_t v = nh_rng_range(&a, 10);
        same = same && v == nh_rng_range(&b, 10);
        CHECK(v < 10);
        counts[v]++;
        float f = nh_rng_float(&a);
        (void)nh_rng_float(&b);
        CHECK(f >= 0.0f && f < 1.0f);
    }
    CHECK(same);
    for (int i = 0; i < 10; i++)
        CHECK(counts[i] > 850 && counts[i] < 1150);
}

void test_arena(void)
{
    NhArena a;
    nh_arena_init(&a, s_mem, 1024);
    void *p1 = nh_arena_alloc(&a, 3);
    void *p2 = nh_arena_alloc(&a, 5);
    CHECK(p1 && p2);
    CHECK(((uintptr_t)p1 % NH_ARENA_ALIGN) == 0);
    CHECK(((uintptr_t)p2 % NH_ARENA_ALIGN) == 0);
    size_t mark = nh_arena_mark(&a);
    CHECK(nh_arena_alloc(&a, 2000) == NULL);
    CHECK(nh_arena_mark(&a) == mark); /* failed alloc changes nothing */
    nh_arena_reset_to(&a, mark);
    CHECK(nh_arena_alloc(&a, 1024 - 64) != NULL);
    CHECK(nh_arena_alloc(&a, 1) == NULL);
    nh_arena_reset(&a);
    CHECK(a.used == 0 && a.peak >= 1024 - 64);
}

void test_fixedstep(void)
{
    NhFixedStep fs;

    /* 60 Hz display: exactly 60 ticks per second over a long run. */
    nh_fixedstep_init(&fs);
    int ticks = 0;
    for (int i = 0; i < 6000; i++)
        ticks += nh_fixedstep_advance(&fs, i % 3 == 2 ? 16668 : 16666); /* 50000 us per 3 frames */
    CHECK(ticks == 6000);

    /* 50 Hz PAL display: still 60 ticks per second. */
    nh_fixedstep_init(&fs);
    ticks = 0;
    for (int i = 0; i < 500; i++)
        ticks += nh_fixedstep_advance(&fs, 20000);
    CHECK(ticks == 600);
    CHECK(nh_fixedstep_alpha(&fs) >= 0.0f && nh_fixedstep_alpha(&fs) < 1.0f);

    /* A long stall is capped instead of spiralling. */
    nh_fixedstep_init(&fs);
    CHECK(nh_fixedstep_advance(&fs, 5000000) == NH_MAX_TICKS_PER_FRAME);
    CHECK(nh_fixedstep_alpha(&fs) == 0.0f);
}

void test_light(void)
{
    NhLight l;
    nh_light_init(&l, nh_v3(0, 0, 0), nh_rgba(255, 255, 255, 255));
    nh_light_set_spot(&l, nh_v3(0, 0, -1), 30.0f, NH_SPOT_COS2);
    nh_light_set_dist(&l, 5.0f, 0.5f, NH_DIST_MEDIUM);

    /* Distance attenuation hits ref_bright at ref_dist on the axis. */
    NhVec3 facing = nh_v3(0, 0, 1);
    CHECK_NEAR(nh_light_factor(&l, nh_v3(0, 0, -5), facing), 0.5, 1e-5);
    CHECK_NEAR(nh_light_factor(&l, nh_v3(0, 0, -1e-3f), facing), 1.0, 1e-3);

    /* Spot falls to zero at the cutoff and stays zero outside it. */
    float c = cosf(NH_DEG2RAD(30.0f)), s = sinf(NH_DEG2RAD(30.0f));
    CHECK_NEAR(nh_light_factor(&l, nh_v3(s * 2, 0, -c * 2), nh_v3(0, 0, 0)), 0.0, 1e-5);
    CHECK(nh_light_factor(&l, nh_v3(1.0f, 0, -0.2f), nh_v3(0, 0, 0)) == 0.0f);
    CHECK(nh_light_factor(&l, nh_v3(0, 0, 2), nh_v3(0, 0, 0)) == 0.0f); /* behind */

    /* Back-facing surfaces receive nothing (GX_DF_CLAMP). */
    CHECK(nh_light_factor(&l, nh_v3(0, 0, -2), nh_v3(0, 0, -1)) == 0.0f);

    /* Spot coefficients match GX_InitLightSpot's COS2 formula. */
    float cr = cosf(NH_DEG2RAD(30.0f));
    CHECK_NEAR(l.a[0], 0.0, 1e-6);
    CHECK_NEAR(l.a[1], -cr / (1.0f - cr), 1e-4);
    CHECK_NEAR(l.a[2], 1.0f / (1.0f - cr), 1e-4);

    /* Out-of-range parameters turn attenuation off, as libogc does. */
    nh_light_set_spot(&l, nh_v3(0, 0, -1), 120.0f, NH_SPOT_COS2);
    nh_light_set_dist(&l, 5.0f, 1.5f, NH_DIST_STEEP);
    CHECK(l.a[0] == 1.0f && l.a[1] == 0.0f && l.a[2] == 0.0f);
    CHECK(l.k[0] == 1.0f && l.k[1] == 0.0f && l.k[2] == 0.0f);
}

static void fill_pattern(uint8_t *rgba, int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t *p = rgba + (y * w + x) * 4;
            p[0] = (uint8_t)(x * 37 + y * 11);
            p[1] = (uint8_t)(x * 5 + y * 71);
            p[2] = (uint8_t)(x * y);
            p[3] = (uint8_t)(255 - x * 3);
        }
}

void test_texconv(void)
{
    /* Sizes follow GX block layouts. */
    CHECK(nh_tex_level_size(NH_TF_RGB565, 128, 128) == 128 * 128 * 2);
    CHECK(nh_tex_level_size(NH_TF_RGBA8, 64, 64) == 64 * 64 * 4);
    CHECK(nh_tex_level_size(NH_TF_I8, 8, 4) == 32);
    CHECK(nh_tex_level_size(NH_TF_I8, 1, 1) == 32);  /* padded to one block */
    CHECK(nh_tex_level_size(NH_TF_RGB565, 2, 2) == 32);
    CHECK(nh_tex_max_levels(128, 32) == 8);
    CHECK(nh_tex_total_size(NH_TF_RGB565, 8, 8, 4) == 128 + 32 + 32 + 32);

    enum { W = 16, H = 12 };
    static uint8_t src[W * H * 4], tiled[W * H * 4 + 256], back[W * H * 4];
    fill_pattern(src, W, H);

    /* RGBA8 is lossless. */
    nh_tex_encode(NH_TF_RGBA8, src, W, H, tiled);
    nh_tex_decode(NH_TF_RGBA8, tiled, W, H, back);
    CHECK(memcmp(src, back, sizeof(src)) == 0);

    /* RGB565 loses at most the low bits; alpha becomes opaque. */
    nh_tex_encode(NH_TF_RGB565, src, W, H, tiled);
    nh_tex_decode(NH_TF_RGB565, tiled, W, H, back);
    int worst = 0;
    for (int i = 0; i < W * H; i++) {
        for (int c = 0; c < 3; c++) {
            int d = abs(src[i * 4 + c] - back[i * 4 + c]);
            worst = d > worst ? d : worst;
        }
        CHECK(back[i * 4 + 3] == 255);
    }
    CHECK(worst <= 4);

    /* IA8 keeps alpha exactly; first stored byte is alpha (GX order). */
    nh_tex_encode(NH_TF_IA8, src, W, H, tiled);
    CHECK(tiled[0] == src[3]);
    nh_tex_decode(NH_TF_IA8, tiled, W, H, back);
    for (int i = 0; i < W * H; i++)
        CHECK(back[i * 4 + 3] == src[i * 4 + 3]);

    /* Mip chain writes exactly nh_tex_total_size bytes. */
    static uint8_t big[64 * 64 * 4], scratch[64 * 64 * 2], out[64 * 64 * 3];
    memset(out, 0xAB, sizeof(out));
    fill_pattern(big, 64, 64);
    int levels = nh_tex_max_levels(64, 64);
    size_t total = nh_tex_total_size(NH_TF_RGB565, 64, 64, levels);
    CHECK(total < sizeof(out));
    nh_tex_encode_mips(NH_TF_RGB565, big, 64, 64, levels, out, scratch);
    CHECK(out[total] == 0xAB); /* nothing written past the end */
}

/* Every triangle's geometric normal must agree with its vertex normals, i.e.
 * CCW winding seen from the outside (the convention both backends cull by). */
static void check_winding(const NhMeshData *m)
{
    int bad = 0;
    for (uint32_t t = 0; t < m->icount; t += 3) {
        NhVec3 a = m->pos[m->idx[t]], b = m->pos[m->idx[t + 1]], c = m->pos[m->idx[t + 2]];
        NhVec3 fn = nh_v3_cross(nh_v3_sub(b, a), nh_v3_sub(c, a));
        NhVec3 vn = nh_v3_add(nh_v3_add(m->nrm[m->idx[t]], m->nrm[m->idx[t + 1]]), m->nrm[m->idx[t + 2]]);
        if (nh_v3_dot(fn, vn) <= 0.0f)
            bad++;
    }
    CHECK(bad == 0);
}

void test_meshgen(void)
{
    NhArena a;
    nh_arena_init(&a, s_mem, sizeof(s_mem));
    NhMeshData m;
    CHECK(nh_mesh_torus(&m, &a, 0.8f, 0.3f, 48, 24, 4.0f, 1.0f));
    CHECK(m.vcount == 49 * 25 && m.icount == 48 * 24 * 6);
    check_winding(&m);
    for (uint32_t i = 0; i < m.icount; i++)
        CHECK(m.idx[i] < m.vcount);

    nh_arena_reset(&a);
    CHECK(nh_mesh_grid(&m, &a, 20.0f, 6.0f, 40, 12, 0.3f));
    CHECK(m.vcount == 41 * 13 && m.icount == 40 * 12 * 6);
    check_winding(&m);

    nh_arena_reset(&a);
    CHECK(!nh_mesh_grid(&m, &a, 1.0f, 1.0f, 300, 300, 1.0f)); /* > 65535 vertices */
}

void test_font(void)
{
    for (int g = 0; g < NH_FONT_GLYPHS; g++) {
        int bits = 0;
        for (int r = 0; r < 7; r++) {
            CHECK((nh_font5x7[g][r] & ~0x1f) == 0);
            bits += nh_font5x7[g][r] != 0;
        }
        if (g == 0)
            CHECK(bits == 0); /* space */
        else
            CHECK(bits > 0);
    }
    float u0, v0, u1, v1;
    nh_font_glyph_uv('A', &u0, &v0, &u1, &v1);
    CHECK(u0 < u1 && v0 < v1 && u1 <= 1.0f && v1 <= 1.0f);
    nh_font_glyph_uv(200, &u0, &v0, &u1, &v1);
    float qu0, qv0, qu1, qv1;
    nh_font_glyph_uv('?', &qu0, &qv0, &qu1, &qv1);
    CHECK(u0 == qu0 && v0 == qv0);
}
