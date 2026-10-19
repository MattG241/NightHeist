/* Night Heist - desktop entry point, lifecycle, input, files and timing.
 *
 * Mouse + keyboard stand in for the Wii Remote + Nunchuk:
 *   mouse = pointer, LMB = A, RMB = B, WASD = stick, Q/E = Nunchuk tilt (lean),
 *   Ctrl = Z, F = C, 1/2 = 1/2, Enter = +, Tab = -, arrows = D-pad,
 *   [ ] = twist the remote (roll), Esc = HOME. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/nh_math.h"
#include "desktop_internal.h"
#include "game/app.h"
#include "platform/gfx.h"
#include "platform/platform.h"

DesktopOptions g_desktop_opts = { 0, NULL, false, false, true, "apps/NightHeist" };
SDL_Window *g_window;

static bool s_quit;
static uint32_t s_prev_held;
static bool s_rumble;

static void usage(void)
{
    puts("nightheist [--frames N] [--screenshot out.bmp] [--autotest] [--wide] [--novsync] [--data DIR]");
}

static bool parse_args(const NhPlatformArgs *args)
{
    for (int i = 1; i < args->argc; i++) {
        const char *a = args->argv[i];
        bool has_next = i + 1 < args->argc;
        if (!strcmp(a, "--frames") && has_next)
            g_desktop_opts.frames = atoi(args->argv[++i]);
        else if (!strcmp(a, "--screenshot") && has_next)
            g_desktop_opts.screenshot = args->argv[++i];
        else if (!strcmp(a, "--data") && has_next)
            g_desktop_opts.data_dir = args->argv[++i];
        else if (!strcmp(a, "--autotest"))
            g_desktop_opts.autotest = true;
        else if (!strcmp(a, "--wide"))
            g_desktop_opts.wide = true;
        else if (!strcmp(a, "--novsync"))
            g_desktop_opts.vsync = false;
        else {
            usage();
            return false;
        }
    }
    return true;
}

bool plat_init(const NhPlatformArgs *args)
{
    if (!parse_args(args))
        return false;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!desktop_gfx_init())
        return false;
    if (!desktop_audio_init())
        fprintf(stderr, "audio unavailable, continuing silently\n");
    return true;
}

void plat_shutdown(void)
{
    desktop_audio_shutdown();
    desktop_gfx_shutdown();
    SDL_Quit();
}

bool plat_should_quit(void)
{
    return s_quit;
}

void plat_request_quit(void)
{
    s_quit = true;
}

const char *plat_name(void)
{
    return "Desktop";
}

uint64_t plat_time_us(void)
{
    static uint64_t freq;
    if (!freq)
        freq = SDL_GetPerformanceFrequency();
    uint64_t c = SDL_GetPerformanceCounter();
    return (c / freq) * 1000000u + (c % freq) * 1000000u / freq;
}

void plat_mem_stats(NhMemStats *out)
{
    *out = (NhMemStats){ 0 };
}

/* ---- input --------------------------------------------------------------- */

/* Scripted input for --autotest: exercises pointer, stick, buttons and lean
 * deterministically by frame number so screenshots are reproducible. */
static void autotest_input(NhInput *out, uint32_t *held)
{
    uint64_t f = desktop_frame_count();
    float t = (float)f / 60.0f;
    out->ir_valid = (f / 90) % 4 != 3; /* lose IR for a while to test the hold */
    out->ir_x = 320.0f + 150.0f * cosf(t * 1.3f);
    out->ir_y = 260.0f + 60.0f * sinf(t * 1.7f);
    out->stick_y = (f > 30 && f < 70) ? -1.0f : 0.0f;
    out->nunchuk_roll = NH_DEG2RAD(20.0f * sinf(t));
    if (f % 45 == 10)
        *held |= NH_BTN_A;
    if (f >= 100 && f < 110)
        *held |= NH_BTN_Z;
}

void plat_input_poll(NhInput *out)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT)
            s_quit = true;
    }

    *out = (NhInput){ 0 };
    out->connected = true;
    out->nunchuk = true;
    uint32_t held = 0;

    if (g_desktop_opts.autotest) {
        autotest_input(out, &held);
    } else {
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        int mx, my;
        Uint32 mb = SDL_GetMouseState(&mx, &my);
        int vx, vy, vw, vh;
        desktop_gfx_viewport(&vx, &vy, &vw, &vh);
        bool focused = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
        if (focused && mx >= vx && my >= vy && mx < vx + vw && my < vy + vh) {
            out->ir_valid = true;
            out->ir_x = (float)(mx - vx) * GFX_SCREEN_W / (float)vw;
            out->ir_y = (float)(my - vy) * GFX_SCREEN_H / (float)vh;
        }
        if (mb & SDL_BUTTON(SDL_BUTTON_LEFT))
            held |= NH_BTN_A;
        if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT))
            held |= NH_BTN_B;

        static const struct {
            SDL_Scancode sc;
            uint32_t btn;
        } map[] = {
            { SDL_SCANCODE_1, NH_BTN_1 },       { SDL_SCANCODE_2, NH_BTN_2 },
            { SDL_SCANCODE_RETURN, NH_BTN_PLUS }, { SDL_SCANCODE_TAB, NH_BTN_MINUS },
            { SDL_SCANCODE_ESCAPE, NH_BTN_HOME }, { SDL_SCANCODE_UP, NH_BTN_UP },
            { SDL_SCANCODE_DOWN, NH_BTN_DOWN }, { SDL_SCANCODE_LEFT, NH_BTN_LEFT },
            { SDL_SCANCODE_RIGHT, NH_BTN_RIGHT }, { SDL_SCANCODE_LCTRL, NH_BTN_Z },
            { SDL_SCANCODE_RCTRL, NH_BTN_Z },   { SDL_SCANCODE_F, NH_BTN_C },
        };
        for (size_t i = 0; i < NH_ARRAY_LEN(map); i++)
            if (k[map[i].sc])
                held |= map[i].btn;

        float sx = (float)(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]);
        float sy = (float)(k[SDL_SCANCODE_W] - k[SDL_SCANCODE_S]);
        if (sx != 0.0f && sy != 0.0f) {
            sx *= 0.7071f;
            sy *= 0.7071f;
        }
        out->stick_x = sx;
        out->stick_y = sy;
        out->nunchuk_roll = NH_DEG2RAD(30.0f) * (float)(k[SDL_SCANCODE_E] - k[SDL_SCANCODE_Q]);
        out->remote_roll = NH_DEG2RAD(45.0f) * (float)(k[SDL_SCANCODE_RIGHTBRACKET] - k[SDL_SCANCODE_LEFTBRACKET]);
    }

    out->held = held;
    out->pressed = held & ~s_prev_held;
    out->released = s_prev_held & ~held;
    s_prev_held = held;
}

void plat_rumble(bool on)
{
    if (on == s_rumble)
        return;
    s_rumble = on;
    SDL_SetWindowTitle(g_window, on ? "Night Heist [RUMBLE]" : "Night Heist");
}

/* ---- files --------------------------------------------------------------- */

const char *plat_data_dir(void)
{
    return g_desktop_opts.data_dir;
}

bool plat_storage_ready(void)
{
    return true;
}

long plat_file_read(const char *path, void *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t n = fread(buf, 1, cap, f);
    bool err = ferror(f) != 0;
    fclose(f);
    return err ? -1 : (long)n;
}

bool plat_file_write(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    return ok;
}

int main(int argc, char **argv)
{
    NhPlatformArgs args = { argc, argv };
    return nh_app_run(&args);
}
