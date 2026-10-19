/* Night Heist - the level model.
 *
 * A building is a stack of floors on a grid of 2 m cells. Every cell belongs to
 * a room (or is void); the boundaries between cells are edges that are open,
 * walls, doors, windows, arches or rails. Stairs are 3-cell ramps that climb
 * one storey; the cells above a stair are holes in the upper floor. Each floor
 * has a vent layer in its ceiling void. Floor index `floors` is the roof.
 *
 * Everything spatial in the game (mesh building, collision, line of sight,
 * navigation, noise, portal culling, light baking) reads this structure, so it
 * is pure data plus queries, with no platform calls. The builder functions are
 * shared by the procedural generator and hand-authored levels.
 *
 * World space: cell (x, z) on floor f spans [x*2, x*2+2] x [z*2, z*2+2] in
 * metres, its floor is at y = f * LV_STOREY. */
#ifndef NH_LEVEL_H
#define NH_LEVEL_H

#include "nh_math.h"

#define LV_CELL 2.0f
#define LV_STOREY 3.6f      /* floor to floor */
#define LV_CEIL 3.0f        /* room ceiling height; the 0.6 m above holds ducts */
#define LV_DOOR_W 1.0f
#define LV_DOOR_H 2.2f
#define LV_WIN_Y0 0.9f      /* window opening, metres above the floor */
#define LV_WIN_Y1 2.3f
#define LV_RAIL_H 1.0f
#define LV_STAIR_LEN 3      /* cells */
#define LV_STEP_UP 0.45f    /* max height a walker steps up without climbing */

#define LV_W_MAX 48
#define LV_D_MAX 48
#define LV_F_MAX 6          /* up to 5 storeys + roof layer */
#define LV_ROOMS_MAX 220
#define LV_DOORS_MAX 250
#define LV_STAIRS_MAX 16
#define LV_PROPS_MAX 1200
#define LV_LIGHTS_MAX 240
#define LV_CELL_PROPS 6     /* props indexed per cell for collision/LOS */

enum {
    LV_ROOM_VOID = 0,
    LV_ROOM_OUTSIDE = 1, /* ground around the building, floor 0 */
    LV_ROOM_FIRST = 2,
};

typedef enum LvEdgeType {
    LV_E_OPEN = 0, /* inside a room */
    LV_E_WALL,
    LV_E_DOOR,
    LV_E_WINDOW,   /* wall with a glazed opening; passable once opened/cut */
    LV_E_ARCH,     /* doorway without a door */
    LV_E_RAIL,     /* waist-high railing: blocks movement, not sight */
} LvEdgeType;

typedef struct LvEdge {
    uint8_t type;
    uint8_t ref;   /* door index + 1, or window state for windows */
} LvEdge;

enum { LV_WIN_CLOSED = 0, LV_WIN_OPEN = 1, LV_WIN_CUT = 2 };

/* Cell flags */
enum {
    LV_C_HOLE = 1u << 0,    /* no floor (above a stair) */
    LV_C_VENT = 1u << 1,    /* duct in this floor's ceiling void */
    LV_C_GRATE = 1u << 2,   /* vent grate between the room and the duct */
    LV_C_NOCEIL = 1u << 3,  /* open to the sky */
    LV_C_HATCH = 1u << 4,   /* roof hatch above this cell */
    LV_C_NONAV = 1u << 5,   /* blocked for navigation (furniture) */
    LV_C_EXTRACT = 1u << 6, /* extraction zone */
    LV_C_ENTRY = 1u << 7,   /* player spawn/entry zone */
};

typedef enum LvSurface {
    LV_SURF_CONCRETE,
    LV_SURF_CARPET,
    LV_SURF_TILE,
    LV_SURF_WOOD,
    LV_SURF_MARBLE,
    LV_SURF_METAL,
    LV_SURF_ASPHALT,
    LV_SURF_GRASS,
    LV_SURF_COUNT
} LvSurface;

typedef struct LvCell {
    uint8_t room;
    uint8_t flags;
    uint8_t surface;
    uint8_t stair;  /* stair index + 1 on the lower floor of a stair */
} LvCell;

