/* Night Heist - building security: cameras, lasers, alarmed doors and power.
 * Pure logic over the level, the heist plan and the AI world. */
#ifndef NH_SECURITY_H
#define NH_SECURITY_H

#include "nh_ai.h"
#include "nh_gen.h"

#define SEC_CAM_MAX 24
#define SEC_LASER_MAX 64

typedef struct SecCamera {
    int16_t ent;
    uint8_t circuit, room;
    NhVec3 pos;
    float base_yaw, yaw, t;
    float sus;        /* 0..1 detection */
    float jam_t;      /* seconds jammed */
    float vis;        /* last visibility of the player */
} SecCamera;

typedef struct SecLaser {
    int16_t ent;
    uint8_t circuit, room, mirrored;
    NhVec3 a, b;
} SecLaser;

typedef struct NhSecurity {
    SecCamera cam[SEC_CAM_MAX];
    int ncam;
    SecLaser las[SEC_LASER_MAX];
    int nlas;
    uint8_t security_circuit;
    float emp_t, emp_r;
    NhVec3 emp_pos;
    int entry_door;       /* alarmed door counting down, -1 */
    float entry_t;
    bool disarmed;        /* alarm panel disarmed: alarmed doors stay quiet */
    float max_sus;        /* highest camera detection, for the HUD */
} NhSecurity;

enum { SEC_EV_CAMERA = 1u << 0, SEC_EV_LASER = 1u << 1, SEC_EV_ENTRY_TIMEOUT = 1u << 2 };

void sec_init(NhSecurity *s, const Level *L, const NhPlan *plan);
/* Returns SEC_EV_* bits; *where gets the position the event refers to. */
uint32_t sec_tick(NhSecurity *s, const Level *L, const AiWorld *ai, const AiPlayerView *pv, float dt, NhVec3 *where);
bool sec_camera_live(const NhSecurity *s, const Level *L, int i);
bool sec_laser_live(const NhSecurity *s, const Level *L, int i);
void sec_door_opened(NhSecurity *s, const Level *L, int door);
void sec_disarm(NhSecurity *s);
void sec_emp(NhSecurity *s, NhVec3 pos, float radius, float seconds);
/* Distance from the segment to a vertical capsule (the player). */
float sec_segment_capsule(NhVec3 a, NhVec3 b, NhVec3 feet, float height);

#endif
