/* Night Heist - Wii Remote + Nunchuk input via wiiuse/WPAD. */
#include <gccore.h>
#include <math.h>
#include <wiiuse/wpad.h>

#include "core/nh_math.h"
#include "platform/gfx.h"
#include "platform/platform.h"
#include "wii_internal.h"

#define STICK_DEADZONE 0.15f

static bool s_rumble;

void wii_input_init(void)
{
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, GFX_SCREEN_W, GFX_SCREEN_H);
}

void wii_input_shutdown(void)
{
    WPAD_Rumble(WPAD_CHAN_0, 0);
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

/* Normalise one stick axis against the Nunchuk's own calibration. */
static float stick_axis(int pos, int center, int min, int max)
{
    float range = (float)(pos >= center ? max - center : center - min);
    if (range < 1.0f)
        return 0.0f;
    return NH_CLAMP((float)(pos - center) / range, -1.0f, 1.0f);
}

void plat_input_poll(NhInput *out)
{
    *out = (NhInput){ 0 };
    WPAD_ScanPads();

    u32 type = 0;
    if (WPAD_Probe(WPAD_CHAN_0, &type) != WPAD_ERR_NONE)
        return;
    WPADData *wd = WPAD_Data(WPAD_CHAN_0);
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
    if (wd->exp.type == WPAD_EXP_NUNCHUK) {
        const joystick_t *js = &wd->exp.nunchuk.js;
        float x = stick_axis(js->pos.x, js->center.x, js->min.x, js->max.x);
        float y = stick_axis(js->pos.y, js->center.y, js->min.y, js->max.y);
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

void plat_rumble(bool on)
{
    if (on == s_rumble)
        return;
    s_rumble = on;
    WPAD_Rumble(WPAD_CHAN_0, on ? 1 : 0);
}
