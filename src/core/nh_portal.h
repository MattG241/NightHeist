/* Night Heist - room/portal visibility.
 *
 * Buildings are rooms joined by openings, so visibility is computed by walking
 * from the camera's room through portals (doorways, arches, windows, stair
 * holes), narrowing a screen-space rectangle at each step. Closed doors close
 * their portal; glass doors and windows always pass sight. */
#ifndef NH_PORTAL_H
#define NH_PORTAL_H

#include "nh_level.h"

#define NH_PORTALS_MAX 1024
#define NH_ROOM_PORTALS_MAX 48

enum { NH_PORTAL_DOOR, NH_PORTAL_ARCH, NH_PORTAL_WINDOW, NH_PORTAL_HOLE };

typedef struct NhPortal {
    uint8_t a, b;      /* rooms on either side (OUTSIDE stands in for void) */
    uint8_t kind;
    uint8_t door;      /* door index + 1 for door portals */
    NhVec3 c[4];       /* corners */
    NhVec3 n;          /* normal pointing from a to b */
} NhPortal;

typedef struct NhPortalSet {
    NhPortal p[NH_PORTALS_MAX];
    int n;
    uint16_t room_portals[LV_ROOMS_MAX][NH_ROOM_PORTALS_MAX];
    uint8_t room_count[LV_ROOMS_MAX];
} NhPortalSet;

void nh_portals_build(const Level *L, NhPortalSet *ps);

typedef struct NhVisRect {
    float x0, y0, x1, y1; /* NDC */
} NhVisRect;

/* viewproj: row-major 4x4, clip = viewproj * (x,y,z,1) with OpenGL-style
 * -w..w clip space. Writes visible[room] = 1 for every visible room and
 * returns how many there are. */
int nh_portals_visible(const Level *L, const NhPortalSet *ps, const float viewproj[16], NhVec3 eye, int cam_room,
                       uint8_t *visible);

/* Room containing a world point (eye), falling back to neighbouring floors. */
int nh_room_at_point(const Level *L, NhVec3 p);

#endif
