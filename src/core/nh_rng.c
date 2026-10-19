#include "nh_rng.h"

void nh_rng_seed(NhRng *r, uint64_t seed, uint64_t stream)
{
    r->state = 0u;
    r->inc = (stream << 1u) | 1u;
    nh_rng_u32(r);
    r->state += seed;
    nh_rng_u32(r);
}

uint32_t nh_rng_u32(NhRng *r)
{
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

uint32_t nh_rng_range(NhRng *r, uint32_t bound)
{
    uint32_t threshold = (0u - bound) % bound;
    for (;;) {
        uint32_t v = nh_rng_u32(r);
        if (v >= threshold)
            return v % bound;
    }
}

float nh_rng_float(NhRng *r)
{
    return (float)(nh_rng_u32(r) >> 8) * (1.0f / 16777216.0f);
}
