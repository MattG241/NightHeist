/* Night Heist - Phase 0 test scene: a dark room, spinning lit+textured tori,
 * a pointer-aimed torch and an input/perf readout. */
#ifndef NH_DEMO_SCENE_H
#define NH_DEMO_SCENE_H

#include "core/nh_arena.h"
#include "demo_sim.h"

typedef struct DemoFrameInfo {
    const NhInput *input;
    float fps;
    float alpha;           /* render interpolation factor */
    bool storage_ok;
    long meta_bytes;       /* size of meta.xml read from storage, -1 if unreadable */
} DemoFrameInfo;

bool demo_scene_load(NhArena *scratch);
void demo_scene_render(const DemoSim *sim, const DemoFrameInfo *info);
/* Plays the click for DEMO_EV_CLICK. */
void demo_scene_handle_events(const DemoSim *sim, uint32_t events);

#endif
