/* Night Heist - Wii Remote + Nunchuk input via wiiuse/WPAD. */
#include <gccore.h>
#include <math.h>
#include <wiiuse/wpad.h>

#include "core/nh_math.h"
#include "platform/gfx.h"
#include "platform/platform.h"
#include "wii_internal.h"

#define STICK_DEADZONE 0.2f

static bool s_rumble[NH_MAX_PLAYERS];

void wii_input_init(void)
{
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, GFX_SCREEN_W, GFX_SCREEN_H);
}

void wii_input_shutdown(void)
{
    WPAD_Rumble(WPAD_CHAN_0, 0);
    WPAD_Rumble(WPAD_CHAN_1, 0);
    WPAD_Shutdown();
}

static uint32_t map_buttons(u32 w)
{
    static const struct {
        u32 wpad;
        uint32_t nh;
    } map[] = {
        { WPAD_BUTTON_A, NH_BTN_A },         { WPAD_BUTTON_B, NH_BTN_B },
        { WPAD_BUTTON_1, NH_BTN_1 },         { WPAD_BUTTON_2, NH_BTN_2 },
        { WPAD_BUTTON_PLUS, NH_BTN_PLUS },   { WPAD_BUTTON_MINUS, NH_BTN_MINUS },
        { WPAD_BUTTON_HOME, NH_BTN_HOME },   { WPAD_BUTTON_UP, NH_BTN_UP },
        { WPAD_BUTTON_DOWN, NH_BTN_DOWN },   { WPAD_BUTTON_LEFT, NH_BTN_LEFT },
        { WPAD_BUTTON_RIGHT, NH_BTN_RIGHT }, { WPAD_NUNCHUK_BUTTON_Z, NH_BTN_Z },
        { WPAD_NUNCHUK_BUTTON_C, NH_BTN_C },
    };
    uint32_t out = 0;
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (w & map[i].wpad)
            out |= map[i].nh;
    return out;
}

/* Normalise one stick axis against the Nunchuk's own calibration. Some
 * Nunchuks (third-party ones especially) report nonsense calibration, which
 * reads as the stick held over to one side; those get standard values,
 * centred on where the stick rested when it was plugged in. */
static float stick_axis(int pos, int center, int min, int max, float rest)
{
    bool sane = center > 90 && center < 166 && max - center > 50 && center - min > 50 && max <= 255 && min >= 0;
    if (!sane) {
        center = (int)rest;
        min = center - 100;
        max = center + 100;
    }
    float range = (float)(pos >= center ? max - center : center - min);
    if (range < 1.0f)
        return 0.0f;
    return NH_CLAMP((float)(pos - center) / range, -1.0f, 1.0f);
}

/* Where each Nunchuk's stick sat when it was plugged in (-1: not seen yet). */
static float s_rest[NH_MAX_PLAYERS][2] = { { -1, -1 }, { -1, -1 } };

void plat_input_poll(int player, NhInput *out)
{
    *out = (NhInput){ 0 };
    if (player == 0)
        WPAD_ScanPads(); /* reads every remote */
    if (player < 0 || player >= NH_MAX_PLAYERS)
        return;
    int chan = player == 0 ? WPAD_CHAN_0 : WPAD_CHAN_1;

    /* Not connected (yet): this frame has no input; try again next frame. */
    u32 type = 0;
    if (WPAD_Probe(chan, &type) != WPAD_ERR_NONE)
        return;
    WPADData *wd = WPAD_Data(chan);
    if (!wd)
        return;

    out->connected = true;
    out->held = map_buttons(wd->btns_h);
    out->pressed = map_buttons(wd->btns_d);
    out->released = map_buttons(wd->btns_u);

    out->ir_valid = wd->ir.valid != 0;
    out->ir_x = wd->ir.x;
    out->ir_y = wd->ir.y;
    out->remote_roll = NH_DEG2RAD(wd->orient.roll);

    out->expansion = (int)wd->exp.type;
    if (wd->exp.type != WPAD_EXP_NUNCHUK)
        s_rest[player][0] = s_rest[player][1] = -1.0f; /* re-learn when one is plugged in */
    if (wd->exp.type == WPAD_EXP_NUNCHUK) {
        const joystick_t *js = &wd->exp.nunchuk.js;
        float *rest = s_rest[player];
        if (rest[0] < 0.0f) {
            /* First reading: trust it as the centre only if it's near the middle. */
            rest[0] = js->pos.x > 80 && js->pos.x < 176 ? (float)js->pos.x : 128.0f;
            rest[1] = js->pos.y > 80 && js->pos.y < 176 ? (float)js->pos.y : 128.0f;
        }
        float x = stick_axis(js->pos.x, js->center.x, js->min.x, js->max.x, rest[0]);
        float y = stick_axis(js->pos.y, js->center.y, js->min.y, js->max.y, rest[1]);
        float mag = sqrtf(x * x + y * y);
        if (mag < STICK_DEADZONE) {
            x = y = 0.0f;
        } else {
            /* Rescale so motion starts at zero just outside the dead zone. */
            float scaled = NH_MIN((mag - STICK_DEADZONE) / (1.0f - STICK_DEADZONE), 1.0f);
            x *= scaled / mag;
            y *= scaled / mag;
        }
        out->nunchuk = true;
        out->stick_x = x;
        out->stick_y = y;
        out->nunchuk_roll = NH_DEG2RAD(wd->exp.nunchuk.orient.roll);
    }
}

void plat_rumble(int player, bool on)
{
    if (player < 0 || player >= NH_MAX_PLAYERS || on == s_rumble[player])
        return;
    s_rumble[player] = on;
    WPAD_Rumble(player == 0 ? WPAD_CHAN_0 : WPAD_CHAN_1, on ? 1 : 0);
}
