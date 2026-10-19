/* Unit tests for the pure game simulation. */
#include <string.h>

#include "game/demo_sim.h"
#include "nh_test.h"

/* Deterministic pseudo-random input stream for a given frame. */
static NhInput scripted_input(int f, uint32_t *prev)
{
    NhInput in = { 0 };
    in.connected = in.nunchuk = true;
    in.ir_valid = (f / 50) % 3 != 2;
    in.ir_x = (float)((f * 7) % 640);
    in.ir_y = (float)((f * 13) % 480);
    in.stick_x = (float)((f % 21) - 10) / 10.0f;
    in.stick_y = (float)((f % 17) - 8) / 8.0f;
    in.nunchuk_roll = (float)((f % 11) - 5) * 0.1f;
    uint32_t held = 0;
    if (f % 23 == 0)
        held |= NH_BTN_A;
    if (f % 97 == 0)
        held |= NH_BTN_B;
    if ((f / 40) % 2)
        held |= NH_BTN_Z;
    in.held = held;
    in.pressed = held & ~*prev;
    in.released = *prev & ~held;
    *prev = held;
    return in;
}

void test_demo_sim(void)
{
    DemoSim a, b;
    demo_sim_init(&a);
    demo_sim_init(&b);
    uint32_t pa = 0, pb = 0;
    for (int f = 0; f < 3000; f++) {
        NhInput ia = scripted_input(f, &pa);
        NhInput ib = scripted_input(f, &pb);
        demo_sim_tick(&a, &ia);
        demo_sim_tick(&b, &ib);
    }
    CHECK(memcmp(&a, &b, sizeof(a)) == 0); /* bit-identical given identical input */
    CHECK(a.tick == 3000);
    CHECK(a.clicks > 0);

    /* The torch holds its aim while IR is lost. */
    DemoSim s;
    demo_sim_init(&s);
    NhInput in = { 0 };
    in.ir_valid = true;
    in.ir_x = 100.0f;
    in.ir_y = 50.0f;
    for (int i = 0; i < 60; i++)
        demo_sim_tick(&s, &in);
    CHECK_NEAR(s.aim_x, 100.0, 0.01);
    in.ir_valid = false;
    in.ir_x = 600.0f;
    for (int i = 0; i < 60; i++)
        demo_sim_tick(&s, &in);
    CHECK_NEAR(s.aim_x, 100.0, 0.01);

    /* A press rumbles for a few ticks then stops; B toggles the torch. */
    NhInput press = { 0 };
    press.pressed = NH_BTN_A | NH_BTN_B;
    bool torch = s.torch_on;
    demo_sim_tick(&s, &press);
    CHECK(s.events & DEMO_EV_CLICK);
    CHECK(s.rumble_ticks > 0);
    CHECK(s.torch_on != torch);
    NhInput idle = { 0 };
    for (int i = 0; i < 20; i++)
        demo_sim_tick(&s, &idle);
    CHECK(s.rumble_ticks == 0);
    CHECK((s.events & DEMO_EV_CLICK) == 0);

    /* The eye never leaves the room no matter how long we push. */
    NhInput push = { 0 };
    push.stick_x = 1.0f;
    push.stick_y = 1.0f;
    for (int i = 0; i < 2000; i++)
        demo_sim_tick(&s, &push);
    CHECK(s.pos.x <= 10.0f && s.pos.z >= -7.0f);
}
