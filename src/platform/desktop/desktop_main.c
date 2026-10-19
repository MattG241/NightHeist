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

DesktopOptions g_desktop_opts = { 0, NULL, 0, NULL, false, false, true, "apps/NightHeist" };
SDL_Window *g_window;

static bool s_quit;
static bool s_has_start;
static float s_start[5];
static uint32_t s_prev_held;
static bool s_rumble;
static bool load_script(const char *path);

static void usage(void)
{
    puts("nightheist [--frames N] [--screenshot PATH] [--shot-every N] [--script FILE] [--wide] [--novsync]"
         " [--data DIR]");
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
        else if (!strcmp(a, "--script") && has_next)
            g_desktop_opts.script = args->argv[++i];
        else if (!strcmp(a, "--start") && has_next) {
            s_has_start = sscanf(args->argv[++i], "%f,%f,%f,%f,%f", &s_start[0], &s_start[1], &s_start[2],
                                 &s_start[3], &s_start[4]) >= 4;
            s_start[3] = NH_DEG2RAD(s_start[3]);
            s_start[4] = NH_DEG2RAD(s_start[4]);
        } else if (!strcmp(a, "--shot-every") && has_next)
            g_desktop_opts.shot_every = atoi(args->argv[++i]);
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
    if (g_desktop_opts.script && !load_script(g_desktop_opts.script))
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

bool plat_debug_start(float *x, float *y, float *z, float *yaw, float *pitch)
{
    if (!s_has_start)
        return false;
    *x = s_start[0];
    *y = s_start[1];
    *z = s_start[2];
    *yaw = s_start[3];
    *pitch = s_start[4];
    return true;
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

/* ---- scripted input (--script FILE) ----------------------------------------
 * One command per line: "<frame> <cmd> [args]". State persists until changed.
 *   ir X Y | noir | stick X Y | roll DEG | twist DEG | hold BTN | release BTN
 *   press BTN (one frame) | shot NAME (screenshot) | quit
 * Buttons: A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT Z C */
#define SCRIPT_MAX 4096
typedef struct ScriptCmd {
    uint32_t frame;
    char cmd[12];
    char arg[48];
    float a, b;
} ScriptCmd;
static ScriptCmd s_script[SCRIPT_MAX];
static int s_nscript, s_script_pos;
static struct {
    bool ir_valid;
    float ir_x, ir_y, sx, sy, roll, twist;
    uint32_t held, pulse;
} s_ss = { true, 320, 240, 0, 0, 0, 0, 0, 0 };

static uint32_t btn_from_name(const char *n)
{
    static const struct {
        const char *n;
        uint32_t b;
    } t[] = { { "A", NH_BTN_A }, { "B", NH_BTN_B }, { "1", NH_BTN_1 }, { "2", NH_BTN_2 }, { "PLUS", NH_BTN_PLUS },
              { "MINUS", NH_BTN_MINUS }, { "HOME", NH_BTN_HOME }, { "UP", NH_BTN_UP }, { "DOWN", NH_BTN_DOWN },
              { "LEFT", NH_BTN_LEFT }, { "RIGHT", NH_BTN_RIGHT }, { "Z", NH_BTN_Z }, { "C", NH_BTN_C } };
    for (size_t i = 0; i < NH_ARRAY_LEN(t); i++)
        if (!strcmp(t[i].n, n))
            return t[i].b;
    return 0;
}

static bool load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot open script %s\n", path);
        return false;
    }
    char line[160];
    while (fgets(line, sizeof(line), f) && s_nscript < SCRIPT_MAX) {
        ScriptCmd c = { 0 };
        unsigned frame;
        if (line[0] == '#' || sscanf(line, "%u %11s", &frame, c.cmd) < 2)
            continue;
        c.frame = frame;
        char *rest = strstr(line, c.cmd) + strlen(c.cmd);
        sscanf(rest, "%f %f", &c.a, &c.b);
        sscanf(rest, "%47s", c.arg);
        s_script[s_nscript++] = c;
    }
    fclose(f);
    return true;
}

static void script_input(NhInput *out, uint32_t *held)
{
    uint64_t f = desktop_frame_count();
    s_ss.pulse = 0;
    while (s_script_pos < s_nscript && s_script[s_script_pos].frame <= f) {
        const ScriptCmd *c = &s_script[s_script_pos++];
        if (!strcmp(c->cmd, "ir")) {
            s_ss.ir_valid = true;
            s_ss.ir_x = c->a;
            s_ss.ir_y = c->b;
        } else if (!strcmp(c->cmd, "noir")) {
            s_ss.ir_valid = false;
        } else if (!strcmp(c->cmd, "stick")) {
            s_ss.sx = c->a;
            s_ss.sy = c->b;
        } else if (!strcmp(c->cmd, "roll")) {
            s_ss.roll = NH_DEG2RAD(c->a);
        } else if (!strcmp(c->cmd, "twist")) {
            s_ss.twist = NH_DEG2RAD(c->a);
        } else if (!strcmp(c->cmd, "hold")) {
            s_ss.held |= btn_from_name(c->arg);
        } else if (!strcmp(c->cmd, "release")) {
            s_ss.held &= ~btn_from_name(c->arg);
        } else if (!strcmp(c->cmd, "press")) {
            s_ss.pulse |= btn_from_name(c->arg);
        } else if (!strcmp(c->cmd, "shot") && g_desktop_opts.screenshot) {
            char path[512];
            snprintf(path, sizeof(path), "%s_%s.bmp", g_desktop_opts.screenshot, c->arg);
            desktop_gfx_screenshot(path);
        } else if (!strcmp(c->cmd, "quit")) {
            plat_request_quit();
        }
    }
    out->ir_valid = s_ss.ir_valid;
    out->ir_x = s_ss.ir_x;
    out->ir_y = s_ss.ir_y;
    out->stick_x = s_ss.sx;
    out->stick_y = s_ss.sy;
    out->nunchuk_roll = s_ss.roll;
    out->remote_roll = s_ss.twist;
    *held |= s_ss.held | s_ss.pulse;
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

    if (g_desktop_opts.script) {
        script_input(out, &held);
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
