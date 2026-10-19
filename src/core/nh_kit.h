/* Night Heist - modular building kit: turns a Level into renderable geometry.
 *
 * Geometry is emitted per room (the unit of portal culling and light baking)
 * into batches keyed by (room, layer, material). Walls are drawn 0.16 m thick
 * (each room's face is offset into the room) with reveals around openings.
 * Exterior faces of the building go to the OUTSIDE room so they are visible
 * from the street. Vent ducts go to layer 1 and are only drawn from inside. */
#ifndef NH_KIT_H
#define NH_KIT_H

#include "nh_level.h"
#include "nh_mesh.h"

#define NH_BATCHES_MAX 24
#define NH_WALL_HALF 0.08f

enum { NH_LAYER_ROOM = 0, NH_LAYER_VENT = 1 };

typedef struct NhBatch {
    uint8_t room, layer, material;
    NhMesh mesh;      /* colours hold the surface tint until lighting is baked */
} NhBatch;

typedef struct NhBatchSet {
    NhBatch b[NH_BATCHES_MAX];
    int n;
    NhArena *arena;
    uint32_t vcap, icap;
    bool overflow;
} NhBatchSet;

void nh_batches_init(NhBatchSet *s, NhArena *arena, uint32_t vcap, uint32_t icap);
/* Mesh for (room, layer, material), creating it (or a continuation when the
 * current one is nearly full) as needed. NULL if out of memory/slots. */
NhMesh *nh_batches_get(NhBatchSet *s, int room, int layer, int material, uint32_t need_verts);

/* All values are renderer material ids; map[] translates a room's NhMat
 * surface ids (nh_rooms.h) into renderer ids. */
typedef struct NhKitMaterials {
    uint8_t ceiling, roof, stair, duct, rail, facade, frame, parapet, ground;
    const uint8_t *map;
} NhKitMaterials;

/* Emit all architecture belonging to `room` (floors, ceilings, walls,
 * openings, stairs, rails, facades for OUTSIDE, ducts on the vent layer). */
void nh_kit_build_room(const Level *L, int room, const NhKitMaterials *km, NhBatchSet *out);

/* World-space AABB of a room including the storey height (for culling). */
void nh_kit_room_bounds(const Level *L, int room, NhVec3 *bmin, NhVec3 *bmax);

#endif