/* Room flags */
enum {
    LV_R_OUTDOOR = 1u << 0,
    LV_R_ROOF = 1u << 1,
    LV_R_CORRIDOR = 1u << 2,
    LV_R_STAIRWELL = 1u << 3,
    LV_R_SECURE = 1u << 4,
    LV_R_LIT = 1u << 5,     /* lights on at the start of the job */
    LV_R_SETPIECE = 1u << 6,
    LV_R_PUBLIC = 1u << 7,
};

typedef struct LvRoom {
    uint8_t floor;
    uint8_t x0, z0, x1, z1;  /* inclusive cell bounds */
    uint8_t func;            /* room function, see nh_rooms.h */
    uint8_t setpiece;        /* set-piece template + 1, 0 = none */
    uint8_t circuit;         /* power circuit (lights, cameras, lasers) */
    uint8_t flags;
    uint8_t wall_mat, floor_mat, ceil_mat; /* NhMat (nh_rooms.h) */
    NhColor wall_tint;
} LvRoom;

typedef enum LvDoorKind { LV_DOOR_WOOD, LV_DOOR_METAL, LV_DOOR_GLASS, LV_DOOR_VAULT, LV_DOOR_SHUTTER } LvDoorKind;

typedef enum LvLock {
    LV_LOCK_NONE,
    LV_LOCK_PICK,      /* lockpick minigame, level 1..3 */
    LV_LOCK_KEYCARD,   /* needs keycard key_id (or cloner) */
    LV_LOCK_CODE,      /* keypad, code key_id */
    LV_LOCK_POWERED,   /* magnetic: opens when its circuit loses power */
    LV_LOCK_VAULT,     /* vault wheel: needs the vault objective chain */
} LvLock;

typedef struct LvDoor {
    uint8_t floor, x, z, axis; /* axis 0: on the x boundary (normal +X), 1: z boundary (normal +Z) */
    uint8_t kind, lock, lock_level, key_id;
    uint8_t room_a, room_b;    /* room on the - side, room on the + side */
    uint8_t locked, flags;
    float open;                /* 0 closed .. 1 fully open */
    float target;
} LvDoor;

enum { LV_DOOR_F_EXTERIOR = 1u << 0, LV_DOOR_F_ALARMED = 1u << 1, LV_DOOR_F_BROKEN = 1u << 2 };

typedef struct LvStair {
    uint8_t floor, x, z, dir; /* dir: 0 +X, 1 +Z, 2 -X, 3 -Z (ascending) */
} LvStair;

/* Prop flags */
enum {
    LV_P_SOLID = 1u << 0,     /* blocks movement */
    LV_P_OCCLUDER = 1u << 1,  /* blocks line of sight */
    LV_P_HIDE = 1u << 2,      /* hiding spot */
    LV_P_INTERACT = 1u << 3,
    LV_P_HIDDEN = 1u << 4,    /* not drawn / not collided (taken loot, etc.) */
    LV_P_DYNAMIC = 1u << 5,   /* drawn on its own (can change); else merged into the room mesh */
};

typedef struct LvProp {
    uint16_t type;
    uint8_t room, floor;
    float x, y, z, yaw;
    NhVec3 half;   /* collision half extents in the prop's local frame */
    uint8_t flags;
    uint8_t state;
    uint16_t entity; /* gameplay entity index + 1 */
} LvProp;

typedef enum LvLightKind {
    LV_LIGHT_CEILING,
    LV_LIGHT_LAMP,
    LV_LIGHT_WINDOW, /* moonlight through a window */
    LV_LIGHT_STREET,
    LV_LIGHT_SIGN,   /* exit signs, screens: small, always on */
    LV_LIGHT_SPOT,   /* gallery spotlights */
} LvLightKind;

typedef struct LvLight {
    NhVec3 pos, dir;
    NhColor color;
    float radius;
    float cone;      /* cos of half-angle for spots, 0 = omni */
    uint8_t room, kind, circuit, on;
    uint8_t emergency; /* stays on when the power is cut */
} LvLight;

