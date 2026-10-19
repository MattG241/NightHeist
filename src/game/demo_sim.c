#include "demo_sim.h"

#include "core/nh_fixedstep.h"

static const int k_instances[DEMO_INSTANCE_STEPS] = { 1, 9, 25, 36, 49 };
static const float k_spin_speeds[3] = { 0.8f, 2.0f, 0.0f };

#define WALK_SPEED 2.5f   /* m/s */
#define CROUCH_SPEED 1.2f
#define ROOM_HALF 10.0f   /* keep the eye inside the test room */
#define MAX_LEAN_RAD NH_DEG2RAD(30.0f)

void demo_sim_init(DemoSim *s)
{
    *s = (DemoSim){ 0 };
    s->spin_speed = k_spin_speeds[0];
    s->pos = s->prev_pos = nh_v3(0.0f, 0.0f, 5.0f);
    s->aim_x = 320.0f;
    s->aim_y = 240.0f;
    s->torch_on = true;
}

float demo_sim_eye_height(float crouch)
{
    return nh_lerpf(1.65f, 0.95f, crouch);
}

int demo_sim_instances(const DemoSim *s)
{
    return k_instances[s->instance_step];
}

static float approach(float cur, float target, float rate)
{
    return cur + (target - cur) * rate;
}

void demo_sim_tick(DemoSim *s, const NhInput *in)
{
    s->tick++;
    s->events = 0;
    s->prev_spin = s->spin;
    s->prev_pos = s->pos;

    if (in->pressed & NH_BTN_PLUS)
        s->paused = !s->paused;
    if (in->pressed & NH_BTN_C) {
        s->spin_mode = (s->spin_mode + 1) % 3;
        s->spin_speed = k_spin_speeds[s->spin_mode];
    }
    if (!s->paused)
        s->spin = nh_wrap_angle(s->spin + s->spin_speed * NH_TICK_DT);

    if (in->pressed & NH_BTN_B)
        s->torch_on = !s->torch_on;
    if ((in->pressed & NH_BTN_2) && s->instance_step + 1 < DEMO_INSTANCE_STEPS)
        s->instance_step++;
    if ((in->pressed & NH_BTN_1) && s->instance_step > 0)
        s->instance_step--;

    /* Crouch is hold-to-crouch here; the toggle option arrives with settings. */
    s->crouch = approach(s->crouch, (in->held & NH_BTN_Z) ? 1.0f : 0.0f, 0.15f);

    float lean_target = NH_CLAMP(in->nunchuk_roll / MAX_LEAN_RAD, -1.0f, 1.0f);
    s->lean = approach(s->lean, lean_target, 0.12f);

    /* Camera faces -Z in Phase 0 (turning arrives with the Phase 1 controller). */
    float speed = nh_lerpf(WALK_SPEED, CROUCH_SPEED, s->crouch) * NH_TICK_DT;
    s->pos.x = NH_CLAMP(s->pos.x + in->stick_x * speed, -ROOM_HALF, ROOM_HALF);
    s->pos.z = NH_CLAMP(s->pos.z - in->stick_y * speed, -ROOM_HALF + 3.0f, ROOM_HALF);

    /* The torch follows the pointer and holds its last direction when IR is lost. */
    if (in->ir_valid) {
        s->aim_x = approach(s->aim_x, in->ir_x, 0.5f);
        s->aim_y = approach(s->aim_y, in->ir_y, 0.5f);
    }

    if (in->pressed & NH_BTN_A) {
        s->events |= DEMO_EV_CLICK;
        s->clicks++;
        s->click_pan = NH_CLAMP((s->aim_x - 320.0f) / 320.0f, -1.0f, 1.0f);
        s->rumble_ticks = 6;
    } else if (s->rumble_ticks > 0) {
        s->rumble_ticks--;
    }
}
