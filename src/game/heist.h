/* Night Heist - an active job: the world, the player, the torch.
 * (Guards, interactables and objectives are layered on in later phases.) */
#ifndef NH_HEIST_H
#define NH_HEIST_H

#include "core/nh_player.h"
#include "platform/platform.h"
#include "world.h"

#define HEIST_FOVY 62.0f

typedef struct GameSettings {
    NhControlSettings controls;
    bool show_debug;
    bool rumble;
    bool subtitles;
    float music_volume, sfx_volume;
} GameSettings;

typedef struct Heist {
    World world;
    NhPlayer player;
    bool torch_on;
    float battery;          /* 0..1 */
    NhVec3 torch_dir;       /* smoothed aim */
    float light_level;      /* 0..1 at the player, static + torch */
    uint64_t tick;
    int debug_view;
    char status[64];
    int status_ticks;
} Heist;

bool heist_start_test(Heist *h, NhArena *level_arena, NhArena *scratch);
void heist_end(Heist *h);
void heist_tick(Heist *h, const NhInput *in, GameSettings *gs);
void heist_render(Heist *h, float alpha, const GameSettings *gs, float fps);
void heist_camera(const Heist *h, float alpha, NhMat34 *view, NhVec3 *eye);

#endif
