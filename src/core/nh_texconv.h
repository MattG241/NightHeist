/* Night Heist - GX texture formats.
 *
 * Textures are stored in the Wii's native tiled formats (big-endian, block
 * swizzled, mip levels concatenated). The Wii backend uploads these bytes
 * as-is; the desktop backend decodes them back to RGBA8 with the routines
 * below, so the desktop shows exactly the precision loss the Wii would. */
#ifndef NH_TEXCONV_H
#define NH_TEXCONV_H

#include "nh_types.h"

/* Values match GX_TF_* so the Wii backend can pass them straight through. */
typedef enum NhTexFormat {
    NH_TF_I8 = 0x1,
    NH_TF_IA8 = 0x3,
    NH_TF_RGB565 = 0x4,
    NH_TF_RGBA8 = 0x6,
} NhTexFormat;

#define NH_TEX_MAX_LEVELS 11 /* 1024 -> 1 */

size_t nh_tex_level_size(NhTexFormat fmt, int w, int h);
size_t nh_tex_total_size(NhTexFormat fmt, int w, int h, int levels);
int nh_tex_max_levels(int w, int h);

/* Encode one RGBA8 level (row-major, 4 bytes/pixel) into GX tiled layout. */
void nh_tex_encode(NhTexFormat fmt, const uint8_t *rgba, int w, int h, uint8_t *out);
/* Decode one GX tiled level back to RGBA8. */
void nh_tex_decode(NhTexFormat fmt, const uint8_t *in, int w, int h, uint8_t *rgba);
/* 2x2 box filter; out is (w/2 x h/2), each at least 1. */
void nh_tex_downsample(const uint8_t *rgba, int w, int h, uint8_t *out);

/* Encode a full mip chain. scratch needs w*h*2 bytes (two half-size RGBA
 * buffers ping-ponged). out needs nh_tex_total_size() bytes. */
void nh_tex_encode_mips(NhTexFormat fmt, const uint8_t *rgba, int w, int h, int levels,
                        uint8_t *out, uint8_t *scratch);

#endif
