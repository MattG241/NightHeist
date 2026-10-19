/* Night Heist - static lighting baked into vertex colours at level load.
 *
 * Each vertex gets: albedo_tint * clamp(ambient * AO + sum(static lights)).
 * Lights use the same GX attenuation model as the hardware lights (nh_light.h),
 * so baked and dynamic light match. Rooms are convex rectangles, so a light in
 * the same room is visible without a raycast; lights in other rooms only reach
 * through openings and are tested with the grid raycast. AO is approximated
 * from distances to the room's walls, floor and ceiling.
 *
 * nh_bake_sample() is also the gameplay light meter: guards and the HUD use
 * exactly the light the player sees. */
#ifndef NH_BAKE_H
#define NH_BAKE_H

#include "nh_level.h"
#include "nh_light.h"
#include "nh_mesh.h"

#define NH_BAKE_ROOM_LIGHTS 16

typedef struct NhBakeParams {
    NhColor ambient_indoor;
    NhColor ambient_outdoor;
    float ao_strength; /* 0..1 */
} NhBakeParams;

void nh_bake_default_params(NhBakeParams *p);
/* Static lamp -> GX light coefficients. */
void nh_bake_light(const LvLight *l, NhLight *out);
/* Lights that can reach `room` (on, powered, within range); returns count. */
int nh_bake_room_lights(const Level *L, int room, int *out, int max);

/* Light arriving at p (with surface normal n, or zero for "a body here"),
 * RGB in 0..1+ (not clamped). room may be 0 to look it up. */
NhVec3 nh_bake_sample(const Level *L, NhVec3 p, NhVec3 n, int room, const NhBakeParams *bp);
/* 0..1 perceptual brightness of static light at a body position. */
float nh_bake_light_level(const Level *L, NhVec3 p, const NhBakeParams *bp);

/* Write m->colors from albedo (per-vertex tint). */
void nh_bake_mesh(const Level *L, int room, NhMesh *m, const NhColor *albedo, const NhBakeParams *bp);

#endif
