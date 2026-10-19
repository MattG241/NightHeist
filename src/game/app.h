#ifndef NH_APP_H
#define NH_APP_H

#include "platform/platform.h"

/* Runs the game until the user quits. Called from each backend's main(). */
int nh_app_run(const NhPlatformArgs *args);
/* Peak/capacity of the persistent, level and scratch arenas (bytes). */
void app_arena_stats(size_t out[6]);

#endif
