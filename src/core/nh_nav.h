/* Night Heist - navigation for guards and dogs.
 *
 * A nav grid at 1 m resolution (2x2 nodes per 2 m cell) per floor, built from
 * the level: walls, windows and rails block; doors are passable (guards open
 * them) at a small extra cost; solid props block the nodes they cover. Stairs
 * are directional ramps whose top nodes link to the floor above.
 * A* with a binary heap, an LRU cache of recent paths, and string-pulling so
 * guards walk straight lines instead of grid zig-zags. Door crossings keep a
 * waypoint at the middle of the opening. */
#ifndef NH_NAV_H
#define NH_NAV_H

#include "nh_level.h"

#define NAV_RES 2
#define NAV_W (LV_W_MAX * NAV_RES)
#define NAV_D (LV_D_MAX * NAV_RES)
#define NAV_NODES (LV_F_MAX * NAV_W * NAV_D)
#define NAV_PATH_MAX 64
#define NAV_CACHE 24

enum { NAV_WALK = 1u << 0, NAV_STAIR = 1u << 1, NAV_OUTSIDE = 1u << 2, NAV_DOORWAY = 1u << 3 };

typedef struct NhPath {
    NhVec3 p[NAV_PATH_MAX];
    int n;
} NhPath;

typedef struct NhNav {
    const Level *L;
    int w, d, floors;        /* in nodes */
    uint8_t flags[LV_F_MAX][NAV_D][NAV_W];
    /* Search scratch */
    float g[NAV_NODES];
    int32_t parent[NAV_NODES];
    uint32_t stamp[NAV_NODES];
    uint8_t closed[NAV_NODES];
    int32_t heap[NAV_NODES];
    int heap_n;
    uint32_t cur_stamp;
    /* Path cache */
    struct {
        int32_t from, to;
        uint32_t age;
        NhPath path;
    } cache[NAV_CACHE];
    uint32_t cache_clock;
    int expansions; /* stats of the last search */
} NhNav;

void nh_nav_build(NhNav *nav, const Level *L, float agent_radius);
/* Rebuild after props move (bodies don't block; furniture does). */
void nh_nav_clear_cache(NhNav *nav);
bool nh_nav_walkable(const NhNav *nav, NhVec3 p);
/* Nearest walkable node centre to p (searching a few metres). */
bool nh_nav_snap(const NhNav *nav, NhVec3 p, NhVec3 *out);
/* Path from a to b; false if unreachable. allow_doors=false for dogs. */
bool nh_nav_path(NhNav *nav, NhVec3 a, NhVec3 b, bool allow_doors, NhPath *out);
/* Straight walk test on one floor (used for string pulling and steering). */
bool nh_nav_line_clear(const NhNav *nav, NhVec3 a, NhVec3 b);
float nh_path_length(const NhPath *p);

#endif
