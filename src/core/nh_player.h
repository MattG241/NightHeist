/* Night Heist - first-person controller (pure simulation).
 *
 * Look/aim uses the bounding-box scheme from The Conduit: the cursor (and the
 * torch, which follows it) moves freely inside a central box; when the pointer
 * pushes past the box edge the camera turns, faster the further out it is.
 * When IR tracking is lost the cursor holds its last position, so the torch
 * keeps its direction and the camera stops turning. */
#ifndef NH_PLAYER_H
#define NH_PLAYER_H

#include "nh_level.h"

typedef struct NhControlSettings {
    float ir_smoothing;   /* 0..1, higher = smoother pointer (more lag) */
    float box_w, box_h;   /* turn box as a fraction of the screen, 0.1..0.9 */
    float turn_speed;     /* max yaw speed in rad/s at the screen edge */
    bool crouch_toggle;   /* Z toggles instead of hold */
    bool invert_y;
} NhControlSettings;

void nh_controls_default(NhControlSettings *s);

/* Controller input for one tick, already mapped from NhInput by the game. */
typedef struct NhPlayerInput {
    bool ir_valid;
    float ir_x, ir_y;       /* 640x480 pointer */
    float move_x, move_y;   /* stick, -1..1 (+y forward) */
    float lean;             /* -1..1 */
    bool crouch_held, crouch_pressed;
    bool frozen;            /* in a minigame/menu: no movement, no turning */
} NhPlayerInput;

typedef enum NhNoiseKind {
    NH_NOISE_FOOTSTEP,
    NH_NOISE_DOOR,
    NH_NOISE_GLASS,
    NH_NOISE_THROW,
    NH_NOISE_GADGET,
    NH_NOISE_TAKEDOWN,
    NH_NOISE_ALARM,
    NH_NOISE_VOICE,
    NH_NOISE_PHONE,
} NhNoiseKind;

typedef struct NhNoise {
    NhVec3 pos;
    float radius;  /* metres at which it's just audible in the open */
    uint8_t kind;
    uint8_t surface;
    uint8_t from_player;
} NhNoise;

#define NH_PLAYER_RADIUS 0.3f
#define NH_PLAYER_HEIGHT 1.75f
#define NH_PLAYER_CROUCH_HEIGHT 1.1f
#define NH_VENT_HEIGHT 0.5f

typedef enum NhMoveMode { NH_MOVE_WALK, NH_MOVE_VENT } NhMoveMode;

typedef struct NhPlayer {
    NhVec3 pos, prev_pos;  /* feet */
    float vel_y;
    float yaw, pitch, prev_yaw, prev_pitch;
    float cursor_x, cursor_y; /* smoothed pointer, screen pixels */
    float crouch;          /* 0..1 */
    bool crouch_latched;
    float lean, prev_lean; /* -1..1, collision limited */
    float speed;           /* horizontal speed this tick, m/s */
    float step_phase;      /* metres walked since the last footstep */
    bool grounded;
    NhMoveMode mode;
    int floor;
    NhNoise noises[4];
    int nnoise;
} NhPlayer;

void nh_player_init(NhPlayer *p, NhVec3 feet, float yaw);
void nh_player_tick(NhPlayer *p, const Level *L, const NhPlayerInput *in, const NhControlSettings *cs,
                    float aspect, float fovy_deg);
float nh_player_eye_height(const NhPlayer *p);
/* Interpolated eye position including lean. */
NhVec3 nh_player_eye(const NhPlayer *p, const Level *L, float alpha);
/* World-space direction through the cursor for the current view. */
NhVec3 nh_player_aim_dir(const NhPlayer *p, float aspect, float fovy_deg);
/* Turn rate (-1..1 per axis) the bounding box currently applies; for the HUD. */
void nh_player_turn_amount(const NhPlayer *p, const NhControlSettings *cs, float *tx, float *ty);

#endif
