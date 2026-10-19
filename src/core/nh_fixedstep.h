/* Night Heist - fixed-timestep accumulator.
 * The simulation always advances in exact 1/60 s ticks regardless of the
 * display rate (60 Hz NTSC, 50 Hz PAL, or a desktop monitor). Rendering
 * interpolates between the last two ticks using nh_fixedstep_alpha(). */
#ifndef NH_FIXEDSTEP_H
#define NH_FIXEDSTEP_H

#include "nh_types.h"

#define NH_TICK_HZ 60
#define NH_TICK_DT (1.0f / (float)NH_TICK_HZ)
#define NH_MAX_TICKS_PER_FRAME 4 /* beyond this we drop time instead of spiralling */

typedef struct NhFixedStep {
    uint64_t acc_scaled; /* accumulated time in microseconds * NH_TICK_HZ */
    uint64_t total_ticks;
} NhFixedStep;

void nh_fixedstep_init(NhFixedStep *fs);
/* Feed elapsed wall time; returns the number of ticks to simulate now. */
int nh_fixedstep_advance(NhFixedStep *fs, uint64_t elapsed_us);
/* Fraction [0,1) of the next tick already elapsed, for render interpolation. */
float nh_fixedstep_alpha(const NhFixedStep *fs);

#endif
