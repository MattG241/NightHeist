#include "nh_fixedstep.h"

/* Work in units of (microseconds * NH_TICK_HZ) so one tick is exactly
 * 1,000,000 units and no rounding drift accumulates over a long session. */
#define TICK_UNITS 1000000u

void nh_fixedstep_init(NhFixedStep *fs)
{
    fs->acc_scaled = 0;
    fs->total_ticks = 0;
}

int nh_fixedstep_advance(NhFixedStep *fs, uint64_t elapsed_us)
{
    /* Cap the input so a long stall (loading, debugger) can't overflow us. */
    if (elapsed_us > 250000u)
        elapsed_us = 250000u;
    fs->acc_scaled += elapsed_us * NH_TICK_HZ;

    int ticks = (int)(fs->acc_scaled / TICK_UNITS);
    if (ticks > NH_MAX_TICKS_PER_FRAME) {
        ticks = NH_MAX_TICKS_PER_FRAME;
        fs->acc_scaled = 0; /* drop the backlog: run slow rather than spiral */
    } else {
        fs->acc_scaled -= (uint64_t)ticks * TICK_UNITS;
    }
    fs->total_ticks += (uint64_t)ticks;
    return ticks;
}

float nh_fixedstep_alpha(const NhFixedStep *fs)
{
    return (float)fs->acc_scaled / (float)TICK_UNITS;
}
