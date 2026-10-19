/* Night Heist - desktop entry point, lifecycle, input, files and timing.
 *
 * Mouse + keyboard stand in for the Wii Remote + Nunchuk:
 *   mouse = pointer, LMB = A, RMB = B, WASD = stick, Q/E = Nunchuk tilt (lean),
 *   Ctrl = Z, F = C, 1/2 = 1/2, Enter = +, Tab = -, arrows = D-pad,
 *   [ ] = twist the remote (roll; \\ levels it), Esc = HOME. */
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
static bool s_has_job;
static int s_job[4];
static float s_start[5];
static bool s_overlay, s_coop;
static bool s_rumble;
static bool load_script(const char *path);

static void usage(void)
{
    puts("nightheist [--frames N] [--screenshot PATH] [--shot-every N] [--script FILE] [--wide] [--novsync]"
         " [--job TYPE,SIZE,SEED[,SECURITY]] [--test-level] [--overlay] [--coop]"
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
        } else if (!strcmp(a, "--job") && has_next) {
            s_job[3] = 2;
            s_has_job = sscanf(args->argv[++i], "%d,%d,%d,%d", &s_job[0], &s_job[1], &s_job[2], &s_job[3]) >= 3;
        } else if (!strcmp(a, "--test-level")) {
            s_has_job = true;
            s_job[0] = -1;
        } else if (!strcmp(a, "--shot-every") && has_next)
            g_desktop_opts.shot_every = atoi(args->argv[++i]);
        else if (!strcmp(a, "--wide"))
            g_desktop_opts.wide = true;
        else if (!strcmp(a, "--novsync"))
            g_desktop_opts.vsync = false;
        else if (!strcmp(a, "--overlay"))
            s_overlay = true;
        else if (!strcmp(a, "--coop"))
            s_coop = true;
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
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

bool plat_debug_job(int *type, int *size, unsigned *seed, int *security)
{
    if (!s_has_job)
        return false;
    *type = s_job[0];
    *size = s_job[1];
    *seed = (unsigned)s_job[2];
    *security = s_job[3];
    return true;
}

bool plat_debug_overlay(void) { return s_overlay; }

bool plat_debug_coop(void) { return s_coop; }

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
 * One command per line: "<frame> [p2] <cmd> [args]". State persists until
 * changed; "p2" sends the command to player 2's remote.
 *   ir X Y | noir | stick X Y | roll DEG | twist DEG | hold BTN | release BTN
 *   press BTN (one frame) | shot NAME (screenshot) | quit
 * Buttons: A B 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT Z C */
#define SCRIPT_MAX 4096
typedef struct ScriptCmd {
    uint32_t frame;
    uint8_t player;
    char cmd[12];
    char arg[48];
    float a, b;
} ScriptCmd;
static ScriptCmd s_script[SCRIPT_MAX];
static int s_nscript, s_script_pos;
static bool s_script_p2;
typedef struct ScriptState {
    bool ir_valid;
    float ir_x, ir_y, sx, sy, roll, twist;
    uint32_t held, pulse;
} ScriptState;
static ScriptState s_ss[NH_MAX_PLAYERS] = { { true, 320, 240, 0, 0, 0, 0, 0, 0 }, { true, 320, 240, 0, 0, 0, 0, 0, 0 } };
static uint32_t s_prev[NH_MAX_PLAYERS];
static SDL_GameController *s_pad;
static float s_pad_x = 320.0f, s_pad_y = 240.0f;

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
        if (!strcmp(c.cmd, "p2")) {
            c.player = 1;
            s_script_p2 = true;
            if (sscanf(rest, "%11s", c.cmd) < 1)
                continue;
            rest = strstr(rest, c.cmd) + strlen(c.cmd);
        }
        sscanf(rest, "%f %f", &c.a, &c.b);
        sscanf(rest, "%47s", c.arg);
        s_script[s_nscript++] = c;
    }
    fclose(f);
    return true;
}

/* Runs the commands due this frame (called once per frame, by player 0's poll). */
static void script_advance(void)
{
    uint64_t f = desktop_frame_count();
    s_ss[0].pulse = s_ss[1].pulse = 0;
    while (s_script_pos < s_nscript && s_script[s_script_pos].frame <= f) {
        const ScriptCmd *c = &s_script[s_script_pos++];
        ScriptState *ss = &s_ss[c->player];
        if (!strcmp(c->cmd, "ir")) {
            ss->ir_valid = true;
            ss->ir_x = c->a;
            ss->ir_y = c->b;
        } else if (!strcmp(c->cmd, "noir")) {
            ss->ir_valid = false;
        } else if (!strcmp(c->cmd, "stick")) {
            ss->sx = c->a;
            ss->sy = c->b;
        } else if (!strcmp(c->cmd, "roll")) {
            ss->roll = NH_DEG2RAD(c->a);
        } else if (!strcmp(c->cmd, "twist")) {
            ss->twist = NH_DEG2RAD(c->a);
        } else if (!strcmp(c->cmd, "hold")) {
            ss->held |= btn_from_name(c->arg);
        } else if (!strcmp(c->cmd, "release")) {
            ss->held &= ~btn_from_name(c->arg);
        } else if (!strcmp(c->cmd, "press")) {
            ss->pulse |= btn_from_name(c->arg);
        } else if (!strcmp(c->cmd, "shot") && g_desktop_opts.screenshot) {
            char path[512];
            snprintf(path, sizeof(path), "%s_%s.bmp", g_desktop_opts.screenshot, c->arg);
            desktop_gfx_screenshot(path);
        } else if (!strcmp(c->cmd, "quit")) {
            plat_request_quit();
        }
    }
}

