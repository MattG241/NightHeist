#include "nh_texconv.h"

#include <string.h>

typedef struct BlockInfo {
    int bw, bh;      /* block size in pixels */
    int block_bytes; /* bytes per block */
} BlockInfo;

static BlockInfo block_info(NhTexFormat fmt)
{
    BlockInfo b = { 4, 4, 32 };
    switch (fmt) {
    case NH_TF_I8:
        b.bw = 8;
        break;
    case NH_TF_RGBA8:
        b.block_bytes = 64;
        break;
    case NH_TF_CMPR:
        b.bw = 8;
        b.bh = 8;
        break;
    case NH_TF_IA8:
    case NH_TF_RGB565:
    default:
        break;
    }
    return b;
}

size_t nh_tex_level_size(NhTexFormat fmt, int w, int h)
{
    BlockInfo b = block_info(fmt);
    size_t bx = (size_t)((w + b.bw - 1) / b.bw);
    size_t by = (size_t)((h + b.bh - 1) / b.bh);
    return bx * by * (size_t)b.block_bytes;
}

size_t nh_tex_total_size(NhTexFormat fmt, int w, int h, int levels)
{
    size_t total = 0;
    for (int i = 0; i < levels; i++) {
        total += nh_tex_level_size(fmt, w, h);
        w = NH_MAX(w / 2, 1);
        h = NH_MAX(h / 2, 1);
    }
    return total;
}

int nh_tex_max_levels(int w, int h)
{
    int levels = 1;
    while ((w > 1 || h > 1) && levels < NH_TEX_MAX_LEVELS) {
        w = NH_MAX(w / 2, 1);
        h = NH_MAX(h / 2, 1);
        levels++;
    }
    return levels;
}

static uint8_t luminance(const uint8_t *p)
{
    return (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29 + 128) >> 8);
}

/* Fetch a pixel, clamping to the image edge for partial blocks. */
static const uint8_t *px(const uint8_t *rgba, int w, int h, int x, int y)
{
    x = NH_MIN(x, w - 1);
    y = NH_MIN(y, h - 1);
    return rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
}

static uint16_t to565(const uint8_t *p)
{
    unsigned r = (p[0] * 31u + 127u) / 255u;
    unsigned g = (p[1] * 63u + 127u) / 255u;
    unsigned b = (p[2] * 31u + 127u) / 255u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t to5a3(const uint8_t *p)
{
    if (p[3] >= 224) {
        unsigned r = (p[0] * 31u + 127u) / 255u, g = (p[1] * 31u + 127u) / 255u, bl = (p[2] * 31u + 127u) / 255u;
        return (uint16_t)(0x8000u | (r << 10) | (g << 5) | bl);
    }
    unsigned a = (p[3] * 7u + 127u) / 255u, r = (p[0] * 15u + 127u) / 255u, g = (p[1] * 15u + 127u) / 255u,
             bl = (p[2] * 15u + 127u) / 255u;
    return (uint16_t)((a << 12) | (r << 8) | (g << 4) | bl);
}

void nh_tex_encode(NhTexFormat fmt, const uint8_t *rgba, int w, int h, uint8_t *out)
{
    BlockInfo b = block_info(fmt);
    if (fmt == NH_TF_CMPR) {
        memset(out, 0, nh_tex_level_size(fmt, w, h));
        return;
    }
    uint8_t *o = out;
    for (int by = 0; by < h; by += b.bh) {
        for (int bx = 0; bx < w; bx += b.bw) {
            if (fmt == NH_TF_RGBA8) {
                /* 4x4 block: 16 AR pairs, then 16 GB pairs. */
                for (int i = 0; i < 16; i++) {
                    const uint8_t *p = px(rgba, w, h, bx + (i & 3), by + (i >> 2));
                    o[i * 2 + 0] = p[3];
                    o[i * 2 + 1] = p[0];
                    o[32 + i * 2 + 0] = p[1];
                    o[32 + i * 2 + 1] = p[2];
                }
                o += 64;
                continue;
            }
            for (int y = 0; y < b.bh; y++) {
                for (int x = 0; x < b.bw; x++) {
                    const uint8_t *p = px(rgba, w, h, bx + x, by + y);
                    switch (fmt) {
                    case NH_TF_I8:
                        *o++ = luminance(p);
                        break;
                    case NH_TF_IA8:
                        *o++ = p[3];
                        *o++ = luminance(p);
                        break;
                    case NH_TF_RGB5A3: {
                        uint16_t v = to5a3(p);
                        *o++ = (uint8_t)(v >> 8);
                        *o++ = (uint8_t)(v & 0xff);
                        break;
                    }
                    case NH_TF_RGB565:
                    default: {
                        uint16_t v = to565(p);
                        *o++ = (uint8_t)(v >> 8);
                        *o++ = (uint8_t)(v & 0xff);
                        break;
                    }
                    }
                }
            }
        }
    }
}

static void put(uint8_t *rgba, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x >= w || y >= h)
        return;
    uint8_t *d = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
    d[0] = r;
    d[1] = g;
    d[2] = b;
    d[3] = a;
}

static void rgb565_expand(unsigned v, uint8_t out[3])
{
    unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[0] = (uint8_t)((r << 3) | (r >> 2));
    out[1] = (uint8_t)((g << 2) | (g >> 4));
    out[2] = (uint8_t)((b << 3) | (b >> 2));
}

