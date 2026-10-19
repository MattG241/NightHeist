#include "nh_texgen.h"

#include "nh_rng.h"

/* Cheap integer hash for per-cell variation that doesn't depend on draw order. */
static uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed)
{
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

static uint8_t clamp_u8(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static void set_px(uint8_t *p, int r, int g, int b)
{
    p[0] = clamp_u8(r);
    p[1] = clamp_u8(g);
    p[2] = clamp_u8(b);
    p[3] = 255;
}

void nh_texgen_bricks(uint8_t *rgba, int w, int h, uint32_t seed)
{
    const int rows = 8, cols = 4;
    int bh = h / rows, bw = w / cols;
    NhRng rng;
    nh_rng_seed(&rng, seed, 1);
    for (int y = 0; y < h; y++) {
        int row = y / bh;
        int offset = (row & 1) ? bw / 2 : 0;
        for (int x = 0; x < w; x++) {
            int sx = (x + offset) % w;
            int col = sx / bw;
            int lx = sx % bw, ly = y % bh;
            uint8_t *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
            int noise = (int)nh_rng_range(&rng, 24) - 12;
            if (lx < 2 || ly < 2) {
                set_px(p, 120 + noise, 116 + noise, 108 + noise); /* mortar */
            } else {
                uint32_t hv = hash2((uint32_t)col, (uint32_t)row, seed);
                int tint = (int)(hv % 40) - 20;
                int edge = (lx < 4 || ly < 4 || lx > bw - 3 || ly > bh - 3) ? -18 : 0;
                set_px(p, 150 + tint + noise + edge, 70 + tint / 2 + noise + edge, 52 + noise + edge);
            }
        }
    }
}

void nh_texgen_concrete(uint8_t *rgba, int w, int h, uint32_t seed)
{
    const int tiles = 2;
    int tw = w / tiles, th = h / tiles;
    NhRng rng;
    nh_rng_seed(&rng, seed, 2);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
            uint32_t hv = hash2((uint32_t)(x / tw), (uint32_t)(y / th), seed);
            int base = 128 + (int)(hv % 24) - 12;
            int speck = (int)nh_rng_range(&rng, 30) - 15;
            if (nh_rng_range(&rng, 64) == 0)
                speck -= 40;
            int seam = ((x % tw) < 2 || (y % th) < 2) ? -45 : 0;
            int v = base + speck + seam;
            set_px(p, v, v, v + 4);
        }
    }
}