typedef struct Level {
    int w, d, floors; /* floors = storeys; floor index `floors` is the roof layer */
    uint32_t seed;
    LvCell cells[LV_F_MAX][LV_D_MAX][LV_W_MAX];
    LvEdge ex[LV_F_MAX][LV_D_MAX][LV_W_MAX + 1];  /* boundary between (x-1,z) and (x,z) */
    LvEdge ez[LV_F_MAX][LV_D_MAX + 1][LV_W_MAX];  /* boundary between (x,z-1) and (x,z) */
    LvRoom rooms[LV_ROOMS_MAX];
    int nrooms;
    LvDoor doors[LV_DOORS_MAX];
    int ndoors;
    LvStair stairs[LV_STAIRS_MAX];
    int nstairs;
    LvProp props[LV_PROPS_MAX];
    int nprops;
    LvLight lights[LV_LIGHTS_MAX];
    int nlights;
    uint16_t cell_props[LV_F_MAX][LV_D_MAX][LV_W_MAX][LV_CELL_PROPS]; /* prop index + 1 */
    uint8_t power[16];  /* circuit powered? */
} Level;

/* ---- building --------------------------------------------------------------- */

void lv_init(Level *L, int w, int d, int floors, uint32_t seed);
/* Fill a rectangle with a new room; returns its id or 0 if full. */
int lv_add_room(Level *L, int floor, int x0, int z0, int x1, int z1, int func, int flags);
/* Put walls on every boundary between different rooms (rails around roofs and
 * holes, walls elsewhere) that hasn't been given a type yet. Call after all
 * rooms, doors and windows are placed. */
void lv_finalize(Level *L);
/* Edge on the side of cell (x,z): side 0 +X, 1 +Z, 2 -X, 3 -Z. */
LvEdge *lv_edge_side(Level *L, int f, int x, int z, int side);
const LvEdge *lv_edge_side_c(const Level *L, int f, int x, int z, int side);
int lv_add_door(Level *L, int f, int x, int z, int side, int kind, int lock, int level, int key_id);
void lv_add_window(Level *L, int f, int x, int z, int side);
void lv_set_edge(Level *L, int f, int x, int z, int side, int type);
int lv_add_stair(Level *L, int f, int x, int z, int dir);
int lv_add_prop(Level *L, int type, int f, float x, float y, float z, float yaw, NhVec3 half, int flags);
void lv_index_props(Level *L); /* rebuild cell_props after props move */
int lv_add_light(Level *L, NhVec3 pos, NhColor color, float radius, int kind, int room, int circuit);

/* ---- queries ---------------------------------------------------------------- */

static inline bool lv_in_bounds(const Level *L, int f, int x, int z)
{
    return f >= 0 && f <= L->floors && x >= 0 && z >= 0 && x < L->w && z < L->d;
}
const LvCell *lv_cell(const Level *L, int f, int x, int z); /* NULL outside */
int lv_room_at(const Level *L, int f, int x, int z);
int lv_cell_of(float world); /* metres -> cell index */
int lv_floor_of(float y);    /* the floor whose slab is at or below y (+ a margin) */
float lv_floor_y(int f);
NhVec3 lv_cell_center(int f, int x, int z);

bool lv_edge_blocks_move(const Level *L, const LvEdge *e);
bool lv_edge_blocks_sight(const Level *L, const LvEdge *e, float height_in_storey);
/* Room on the far side of the edge on `side` of (x,z), 0 if none. */
int lv_neighbor_room(const Level *L, int f, int x, int z, int side);

/* Highest walkable surface at (x, z) that is not above feet_y + LV_STEP_UP.
 * Returns -1000 when there is nothing below (falls through the world). */
float lv_ground_height(const Level *L, float x, float z, float feet_y);
/* Stair ramp height at (x,z) on floor f's stair, or -1 if not on a stair. */
float lv_stair_height(const Level *L, int f, float x, float z);

/* Resolve a vertical cylinder (radius r, from y0 to y1) against walls, doors,
 * windows, rails and solid props. Moves pos out of penetration; returns true
 * if anything was hit. */
bool lv_collide_cylinder(const Level *L, NhVec3 *pos, float r, float y0, float y1);

/* Segment test for line of sight. Returns true if the segment from a to b is
 * blocked (walls, closed doors, floors, occluding props). */
bool lv_raycast(const Level *L, NhVec3 a, NhVec3 b, float *hit_t);

#endif
