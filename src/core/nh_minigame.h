/* Night Heist - in-world minigames (pure logic, deterministic).
 *
 * Lockpick: twist the Wii Remote to find each pin's sweet spot; the closer
 * you are the stronger the feedback (rumble ticks and clicks). Press A to set
 * the pin; setting it outside the window slips the pick (noise, a short
 * pause, and the pin drops back).
 *
 * Safecrack: twist to spin the dial (the twist angle is the dial speed),
 * alternating direction for each number. Passing the right number gives a
 * heavy clunk; stop on it and press A to lock it in. Wrong numbers reset the
 * current one.
 *
 * Hack: match a carrier signal with the pointer (left/right sets
 * frequency, up/down amplitude). Holding a match fills the lock bar; each
 * layer gets tighter. Running out of time trips a trace. */
#ifndef NH_MINIGAME_H
#define NH_MINIGAME_H

#include "nh_rng.h"
#include "nh_types.h"

typedef enum MgEvent { MG_NONE, MG_TICK, MG_SET, MG_SLIP, MG_DONE, MG_FAIL } MgEvent;

#define LP_MAX_PINS 6

typedef struct NhLockpick {
    int npins, cur;
    float target[LP_MAX_PINS]; /* radians of twist */
    float window;              /* half-width of the sweet spot */
    float feel;                /* 0..1 how close the pick is (feedback) */
    float stun;                /* seconds of recovery after a slip */
    float time;
    int slips;
    bool in_spot;
    bool done;
} NhLockpick;

void lp_init(NhLockpick *lp, int level, NhRng *rng);
MgEvent lp_tick(NhLockpick *lp, float twist, bool press_a, float dt);

#define SC_MAX_NUMS 4

typedef struct NhSafecrack {
    int nnums, cur;
    int combo[SC_MAX_NUMS];   /* 0..99 */
    float dial;               /* 0..100, wraps */
    float last_dial;
    bool passed;              /* passed the right number in the right direction since the last stop */
    float clunk;              /* feedback 0..1, decays */
    float time;
    int mistakes;
    bool done;
} NhSafecrack;

void sc_init(NhSafecrack *sc, int tier, NhRng *rng);
/* Direction for the current number: +1 clockwise (twist right), -1. */
int sc_direction(const NhSafecrack *sc);
MgEvent sc_tick(NhSafecrack *sc, float twist, bool press_a, float dt);

#define HK_LAYERS_MAX 4

typedef struct NhHack {
    int layers, cur;
    float freq[HK_LAYERS_MAX], amp[HK_LAYERS_MAX]; /* targets, 0..1 */
    float tol;           /* match tolerance */
    float lock;          /* 0..1 progress on the current layer */
    float match;         /* 0..1 closeness, for the display */
    float time, time_limit;
    bool done, failed;
} NhHack;

void hk_init(NhHack *hk, int tier, NhRng *rng);
/* px, py: pointer position normalised to 0..1 in the hack window. */
MgEvent hk_tick(NhHack *hk, float px, float py, bool pointer_valid, float dt);

#endif
