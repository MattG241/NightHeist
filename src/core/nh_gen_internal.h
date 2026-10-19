/* Night Heist - generator internals shared by nh_gen.c (layout),
 * nh_gen_dress.c (furniture and set pieces) and nh_gen_sec.c (security,
 * loot, guards). Not part of the public API. */
#ifndef NH_GEN_INTERNAL_H
#define NH_GEN_INTERNAL_H

#include "nh_gen.h"
#include "nh_proptab.h"
#include "nh_rng.h"
#include "nh_rooms.h"

/* Room roles in the plan (before functions are chosen). */
enum { GR_ROOM, GR_CORRIDOR, GR_STAIR, GR_HALL, GR_BACK, GR_ROOF, GR_OUTSIDE };

#define OCC_RES 4 /* occupancy cells per level cell (0.5 m) */

typedef struct GenRoom {
    uint8_t role;
    uint8_t depth;      /* doors from the entrance */
    uint8_t secure;     /* wanted lock strength 0..3 */
    uint8_t setpiece;   /* set piece + 1 */
    uint8_t exterior;   /* touches the outside (side mask: bit per side) */
    uint8_t front;      /* touches the street facade */
    uint8_t dressed;
    uint8_t lit;
    int16_t entry_door; /* the door leading in from the parent room, -1 none */
    int16_t parent;     /* parent room in the BFS tree, -1 */
} GenRoom;

typedef struct GenCtx {
    Level *L;
    NhPlan *P;
    const NhGenParams *gp;
    NhRng rng;
    int type, size, floors, security;
    int bx0, bz0, bx1, bz1;    /* footprint, inclusive cells */
    int stx, stz, st_axis;     /* stairwell door-end anchor; axis 0: 3 wide in X, runs along Z */
    int st_flip;               /* run towards -Z / -X from the anchor */
    int stair_room[LV_F_MAX];
    int corridor[LV_F_MAX];    /* main corridor room per floor, 0 if none */
    int entrance;              /* ground-floor room with the front door */
    int front_door;
    int roof_room;
    GenRoom gr[LV_ROOMS_MAX];
    uint8_t occ[LV_F_MAX][LV_D_MAX * OCC_RES][LV_W_MAX * OCC_RES]; /* 1 = taken, 2 = keep clear */
    int loot_budget;           /* loot items still to place */
} GenCtx;

/* ---- rng ---------------------------------------------------------------- */
static inline int gen_ri(GenCtx *c, int lo, int hi) /* inclusive */
{
    return hi <= lo ? lo : lo + (int)nh_rng_range(&c->rng, (uint32_t)(hi - lo + 1));
}
static inline bool gen_chance(GenCtx *c, int pct) { return (int)nh_rng_range(&c->rng, 100) < pct; }

/* ---- geometry ------------------------------------------------------------ */
static inline int gen_area(const LvRoom *r) { return (r->x1 - r->x0 + 1) * (r->z1 - r->z0 + 1); }
static inline int gen_rw(const LvRoom *r) { return r->x1 - r->x0 + 1; }
static inline int gen_rd(const LvRoom *r) { return r->z1 - r->z0 + 1; }
/* Yaw that turns a prop's front (local -Z) away from a wall on cell side s. */
float gen_wall_yaw(int side);
/* Count of edges of a given type between room a and anything else. */
int gen_room_edges(const Level *L, int room, int type);

/* ---- occupancy (0.5 m grid) for furniture ---------------------------------- */
bool gen_occ_free(GenCtx *c, int f, float x, float z, float hx, float hz);
void gen_occ_mark(GenCtx *c, int f, float x, float z, float hx, float hz, uint8_t v);
/* Place a prop if its footprint (plus margin) is free; returns prop index or -1. */
int gen_prop(GenCtx *c, int type, int f, float x, float y, float z, float yaw, int extra_flags);
/* Prop on the floor against the wall at cell (x,z) side s; offset along the wall in metres. */
int gen_prop_wall(GenCtx *c, int type, int f, int x, int z, int side, float along, int extra_flags);
/* Wall-mounted prop (painting, keypad, fuse box) at height y. */
int gen_prop_on_wall(GenCtx *c, int type, int f, int x, int z, int side, float along, float y);

/* ---- dressing (nh_gen_dress.c) ------------------------------------------- */
void gen_dress_room(GenCtx *c, int room);
void gen_choose_setpieces(GenCtx *c);
void gen_dress_outside(GenCtx *c);
/* Small loot item on a surface or the floor in a room. */
int gen_loot(GenCtx *c, int room, int prop_type, float x, float y, float z, int tier, int flags);
int gen_entity(GenCtx *c, int kind, int room, int prop, NhVec3 pos, float yaw);

/* Rooms reachable from outside with the keys the plan provides (nh_gen.c). */
void gen_reach(const Level *L, const NhPlan *P, uint8_t *reach);

/* ---- security, loot, guards (nh_gen_sec.c) -------------------------------- */
void gen_security(GenCtx *c);
void gen_guards(GenCtx *c);
void gen_room_graph_depths(GenCtx *c);
/* A walkable spot (cell centre clear of furniture) in a room, false if none. */
bool gen_room_spot(GenCtx *c, int room, NhVec3 *out);

#endif
