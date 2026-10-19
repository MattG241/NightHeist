/* Night Heist - game audio: renders the synthesised bank at boot, plays
 * effects (flat or positioned, with distance falloff, panning and wall
 * occlusion) and runs the layered music, fading each stem towards the
 * level the game asks for. */
#ifndef NH_AUDIO_H
#define NH_AUDIO_H

#include "core/nh_level.h"
#include "core/nh_synth.h"

bool audio_init(void);
/* Settings, 0..10 each. */
void audio_volumes(int music, int sfx);

/* A flat effect; pitch 1 = as rendered. */
void audio_play(int sfx, float vol, float pan, float pitch);
/* The listener for positioned effects. L may be NULL (no occlusion). */
void audio_listener(NhVec3 pos, float yaw, int floor, const Level *L);
/* Split-screen: up to two listeners; each sound plays as loud as the nearer one hears it. */
void audio_listeners(const NhVec3 *pos, const float *yaw, const int *floor, int n, const Level *L);
/* A positioned effect, silent beyond `range` metres; a little random pitch. */
void audio_at(int sfx, NhVec3 pos, float vol, float range);

/* Looping effects: returns a handle, or -1. */
int audio_loop(int sfx, float vol);
void audio_loop_set(int handle, float vol, float pan);
void audio_loop_at(int handle, NhVec3 pos, float vol, float range);
void audio_loop_stop(int *handle);

/* Music: switch theme (-1 for silence); stems start together at zero. */
void audio_music(int theme);
/* Target level per stem, 0..1; stems fade towards it. */
void audio_music_mix(const float level[NH_MUSIC_LAYERS]);
void audio_tick(float dt);

/* Stops every loop and effect (leaving a heist). Music keeps playing. */
void audio_stop_effects(void);

#endif
