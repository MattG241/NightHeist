/* Night Heist - GX-compatible per-vertex light model.
 *
 * The Wii's lighting hardware evaluates, per vertex and per light:
 *
 *   L      = normalize(light_pos - P),  d = |light_pos - P|
 *   diff   = max(dot(N, L), 0)                               (GX_DF_CLAMP)
 *   cos    = max(dot(-L, light_dir), 0)
 *   atten  = max(a0 + a1*cos + a2*cos^2, 0) / (k0 + k1*d + k2*d^2)   (GX_AF_SPOT)
 *   colour = material * clamp(ambient + sum(light_colour * diff * atten), 0, 1)
 *
 * The coefficient helpers below reproduce libogc's GX_InitLightSpot and
 * GX_InitLightDistAttn so both backends (and CPU-side gameplay code such as
 * the player light-level meter) agree exactly on how bright things are. */
#ifndef NH_LIGHT_H
#define NH_LIGHT_H

#include "nh_math.h"

#define NH_MAX_HW_LIGHTS 8

typedef enum NhSpotFn {
    NH_SPOT_OFF,   /* omni light */
    NH_SPOT_FLAT,  /* hard edge */
    NH_SPOT_COS,
    NH_SPOT_COS2,  /* smooth falloff to the cone edge */
    NH_SPOT_SHARP,
} NhSpotFn;

typedef enum NhDistFn {
    NH_DIST_OFF,
    NH_DIST_GENTLE,
    NH_DIST_MEDIUM,
    NH_DIST_STEEP,
} NhDistFn;

typedef struct NhLight {
    NhVec3 pos;      /* world space */
    NhVec3 dir;      /* world space, normalized, direction the light points */
    NhColor color;
    float a[3];      /* angular attenuation a0,a1,a2 */
    float k[3];      /* distance attenuation k0,k1,k2 */
} NhLight;

/* Point light with no attenuation. */
void nh_light_init(NhLight *l, NhVec3 pos, NhColor color);
/* Same maths as GX_InitLightSpot. cutoff_deg is the half-angle of the cone. */
void nh_light_set_spot(NhLight *l, NhVec3 dir, float cutoff_deg, NhSpotFn fn);
/* Same maths as GX_InitLightDistAttn: brightness is ref_bright at ref_dist. */
void nh_light_set_dist(NhLight *l, float ref_dist, float ref_bright, NhDistFn fn);

/* CPU evaluation of one light's contribution at P with normal N (normal may be
 * zero to skip the diffuse term, e.g. for "how lit is the player"). Returns
 * the scalar diff*atten factor, unclamped. */
float nh_light_factor(const NhLight *l, NhVec3 p, NhVec3 n);

#endif
