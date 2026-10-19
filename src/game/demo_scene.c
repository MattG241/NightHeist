#include "demo_scene.h"

#include <math.h>

#include "core/nh_texgen.h"
#include "platform/gfx.h"
#include "text.h"

#define FOVY_DEG 60.0f
#define ROOM_HALF 10.0f
#define WALL_H 6.0f

static GfxMesh s_torus, s_floor, s_wall;
static GfxTexture s_bricks, s_concrete;

#define CLICK_SAMPLES 1280 /* 40 ms at 32 kHz; multiple of 16 for 32-byte DMA */
static _Alignas(32) int16_t s_click_pcm[CLICK_SAMPLES];
static NhSound s_click = { s_click_pcm, CLICK_SAMPLES, 32000 };

static GfxTexture make_texture(NhArena *scratch, int size, void (*gen)(uint8_t *, int, int, uint32_t),
                               uint32_t seed)
{
    size_t mark = nh_arena_mark(scratch);
    int levels = nh_tex_max_levels(size, size);
    uint8_t *rgba = nh_arena_alloc(scratch, (size_t)size * (size_t)size * 4);
    uint8_t *mip_scratch = nh_arena_alloc(scratch, (size_t)size * (size_t)size * 2);
    uint8_t *tiled = nh_arena_alloc(scratch, nh_tex_total_size(NH_TF_RGB565, size, size, levels));
    GfxTexture tex = 0;
    if (rgba && mip_scratch && tiled) {
        gen(rgba, size, size, seed);
        nh_tex_encode_mips(NH_TF_RGB565, rgba, size, size, levels, tiled, mip_scratch);
        GfxTextureDesc desc = { NH_TF_RGB565, (uint16_t)size, (uint16_t)size, (uint8_t)levels, true, true, tiled };
        tex = gfx_texture_create(&desc);
    }
    nh_arena_reset_to(scratch, mark);
    return tex;
}

static void make_click(void)
{
    /* A short dry tick: a decaying 1.8 kHz tone plus a little noise. */
    uint32_t noise = 0x1234567u;
    for (int i = 0; i < CLICK_SAMPLES; i++) {
        float t = (float)i / 32000.0f;
        float env = expf(-t * 140.0f);
        noise = noise * 1664525u + 1013904223u;
        float n = ((float)(noise >> 16) / 32768.0f - 1.0f) * 0.35f;
        float v = (sinf(2.0f * NH_PI * 1800.0f * t) * 0.65f + n) * env;
        s_click_pcm[i] = (int16_t)(v * 26000.0f);
    }
}

bool demo_scene_load(NhArena *scratch)
{
    NhMeshData m;
    size_t mark = nh_arena_mark(scratch);
    if (!nh_mesh_torus(&m, scratch, 0.8f, 0.3f, 48, 24, 4.0f, 1.0f))
        return false;
    s_torus = gfx_mesh_create(&m);
    nh_arena_reset_to(scratch, mark);

    if (!nh_mesh_grid(&m, scratch, ROOM_HALF * 2.0f, ROOM_HALF * 2.0f, 40, 40, 0.25f))
        return false;
    s_floor = gfx_mesh_create(&m);
    nh_arena_reset_to(scratch, mark);

    if (!nh_mesh_grid(&m, scratch, ROOM_HALF * 2.0f, WALL_H, 40, 12, 0.3f))
        return false;
    s_wall = gfx_mesh_create(&m);
    nh_arena_reset_to(scratch, mark);

    s_bricks = make_texture(scratch, 128, nh_texgen_bricks, 7);
    s_concrete = make_texture(scratch, 128, nh_texgen_concrete, 11);
    make_click();
    return s_torus && s_floor && s_wall && s_bricks && s_concrete;
}

void demo_scene_handle_events(const DemoSim *sim, uint32_t events)
{
    if (events & DEMO_EV_CLICK)
        plat_audio_play(&s_click, 0.8f, sim->click_pan);
}

/* World-space direction through a screen pixel for a camera with this view. */
static NhVec3 screen_ray(const NhMat34 *view, float sx, float sy, float aspect)
{
    float t = tanf(NH_DEG2RAD(FOVY_DEG) * 0.5f);
    float vx = (sx / (GFX_SCREEN_W * 0.5f) - 1.0f) * t * aspect;
    float vy = (1.0f - sy / (GFX_SCREEN_H * 0.5f)) * t;
    float vz = -1.0f;
    /* Inverse of the view rotation is its transpose. */
    NhVec3 d = nh_v3(view->m[0][0] * vx + view->m[1][0] * vy + view->m[2][0] * vz,
                     view->m[0][1] * vx + view->m[1][1] * vy + view->m[2][1] * vz,
                     view->m[0][2] * vx + view->m[1][2] * vy + view->m[2][2] * vz);
    return nh_v3_norm(d);
}