static void script_input(int player, NhInput *out, uint32_t *held)
{
    const ScriptState *ss = &s_ss[player];
    out->ir_valid = ss->ir_valid;
    out->ir_x = ss->ir_x;
    out->ir_y = ss->ir_y;
    out->stick_x = ss->sx;
    out->stick_y = ss->sy;
    out->nunchuk_roll = ss->roll;
    out->remote_roll = ss->twist;
    *held |= ss->held | ss->pulse;
}

/* Player 1 at the keyboard and mouse. */
static void keyboard_input(NhInput *out, uint32_t *held_out)
{
    uint32_t held = 0;
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
    /* [ and ] turn the remote and it stays turned (like holding it at an
     * angle); backslash levels it again. */
    static float twist;
    twist += NH_DEG2RAD(70.0f) / 60.0f * (float)(k[SDL_SCANCODE_RIGHTBRACKET] - k[SDL_SCANCODE_LEFTBRACKET]);
    if (k[SDL_SCANCODE_BACKSLASH])
        twist = 0.0f;
    twist = NH_CLAMP(twist, NH_DEG2RAD(-80.0f), NH_DEG2RAD(80.0f));
    out->remote_roll = twist;
    *held_out |= held;
}

/* Player 2 on a gamepad: left stick moves, the right stick steers the
 * pointer, A/B as on the remote, X is C, the left shoulder is Z, the
 * triggers twist the remote. */
static bool pad_input(NhInput *out, uint32_t *held_out)
{
    if (!s_pad) {
        for (int i = 0; i < SDL_NumJoysticks() && !s_pad; i++)
            if (SDL_IsGameController(i))
                s_pad = SDL_GameControllerOpen(i);
        if (!s_pad)
            return false;
    }
    if (!SDL_GameControllerGetAttached(s_pad)) {
        SDL_GameControllerClose(s_pad);
        s_pad = NULL;
        return false;
    }
    float ax[6];
    for (int a = 0; a < 6; a++) {
        float v = (float)SDL_GameControllerGetAxis(s_pad, (SDL_GameControllerAxis)a) / 32767.0f;
        ax[a] = fabsf(v) < 0.15f ? 0.0f : NH_CLAMP(v, -1.0f, 1.0f);
    }
    out->stick_x = ax[SDL_CONTROLLER_AXIS_LEFTX];
    out->stick_y = -ax[SDL_CONTROLLER_AXIS_LEFTY];
    s_pad_x = NH_CLAMP(s_pad_x + ax[SDL_CONTROLLER_AXIS_RIGHTX] * 9.0f, 0.0f, (float)GFX_SCREEN_W - 1);
    s_pad_y = NH_CLAMP(s_pad_y + ax[SDL_CONTROLLER_AXIS_RIGHTY] * 9.0f, 0.0f, (float)GFX_SCREEN_H - 1);
    out->ir_valid = true;
    out->ir_x = s_pad_x;
    out->ir_y = s_pad_y;
    out->remote_roll = NH_DEG2RAD(70.0f) * (ax[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] - ax[SDL_CONTROLLER_AXIS_TRIGGERLEFT]);
    static const struct {
        SDL_GameControllerButton b;
        uint32_t btn;
    } map[] = {
        { SDL_CONTROLLER_BUTTON_A, NH_BTN_A },          { SDL_CONTROLLER_BUTTON_B, NH_BTN_B },
        { SDL_CONTROLLER_BUTTON_X, NH_BTN_C },          { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, NH_BTN_Z },
        { SDL_CONTROLLER_BUTTON_START, NH_BTN_PLUS },   { SDL_CONTROLLER_BUTTON_BACK, NH_BTN_MINUS },
        { SDL_CONTROLLER_BUTTON_GUIDE, NH_BTN_HOME },   { SDL_CONTROLLER_BUTTON_DPAD_UP, NH_BTN_UP },
        { SDL_CONTROLLER_BUTTON_DPAD_DOWN, NH_BTN_DOWN }, { SDL_CONTROLLER_BUTTON_DPAD_LEFT, NH_BTN_LEFT },
        { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, NH_BTN_RIGHT },
    };
    for (size_t i = 0; i < NH_ARRAY_LEN(map); i++)
        if (SDL_GameControllerGetButton(s_pad, map[i].b))
            *held_out |= map[i].btn;
    return true;
}

void plat_input_poll(int player, NhInput *out)
{
    *out = (NhInput){ 0 };
    if (player == 0) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                s_quit = true;
        }
        if (g_desktop_opts.script)
            script_advance();
    }
    if (player < 0 || player >= NH_MAX_PLAYERS)
        return;
    uint32_t held = 0;
    if (g_desktop_opts.script) {
        if (player == 1 && !s_script_p2)
            return;
        script_input(player, out, &held);
    } else if (player == 0) {
        keyboard_input(out, &held);
    } else if (!pad_input(out, &held)) {
        return;
    }
    out->connected = true;
    out->nunchuk = true;
    out->held = held;
    out->pressed = held & ~s_prev[player];
    out->released = s_prev[player] & ~held;
    s_prev[player] = held;
}

void plat_rumble(int player, bool on)
{
    if (player == 1 && s_pad)
        SDL_GameControllerRumble(s_pad, on ? 0x6000 : 0, on ? 0x6000 : 0, on ? 200 : 0);
    if (player != 0 || on == s_rumble)
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
