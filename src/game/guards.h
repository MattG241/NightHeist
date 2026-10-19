/* Night Heist - guard presentation: skinned models, animation blending from
 * AI state, guard torches (one projected cookie + per-vertex spots) and the
 * AI debug overlay. The simulation itself lives in core/nh_ai. */
#ifndef NH_GUARDS_H
#define NH_GUARDS_H

#include "core/nh_ai.h"
#include "core/nh_assets.h"
#include "platform/gfx.h"
#include "world.h"

typedef struct GuardVis {
    int clip, prev_clip;      /* AiAnim */
    float t, prev_t, blend;   /* blend: 0 = prev clip, 1 = current */
    NhVec3 prev_pos;
    float prev_yaw;
} GuardVis;

typedef struct GuardsVis {
    GuardVis g[AI_MAX_GUARDS];
} GuardsVis;

void guards_vis_init(GuardsVis *gv, const AiWorld *ai);
/* Call before ai_tick to keep interpolation endpoints. */
void guards_vis_pre_tick(GuardsVis *gv, const AiWorld *ai);
void guards_vis_tick(GuardsVis *gv, const AiWorld *ai, float dt);

/* Interpolated root transform. */
void guards_root(const GuardsVis *gv, const AiWorld *ai, int i, float alpha, NhVec3 *pos, float *yaw);
/* Torch lens position and direction for guard i (world space). */
void guards_torch(const GuardsVis *gv, const AiWorld *ai, int i, float alpha, NhVec3 *pos, NhVec3 *dir);

/* Fill the guard projector and dynamic spot lights for this frame. Returns
 * the number of lights written (<= max). */
int guards_lights(const GuardsVis *gv, const AiWorld *ai, const World *w, NhVec3 eye, float alpha,
                  GfxProjector *proj, NhLight *lights, int max);
/* xray: thermal goggles, draw every guard within 30 m of the eye through walls. */
void guards_draw(const GuardsVis *gv, const AiWorld *ai, const World *w, float alpha, bool xray, NhVec3 eye);
/* Lens glows (additive sprites); call in the 3D pass after guards_draw. */
void guards_draw_glows(const GuardsVis *gv, const AiWorld *ai, const World *w, const NhMat34 *view,
                       float alpha);
/* Debug: view cones and paths on the floor. */
void guards_draw_debug(const AiWorld *ai, const World *w);

#endif
