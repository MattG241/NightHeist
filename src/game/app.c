/* Night Heist - application entry and main loop (portable).
 * Fixed 60 Hz simulation, rendering decoupled and interpolated. */
#include "app.h"

#include <stdio.h>

#include "core/nh_arena.h"
#include "core/nh_fixedstep.h"
#include "demo_scene.h"
#include "demo_sim.h"
#include "platform/gfx.h"
#include "text.h"

#define LOAD_ARENA_SIZE (4u * 1024u * 1024u)

/* Scratch memory for load-time generation; reset after each asset is built. */
static _Alignas(32) uint8_t s_load_mem[LOAD_ARENA_SIZE];

int nh_app_run(const NhPlatformArgs *args)
{
    if (!plat_init(args))
        return 1;

    NhArena scratch;
    nh_arena_init(&scratch, s_load_mem, sizeof(s_load_mem));
    if (!text_init(&scratch) || !demo_scene_load(&scratch)) {
        plat_shutdown();
        return 2;
    }

    /* Prove storage works without writing anything: read our own meta.xml. */
    static char meta_buf[4096];
    char path[256];
    snprintf(path, sizeof(path), "%s/meta.xml", plat_data_dir());
    long meta_bytes = plat_storage_ready() ? plat_file_read(path, meta_buf, sizeof(meta_buf)) : -1;

    DemoSim sim;
    demo_sim_init(&sim);

    NhFixedStep step;
    nh_fixedstep_init(&step);
    uint64_t last = plat_time_us();
    uint64_t fps_start = last;
    uint32_t fps_frames = 0;
    float fps = 0.0f;
    uint32_t pending_pressed = 0, pending_released = 0;

    while (!plat_should_quit()) {
        uint64_t now = plat_time_us();
        int ticks = nh_fixedstep_advance(&step, now - last);
        last = now;

        NhInput in;
        plat_input_poll(&in);
        if (in.pressed & NH_BTN_HOME)
            plat_request_quit();

        /* Edges must reach exactly one tick even on frames that run zero ticks. */
        pending_pressed |= in.pressed;
        pending_released |= in.released;
        for (int i = 0; i < ticks; i++) {
            NhInput tick_in = in;
            tick_in.pressed = pending_pressed;
            tick_in.released = pending_released;
            pending_pressed = pending_released = 0;
            demo_sim_tick(&sim, &tick_in);
            demo_scene_handle_events(&sim, sim.events);
        }
        plat_rumble(sim.rumble_ticks > 0);

        fps_frames++;
        if (now - fps_start >= 1000000u) {
            fps = (float)fps_frames * 1000000.0f / (float)(now - fps_start);
            fps_frames = 0;
            fps_start = now;
        }

        DemoFrameInfo info = { &in, fps, nh_fixedstep_alpha(&step), plat_storage_ready(), meta_bytes };
        demo_scene_render(&sim, &info);
    }

    plat_rumble(false);
    plat_shutdown();
    return 0;
}
