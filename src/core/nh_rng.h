/* Night Heist - deterministic random numbers (PCG32, O'Neill 2014,
 * https://www.pcg-random.org; follows the minimal C reference, Apache-2.0).
 * Same seed -> same sequence on every platform. Never use rand(). */
#ifndef NH_RNG_H
#define NH_RNG_H

#include "nh_types.h"

typedef struct NhRng {
    uint64_t state;
    uint64_t inc;
} NhRng;

void nh_rng_seed(NhRng *r, uint64_t seed, uint64_t stream);
uint32_t nh_rng_u32(NhRng *r);
/* Uniform integer in [0, bound). bound must be > 0. Unbiased. */
uint32_t nh_rng_range(NhRng *r, uint32_t bound);
/* Uniform float in [0, 1). Built from 24 random bits, so bit-identical everywhere. */
float nh_rng_float(NhRng *r);

#endif
