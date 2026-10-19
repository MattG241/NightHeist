/* Night Heist - vector and matrix maths.
 *
 * Conventions match GX (and libogc's Mtx/Mtx44):
 *  - right-handed, camera looks down -Z, +Y up
 *  - column vectors: v' = M * v
 *  - NhMat34 is a row-major 3x4 affine matrix (rotation/scale in [0..2][0..2],
 *    translation in column 3). It has the same memory layout as libogc's Mtx,
 *    so the Wii backend can hand it to GX without conversion.
 *  - NhMat44 is row-major 4x4, same layout as libogc's Mtx44. */
#ifndef NH_MATH_H
#define NH_MATH_H

#include "nh_types.h"

#define NH_PI 3.14159265358979323846f
#define NH_DEG2RAD(d) ((d) * (NH_PI / 180.0f))
#define NH_RAD2DEG(r) ((r) * (180.0f / NH_PI))

typedef struct NhVec2 {
    float x, y;
} NhVec2;

typedef struct NhVec3 {
    float x, y, z;
} NhVec3;

typedef struct NhMat34 {
    float m[3][4];
} NhMat34;

typedef struct NhMat44 {
    float m[4][4];
} NhMat44;

static inline NhVec3 nh_v3(float x, float y, float z)
{
    NhVec3 v = { x, y, z };
    return v;
}
static inline NhVec3 nh_v3_add(NhVec3 a, NhVec3 b) { return nh_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline NhVec3 nh_v3_sub(NhVec3 a, NhVec3 b) { return nh_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline NhVec3 nh_v3_scale(NhVec3 a, float s) { return nh_v3(a.x * s, a.y * s, a.z * s); }
static inline float nh_v3_dot(NhVec3 a, NhVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline NhVec3 nh_v3_cross(NhVec3 a, NhVec3 b)
{
    return nh_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline NhVec3 nh_v3_lerp(NhVec3 a, NhVec3 b, float t)
{
    return nh_v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}
float nh_v3_len(NhVec3 a);
NhVec3 nh_v3_norm(NhVec3 a); /* returns (0,0,0) for zero-length input */

static inline float nh_lerpf(float a, float b, float t) { return a + (b - a) * t; }
float nh_wrap_angle(float a); /* wrap to [-pi, pi) */

void nh_m34_identity(NhMat34 *out);
void nh_m34_mul(NhMat34 *out, const NhMat34 *a, const NhMat34 *b); /* out = a * b, out may alias */
void nh_m34_translate(NhMat34 *out, float x, float y, float z);
void nh_m34_scale(NhMat34 *out, float x, float y, float z);
void nh_m34_rot_x(NhMat34 *out, float rad);
void nh_m34_rot_y(NhMat34 *out, float rad);
void nh_m34_rot_z(NhMat34 *out, float rad);
/* View matrix in the style of guLookAt. */
void nh_m34_look_at(NhMat34 *out, NhVec3 eye, NhVec3 up, NhVec3 target);
/* General affine inverse. Returns false if singular. */
bool nh_m34_inverse(NhMat34 *out, const NhMat34 *in);
/* Inverse-transpose of the upper 3x3 (for normals), translation zeroed. */
bool nh_m34_normal_matrix(NhMat34 *out, const NhMat34 *in);
NhVec3 nh_m34_mul_point(const NhMat34 *m, NhVec3 p);
NhVec3 nh_m34_mul_dir(const NhMat34 *m, NhVec3 d);

#endif