/* One 4x4 CMPR sub-block (big-endian DXT1). */
static void decode_cmpr_block(const uint8_t *s, uint8_t *rgba, int w, int h, int x0, int y0)
{
    unsigned c0 = ((unsigned)s[0] << 8) | s[1], c1 = ((unsigned)s[2] << 8) | s[3];
    uint8_t pal[4][4];
    rgb565_expand(c0, pal[0]);
    rgb565_expand(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    for (int k = 0; k < 3; k++) {
        if (c0 > c1) {
            pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
        } else {
            pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = 0;
        }
    }
    pal[2][3] = 255;
    pal[3][3] = c0 > c1 ? 255 : 0;
    for (int y = 0; y < 4; y++) {
        uint8_t bits = s[4 + y];
        for (int x = 0; x < 4; x++) {
            int idx = (bits >> (6 - x * 2)) & 3;
            int px = x0 + x, py = y0 + y;
            if (px < w && py < h)
                memcpy(rgba + ((size_t)py * (size_t)w + (size_t)px) * 4, pal[idx], 4);
        }
    }
}

void nh_tex_decode(NhTexFormat fmt, const uint8_t *in, int w, int h, uint8_t *rgba)
{
    BlockInfo b = block_info(fmt);
    const uint8_t *s = in;
    if (fmt == NH_TF_CMPR) {
        for (int by = 0; by < h; by += 8)
            for (int bx = 0; bx < w; bx += 8)
                for (int sb = 0; sb < 4; sb++) {
                    decode_cmpr_block(s, rgba, w, h, bx + (sb & 1) * 4, by + (sb >> 1) * 4);
                    s += 8;
                }
        return;
    }
    for (int by = 0; by < h; by += b.bh) {
        for (int bx = 0; bx < w; bx += b.bw) {
            if (fmt == NH_TF_RGBA8) {
                for (int i = 0; i < 16; i++)
                    put(rgba, w, h, bx + (i & 3), by + (i >> 2), s[i * 2 + 1], s[32 + i * 2],
                        s[32 + i * 2 + 1], s[i * 2]);
                s += 64;
                continue;
            }
            for (int y = 0; y < b.bh; y++) {
                for (int x = 0; x < b.bw; x++) {
                    switch (fmt) {
                    case NH_TF_I8:
                        put(rgba, w, h, bx + x, by + y, s[0], s[0], s[0], s[0]);
                        s += 1;
                        break;
                    case NH_TF_IA8:
                        put(rgba, w, h, bx + x, by + y, s[1], s[1], s[1], s[0]);
                        s += 2;
                        break;
                    case NH_TF_RGB5A3: {
                        unsigned v = ((unsigned)s[0] << 8) | s[1];
                        if (v & 0x8000) {
                            unsigned r = (v >> 10) & 31, g = (v >> 5) & 31, bl = v & 31;
                            put(rgba, w, h, bx + x, by + y, (uint8_t)((r << 3) | (r >> 2)),
                                (uint8_t)((g << 3) | (g >> 2)), (uint8_t)((bl << 3) | (bl >> 2)), 255);
                        } else {
                            unsigned a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, bl = v & 15;
                            put(rgba, w, h, bx + x, by + y, (uint8_t)(r * 17), (uint8_t)(g * 17), (uint8_t)(bl * 17),
                                (uint8_t)((a << 5) | (a << 2) | (a >> 1)));
                        }
                        s += 2;
                        break;
                    }
                    case NH_TF_RGB565:
                    default: {
                        unsigned v = ((unsigned)s[0] << 8) | s[1];
                        unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, bl = v & 31;
                        put(rgba, w, h, bx + x, by + y, (uint8_t)((r << 3) | (r >> 2)),
                            (uint8_t)((g << 2) | (g >> 4)), (uint8_t)((bl << 3) | (bl >> 2)), 255);
                        s += 2;
                        break;
                    }
                    }
                }
            }
        }
    }
}

void nh_tex_downsample(const uint8_t *rgba, int w, int h, uint8_t *out)
{
    int ow = NH_MAX(w / 2, 1), oh = NH_MAX(h / 2, 1);
    for (int y = 0; y < oh; y++) {
        for (int x = 0; x < ow; x++) {
            for (int c = 0; c < 4; c++) {
                unsigned sum = 0;
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++)
                        sum += px(rgba, w, h, x * 2 + dx, y * 2 + dy)[c];
                out[((size_t)y * (size_t)ow + (size_t)x) * 4 + (size_t)c] = (uint8_t)((sum + 2) / 4);
            }
        }
    }
}

void nh_tex_encode_mips(NhTexFormat fmt, const uint8_t *rgba, int w, int h, int levels,
                        uint8_t *out, uint8_t *scratch)
{
    const uint8_t *src = rgba;
    uint8_t *bufs[2] = { scratch, scratch + (size_t)w * (size_t)h };
    for (int i = 0; i < levels; i++) {
        nh_tex_encode(fmt, src, w, h, out);
        out += nh_tex_level_size(fmt, w, h);
        if (i + 1 < levels) {
            uint8_t *dst = bufs[i & 1];
            nh_tex_downsample(src, w, h, dst);
            src = dst;
            w = NH_MAX(w / 2, 1);
            h = NH_MAX(h / 2, 1);
        }
    }
}
