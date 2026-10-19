/* Night Heist - Phase 0 test scene simulation.
 * Pure and deterministic: no platform calls, input in, state + events out. */
#ifndef NH_DEMO_SIM_H
#define NH_DEMO_SIM_H

#include "core/nh_math.h"
#include "platform/platform.h"

#define DEMO_MAX_INSTANCES 49
#define DEMO_INSTANCE_STEPS 5 /* 1, 9, 25, 36, 49 tori for the stress test */

enum {
    DEMO_EV_CLICK = 1u << 0, /* play the click sound */
};

typedef struct DemoSim {
    uint64_t tick;
    float spin, prev_spin;   /* torus rotation, radians */
    float spin_speed;        /* radians per second */
    int spin_mode;
    bool paused;

    NhVec3 pos, prev_pos;    /* eye position on the floor plane (y = 0) */
    float crouch;            /* 0 standing .. 1 crouched, smoothed */
    float lean;              /* -1 left .. +1 right, smoothed */

    float aim_x, aim_y;      /* torch aim in screen pixels; holds when IR is lost */
    bool torch_on;
    int instance_step;       /* index into the stress-test table */

    int rumble_ticks;        /* > 0 while the motor should run */
    uint32_t events;         /* DEMO_EV_* raised this tick */
    uint32_t clicks;
    float click_pan;         /* -1..1 for the last click */
} DemoSim;

void demo_sim_init(DemoSim *s);
void demo_sim_tick(DemoSim *s, const NhInput *in);
int demo_sim_instances(const DemoSim *s);
/* Eye height including crouch, for the renderer. */
float demo_sim_eye_height(float crouch);

#endif