static void draw_room(void)
{
    const NhColor white = nh_rgba(255, 255, 255, 255);
    NhMat34 m, r, t;

    nh_m34_identity(&m);
    gfx_draw_mesh(s_floor, s_concrete, &m, white);

    static const float yaw[4] = { 0.0f, 90.0f, -90.0f, 180.0f }; /* back, left, right, front */
    static const float px[4] = { 0.0f, -ROOM_HALF, ROOM_HALF, 0.0f };
    static const float pz[4] = { -ROOM_HALF, 0.0f, 0.0f, ROOM_HALF };
    for (int i = 0; i < 4; i++) {
        NhMat34 rx;
        nh_m34_rot_x(&rx, NH_DEG2RAD(90.0f)); /* XZ grid -> XY wall facing +Z */
        nh_m34_rot_y(&r, NH_DEG2RAD(yaw[i]));
        nh_m34_translate(&t, px[i], WALL_H * 0.5f, pz[i]);
        nh_m34_mul(&m, &r, &rx);
        nh_m34_mul(&m, &t, &m);
        gfx_draw_mesh(s_wall, s_bricks, &m, white);
    }
}

static void draw_tori(const DemoSim *sim, float alpha)
{
    int n = demo_sim_instances(sim);
    int side = (int)(sqrtf((float)n) + 0.5f);
    float spacing = 2.2f;
    float spin = sim->prev_spin + nh_wrap_angle(sim->spin - sim->prev_spin) * alpha;
    for (int i = 0; i < n; i++) {
        float gx = ((float)(i % side) - (float)(side - 1) * 0.5f) * spacing;
        float gz = ((float)(i / side) - (float)(side - 1) * 0.5f) * spacing - 2.5f;
        NhMat34 m, r1, r2, t;
        nh_m34_rot_x(&r1, NH_DEG2RAD(60.0f) + spin * 0.5f);
        nh_m34_rot_y(&r2, spin + (float)i * 0.3f);
        nh_m34_translate(&t, gx, 1.3f, gz);
        nh_m34_mul(&m, &r2, &r1);
        nh_m34_mul(&m, &t, &m);
        gfx_draw_mesh(s_torus, s_concrete, &m, nh_rgba(255, 250, 240, 255));
    }
}

static const char *btn_names[] = { "A", "B", "1", "2", "+", "-", "HOME", "UP", "DOWN", "LEFT", "RIGHT", "Z", "C" };

