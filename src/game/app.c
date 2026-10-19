/* Night Heist - application entry and main loop (portable).
 * Fixed 60 Hz simulation, rendering decoupled and interpolated. */
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/nh_arena.h"
#include "core/nh_fixedstep.h"
#include "heist.h"
#include "platform/gfx.h"
#include "res.h"
#include "text.h"

#define PERSIST_SIZE (4u << 20)
#define LEVEL_SIZE (12u << 20)
#define SCRATCH_SIZE (10u << 20)

static NhArena s_persist, s_level, s_scratch;

void app_arena_stats(size_t out[6])
{
    out[0] = s_persist.peak;
    out[1] = s_persist.cap;
    out[2] = s_level.peak;
    out[3] = s_level.cap;
    out[4] = s_scratch.peak;
    out[5] = s_scratch.cap;
}
static Heist s_heist;
static GameSettings s_settings;

static bool make_arena(NhArena *a, size_t size)
{
    uint8_t *raw = malloc(size + 32);
    if (!raw)
        return false;
    uint8_t *aligned = (uint8_t *)NH_ALIGN_UP((uintptr_t)raw, 32);
    nh_arena_init(a, aligned, size);
    return true;
}

static void loading_screen(const char *msg)
{
    gfx_begin_frame(nh_rgba(0, 0, 0, 255));
    text_draw(320 - text_width("NIGHT HEIST", 4) * 0.5f, 200, 4, nh_rgba(255, 210, 120, 255), "NIGHT HEIST");
    text_draw(320 - text_width(msg, 2) * 0.5f, 260, 2, nh_rgba(170, 170, 180, 255), msg);
    gfx_end_frame();
}

int nh_app_run(const NhPlatformArgs *args)
{
    if (!plat_init(args))
        return 1;
    if (!make_arena(&s_persist, PERSIST_SIZE) || !make_arena(&s_level, LEVEL_SIZE) ||
        !make_arena(&s_scratch, SCRATCH_SIZE)) {
        plat_shutdown();
        return 2;
    }
    res_init(&s_persist, &s_scratch);
    if (!text_init(&s_scratch)) {
        plat_shutdown();
        return 3;
    }
    nh_controls_default(&s_settings.controls);
    s_settings.rumble = true;
    s_settings.subtitles = true;
    s_settings.music_volume = s_settings.sfx_volume = 0.8f;

    loading_screen("building the test building...");
    if (!heist_start_test(&s_heist, &s_level, &s_scratch)) {
        fprintf(stderr, "failed to build the level\n");
        plat_shutdown();
        return 4;
    }

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
            heist_tick(&s_heist, &tick_in, &s_settings);
        }

        fps_frames++;
        if (now - fps_start >= 1000000u) {
            fps = (float)fps_frames * 1000000.0f / (float)(now - fps_start);
            fps_frames = 0;
            fps_start = now;
        }
        heist_render(&s_heist, nh_fixedstep_alpha(&step), &s_settings, fps);
    }

    heist_end(&s_heist);
    plat_rumble(false);
    plat_shutdown();
    return 0;
}
