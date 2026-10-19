/* Night Heist - shared basic types.
 * Core code is portable C11 and must never include platform headers. */
#ifndef NH_TYPES_H
#define NH_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NH_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define NH_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NH_MAX(a, b) ((a) > (b) ? (a) : (b))
#define NH_CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define NH_ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((size_t)(a) - 1))

/* 8-bit RGBA colour, the native colour format of GX registers. */
typedef struct NhColor {
    uint8_t r, g, b, a;
} NhColor;

static inline NhColor nh_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    NhColor c = { r, g, b, a };
    return c;
}

#endif
