/* Night Heist - every sound and all the music, synthesised at boot.
 *
 * No sample data ships with the game: each effect is a short recipe of
 * oscillators, noise, envelopes and filters, and each music theme is a set
 * of loop-aligned stems built from notes on a grid. Rendering is
 * deterministic (a fixed PCG32 stream per sound), so the tests can pin the
 * output down. Looping sounds and every music stem are rendered with their
 * tails wrapped onto the start, so the loop point is seamless. */
#ifndef NH_SYNTH_H
#define NH_SYNTH_H

#include "nh_types.h"

#define NH_SYNTH_RATE 22050

typedef enum NhSfx {
    /* Footsteps by LvSurface, in the same order. */
    SFX_STEP_CONCRETE, SFX_STEP_CARPET, SFX_STEP_TILE, SFX_STEP_WOOD,
    SFX_STEP_MARBLE, SFX_STEP_METAL, SFX_STEP_ASPHALT, SFX_STEP_GRASS,
    SFX_LAND,
    /* Doors and locks */
    SFX_DOOR_OPEN, SFX_DOOR_CLOSE, SFX_DOOR_LOCKED, SFX_DOOR_VAULT,
    SFX_PICK_TICK, SFX_PICK_SET, SFX_PICK_SLIP, SFX_UNLOCK,
    SFX_SAFE_CLICK, SFX_SAFE_SET, SFX_SAFE_OPEN,
    SFX_KEYPAD_BEEP, SFX_KEYPAD_OK, SFX_KEYPAD_BAD, SFX_SWIPE,
    SFX_HACK_BEEP, SFX_HACK_OK, SFX_HACK_DONE, SFX_HACK_FAIL,
    /* Loot */
    SFX_PICKUP, SFX_PICKUP_HEAVY, SFX_CASH,
    /* Gadgets */
    SFX_THROW, SFX_COIN, SFX_NOISEMAKER, SFX_SMOKE, SFX_EMP, SFX_TASER, SFX_DART,
    SFX_JAMMER, SFX_GLASS_CUT, SFX_GLASS_BREAK, SFX_DRILL, SFX_PHONE,
    SFX_THERMAL_ON, SFX_THERMAL_OFF, SFX_TORCH,
    /* Power and security */
    SFX_POWER_DOWN, SFX_POWER_UP, SFX_CAMERA_BEEP, SFX_LASER_TRIP, SFX_ALARM, SFX_SIREN,
    /* People */
    SFX_RADIO, SFX_GUARD_HUH, SFX_GUARD_ALERT, SFX_GUARD_MUTTER, SFX_DOG_BARK,
    SFX_TAKEDOWN, SFX_BODY_THUD, SFX_HIDE, SFX_HEARTBEAT,
    /* Stingers and UI */
    SFX_STINGER, SFX_CAUGHT, SFX_SUCCESS, SFX_FAIL, SFX_VAN,
    SFX_UI_MOVE, SFX_UI_SELECT, SFX_UI_BACK, SFX_UI_DENY,
    SFX_COUNT
} NhSfx;

const char *nh_sfx_name(int sfx);

/* Length in samples at NH_SYNTH_RATE. */
int nh_sfx_length(int sfx);
bool nh_sfx_loops(int sfx);
/* Renders into out[0..nh_sfx_length). `scratch` needs as many floats. */
void nh_sfx_render(int sfx, int16_t *out, float *scratch);

typedef enum NhTheme { THEME_HEIST, THEME_SAFEHOUSE, THEME_COUNT } NhTheme;

#define NH_MUSIC_LAYERS 4

/* Heist layers: 0 ambient, 1 pulse, 2 tension, 3 action.
 * Safehouse layers: 0 keys and bass, 1 brushes. */
int nh_music_layers(int theme);
int nh_music_length(int theme); /* samples per loop, the same for every layer */
void nh_music_render(int theme, int layer, int16_t *out, float *scratch);

#endif
