/* Night Heist - the loaded level: static geometry batches, baked light,
 * portals, doors and props, and drawing them. */
#ifndef NH_WORLD_H
#define NH_WORLD_H

#include "core/nh_bake.h"
#include "core/nh_kit.h"
#include "core/nh_portal.h"
#include "core/nh_rooms.h"
#include "platform/gfx.h"

#define WORLD_BATCHES_MAX 2400

typedef struct WorldBatch {
    uint8_t room, layer;
    uint16_t mat;
    GfxMesh gm;
    NhMesh cpu;        /* verts + baked colours (CPU copy for re-baking) */
    NhColor *albedo;   /* per-vertex surface tint */
    NhVec3 bmin, bmax;
} WorldBatch;

typedef struct WorldView {
    NhMat34 view;
    NhVec3 eye;
    float fovy, aspect, znear, zfar;
    bool in_vent;
    int vent_floor;
    /* Extra opaque geometry (characters) drawn before the blended pass. */
    void (*draw_opaque)(void *ctx);
    void *ctx;
} WorldView;

typedef struct World {
    Level L;
    NhPortalSet portals;
    NhBakeParams bake;
    WorldBatch batches[WORLD_BATCHES_MAX];
    int nbatches;
    uint16_t room_first[LV_ROOMS_MAX], room_count[LV_ROOMS_MAX];
    NhVec3 room_min[LV_ROOMS_MAX], room_max[LV_ROOMS_MAX];
    NhColor prop_ambient[LV_PROPS_MAX];
    uint8_t mat_map[MAT_COUNT];
    uint8_t visible[LV_ROOMS_MAX];
    int cam_room, nvisible;
    uint32_t build_ms;
    GfxTexture tex_glow, tex_glass;
} World;

/* L must already be filled (generator or hand-built). */
bool world_build(World *w, NhArena *level_arena, NhArena *scratch);
void world_free(World *w);
/* Re-light one room's meshes and props after lights change (power cut). */
void world_rebake_room(World *w, int room);
/* Re-bake after lights switch or power changes. */
void world_rebake(World *w);
void world_update(World *w, float dt);
void world_draw(World *w, const WorldView *v);
/* Ambient light for a dynamic object at p (for characters, loot, doors). */
NhColor world_ambient_at(const World *w, NhVec3 p);
/* Display brightness 1..5 (3 = default); takes effect on the next build or rebake. */
void world_set_brightness(int level);
/* Row-major GL-style view-projection used for culling. */
void world_view_proj(const WorldView *v, float out[16]);

#endif