static void draw_hud(const DemoSim *sim, const DemoFrameInfo *info)
{
    const NhInput *in = info->input;
    const NhColor fg = nh_rgba(230, 230, 230, 255);
    const NhColor dim = nh_rgba(150, 150, 160, 255);
    const NhColor hi = nh_rgba(255, 200, 90, 255);
    GfxStats st = gfx_last_stats();
    NhMemStats mem;
    plat_mem_stats(&mem);

    float x = 16, y = 16, lh = 18;
    text_rect(8, 8, 624, 8 * lh + 12, nh_rgba(0, 0, 0, 150));
    text_printf(x, y, 2, hi, "NIGHT HEIST  phase 0  [%s]", plat_name());
    y += lh + 4;
    text_printf(x, y, 2, fg, "%5.1f fps  cpu %4.1f ms  gpu wait %4.1f ms", info->fps, st.cpu_us / 1000.0f,
                st.gpu_wait_us / 1000.0f);
    y += lh;
    text_printf(x, y, 2, fg, "tris %6u  draws %3u  tori %2d (1/2)", (unsigned)st.triangles,
                (unsigned)st.draw_calls, demo_sim_instances(sim));
    y += lh;
    if (!in->connected)
        text_draw(x, y, 2, hi, "REMOTE not connected - press 1+2 or sync");
    else if (in->ir_valid)
        text_printf(x, y, 2, fg, "IR  %4.0f,%4.0f  roll %+4.0f deg", in->ir_x, in->ir_y,
                    NH_RAD2DEG(in->remote_roll));
    else
        text_printf(x, y, 2, dim, "IR  lost - torch holds  roll %+4.0f deg", NH_RAD2DEG(in->remote_roll));
    y += lh;
    if (in->nunchuk)
        text_printf(x, y, 2, fg, "NUN stick %+.2f %+.2f  tilt %+4.0f deg", in->stick_x, in->stick_y,
                    NH_RAD2DEG(in->nunchuk_roll));
    else
        text_printf(x, y, 2, dim, "NUN not connected (expansion %d)", in->expansion);
    y += lh;

    char held[96];
    int len = 0;
    held[0] = 0;
    for (int i = 0; i < (int)NH_ARRAY_LEN(btn_names); i++) {
        if ((in->held & (1u << i)) && len < (int)sizeof(held) - 8) {
            const char *s = btn_names[i];
            while (*s)
                held[len++] = *s++;
            held[len++] = ' ';
            held[len] = 0;
        }
    }
    text_printf(x, y, 2, fg, "BTN %s", len ? held : "-");
    y += lh;
    text_printf(x, y, 2, fg, "torch %s  crouch %.2f  lean %+.2f  clicks %u", sim->torch_on ? "on " : "off",
                sim->crouch, sim->lean, (unsigned)sim->clicks);
    y += lh;
    text_printf(x, y, 2, info->storage_ok ? fg : dim, "SD %s  meta %ldB  heap %.1fM  M1 %.1fM M2 %.1fM",
                info->storage_ok ? "ok" : "--", info->meta_bytes, mem.heap_used / 1048576.0f,
                mem.mem1_free / 1048576.0f, mem.mem2_free / 1048576.0f);

    text_draw(16, 428, 2, dim, "A click+rumble  B torch  Z crouch  C spin");
    text_draw(16, 448, 2, dim, "+ pause  1/2 load  tilt lean  HOME quit");

    /* Pointer cursor: bright when tracking, grey where the torch is holding. */
    NhColor cc = in->ir_valid ? nh_rgba(255, 255, 255, 220) : nh_rgba(140, 140, 140, 160);
    float cx = in->ir_valid ? in->ir_x : sim->aim_x;
    float cy = in->ir_valid ? in->ir_y : sim->aim_y;
    text_rect(cx - 9, cy - 1, 7, 2, cc);
    text_rect(cx + 3, cy - 1, 7, 2, cc);
    text_rect(cx - 1, cy - 9, 2, 7, cc);
    text_rect(cx - 1, cy + 3, 2, 7, cc);
}

void demo_scene_render(const DemoSim *sim, const DemoFrameInfo *info)
{
    float alpha = info->alpha;
    float aspect = gfx_display_aspect();

    gfx_begin_frame(nh_rgba(4, 4, 8, 255));

    /* Camera: eye on the interpolated position, lean shifts and rolls it. */
    NhVec3 base = nh_v3_lerp(sim->prev_pos, sim->pos, alpha);
    float roll = sim->lean * NH_DEG2RAD(12.0f);
    NhVec3 eye = nh_v3(base.x + sim->lean * 0.35f, demo_sim_eye_height(sim->crouch), base.z);
    NhVec3 up = nh_v3(sinf(roll), cosf(roll), 0.0f);
    NhMat34 view;
    nh_m34_look_at(&view, eye, up, nh_v3_add(eye, nh_v3(0.0f, 0.0f, -1.0f)));
    gfx_set_camera(&view, FOVY_DEG, 0.1f, 60.0f);

    /* Lights: a dim cold fill so the room never reads as pure black, plus the
     * torch as a GX hardware spotlight aimed through the pointer. */
    NhLight lights[2];
    int nl = 0;
    nh_light_init(&lights[nl], nh_v3(0.0f, 5.5f, -6.0f), nh_rgba(46, 52, 80, 255));
    nh_light_set_dist(&lights[nl], 8.0f, 0.5f, NH_DIST_GENTLE);
    nl++;
    if (sim->torch_on) {
        NhVec3 right = nh_v3(view.m[0][0], view.m[0][1], view.m[0][2]);
        NhVec3 vup = nh_v3(view.m[1][0], view.m[1][1], view.m[1][2]);
        NhVec3 hand = nh_v3_add(eye, nh_v3_add(nh_v3_scale(right, 0.18f), nh_v3_scale(vup, -0.15f)));
        NhVec3 target = nh_v3_add(eye, nh_v3_scale(screen_ray(&view, sim->aim_x, sim->aim_y, aspect), 6.0f));
        nh_light_init(&lights[nl], hand, nh_rgba(255, 238, 205, 255));
        nh_light_set_spot(&lights[nl], nh_v3_sub(target, hand), 24.0f, NH_SPOT_COS2);
        nh_light_set_dist(&lights[nl], 9.0f, 0.6f, NH_DIST_MEDIUM);
        nl++;
    }
    gfx_set_lights(lights, nl, nh_rgba(14, 15, 22, 255));

    draw_room();
    draw_tori(sim, alpha);
    draw_hud(sim, info);

    gfx_end_frame();
}
