/* Night Heist - guard AI (pure simulation, deterministic).
 *
 * States: PATROL (with IDLE actions at patrol points) -> SUSPICIOUS ->
 * INVESTIGATE -> ALERT (chase) -> SEARCH -> RETURN -> PATROL, plus
 * DISTRACTED, STATION (camera operators), FLEE (civilians reporting you),
 * KO and CARRIED. Guards perceive the player by sight (vision cone, distance,
 * the player's light level, how much of the player is visible, stance and
 * speed), notice the torch beam on surfaces, hear noise attenuated through
 * walls and closed doors, and notice changes (doors left open, missing loot,
 * bodies, lights off). Alerted guards radio the others; enough alerts raise
 * the building alarm. Archetypes change behaviour, not just numbers. */
#ifndef NH_AI_H
#define NH_AI_H

#include "nh_nav.h"
#include "nh_player.h"
#include "nh_rng.h"

#define AI_MAX_GUARDS 16
#define AI_ROUTE_MAX 12
#define AI_SEARCH_MAX 14
#define AI_ANOMALY_MAX 64
#define AI_EVENTS_MAX 48

typedef enum AiArch {
    ARCH_ROOKIE,     /* easily distracted, slow to suspect, gives up quickly */
    ARCH_GUARD,      /* standard */
    ARCH_VETERAN,    /* checks hiding spots, searches thoroughly, hard to distract */
    ARCH_LAZY,       /* skips patrol points, naps, torch off */
    ARCH_HANDLER,    /* walks a dog */
    ARCH_CAMOP,      /* sits at the security console watching cameras */
    ARCH_SUPERVISOR, /* runs radio check-ins; a missed check-in raises the alarm */
    ARCH_ELITE,      /* thermal goggles, flanks in pairs */
    ARCH_JANITOR,    /* civilian: runs to report you */
    ARCH_POLICE,     /* arrive after the alarm, sweep aggressively */
    ARCH_DOG,        /* smells you in the dark, barks, can't open doors */
    ARCH_COUNT
} AiArch;

typedef struct AiArchInfo {
    const char *name;
    float walk, run;      /* m/s */
    float view_range;     /* metres in full light */
    float fov_deg;        /* full horizontal field of view */
    float hearing;        /* noise radius multiplier */
    float gain;           /* suspicion gain multiplier */
    float decay;          /* suspicion decay per second */
    float distractible;   /* 0..1: follows distractions without getting suspicious */
    float search_time;    /* seconds spent searching */
    uint8_t checks_hiding, radio, torch, thermal, civilian, opens_doors;
} AiArchInfo;

extern const AiArchInfo nh_ai_arch[ARCH_COUNT];

typedef enum AiState {
    AS_PATROL, AS_IDLE, AS_SUSPICIOUS, AS_INVESTIGATE, AS_ALERT, AS_SEARCH, AS_RETURN,
    AS_DISTRACTED, AS_STATION, AS_FLEE, AS_KO, AS_CARRIED, AS_COUNT
} AiState;

extern const char *const nh_ai_state_names[AS_COUNT];

typedef enum AiAction { ACT_NONE, ACT_LOOK, ACT_COFFEE, ACT_SMOKE, ACT_CHAT, ACT_NAP, ACT_RADIO, ACT_WAIT } AiAction;

typedef enum AiAnim {
    ANIM_IDLE, ANIM_WALK, ANIM_WALK_TORCH, ANIM_RUN, ANIM_LOOK, ANIM_SUSPICIOUS, ANIM_ALERT, ANIM_SEARCH,
    ANIM_RADIO, ANIM_SMOKE, ANIM_CHAT, ANIM_NAP, ANIM_GRAB, ANIM_KO, ANIM_DRAGGED, ANIM_COUNT
} AiAnim;

typedef struct AiPatrolPt {
    NhVec3 pos;
    float yaw;
    uint8_t action;
    float wait; /* seconds */
} AiPatrolPt;

typedef enum AiBark {
    BARK_NONE, BARK_HUH, BARK_WHO, BARK_THERE, BARK_LOST, BARK_NOTHING, BARK_RADIO_SPOTTED, BARK_MAN_DOWN,
    BARK_CHECKIN, BARK_CHECKIN_OK, BARK_DOOR, BARK_LOOT, BARK_LIGHTS, BARK_CAMERA, BARK_NOISE, BARK_CHAT,
    BARK_FOUND, BARK_GIVEUP, BARK_DOG, BARK_SCREAM, BARK_POLICE, BARK_COUNT
} AiBark;

typedef struct AiGuard {
    uint8_t active, arch, state, prev_state, anim, action;
    int8_t partner;         /* elite pair / dog-handler link, -1 if none */
    NhVec3 pos;
    float yaw, head_yaw, speed;
    int floor;
    /* patrol */
    AiPatrolPt route[AI_ROUTE_MAX];
    uint8_t nroute, route_i;
    float wait_t;
    /* movement */
    NhPath path;
    uint8_t path_i;
    NhVec3 goal;
    bool has_goal, run;
    float repath_t, stuck_t;
    NhVec3 last_pos;
    /* perception */
    float suspicion, awareness, vis;  /* vis: last visibility of the player 0..1 */
    NhVec3 lkp;
    float lkp_age;
    bool saw_player;
    NhVec3 stim;               /* where the guard is looking/heading because of a stimulus */
    /* search */
    NhVec3 search[AI_SEARCH_MAX];
    int16_t search_prop[AI_SEARCH_MAX];
    uint8_t nsearch, search_i;
    float state_t, search_t;
    /* misc */
    bool torch_on, asleep;
    float radio_t;             /* pending radio call countdown */
    float bark_cd;
    float checkin_due;         /* supervisor: time to next check-in */
    uint8_t missed_checkins;
    /* movement bookkeeping */
    int16_t door_wait;         /* door index + 1 the guard is waiting to open */
    int16_t door_close;        /* door index + 1 to close behind once through */
    NhVec3 door_close_pos;
    uint8_t repaths;
    bool arrived, path_failed;
    bool room_lit;
    float lit_t;               /* time until the lit-room check is refreshed */
    float seen_t;              /* seconds since the player was last visible */
    int8_t reported_by;        /* janitor fleeing to this guard */
    int16_t anomaly;           /* anomaly being investigated + 1 */
    uint8_t ret_state;         /* state to resume after SUSPICIOUS */
} AiGuard;

typedef enum AiAnomalyKind {
    AN_DOOR_OPEN, AN_LOOT_MISSING, AN_BODY, AN_CAMERA_OFF, AN_LIGHTS_OFF, AN_SAFE_OPEN, AN_GLASS_BROKEN,
    AN_VENT_OPEN, AN_COUNT
} AiAnomalyKind;

typedef struct AiAnomaly {
    NhVec3 pos;
    uint8_t kind, active;
    uint16_t id;
    uint32_t seen_mask; /* guards that have reacted to it */
} AiAnomaly;

typedef enum AiEventType {
    EV_BARK, EV_RADIO, EV_ALARM, EV_CAUGHT, EV_SPOTTED, EV_DOOR_OPENED, EV_FOUND_HIDING, EV_CHECKIN_MISSED,
    EV_DOG_BARK, EV_TAKEDOWN
} AiEventType;

typedef struct AiEvent {
    uint8_t type, guard, arg;
    NhVec3 pos;
} AiEvent;

/* What the guards can know about the player this tick (filled by the game). */
typedef struct AiPlayerView {
    NhVec3 pos;        /* feet */
    NhVec3 head;       /* eye position including lean */
    float height;      /* current standing height */
    float speed;       /* m/s */
    float light;       /* 0..1 static + torch light on the player */
    bool crouched, torch_on, in_vent, disguised;
    int hiding_prop;   /* prop index the player is hidden in, -1 if not */
    bool beam_valid;
    NhVec3 beam_hit;   /* where the torch beam lands */
    const NhNoise *noises;
    int nnoise;
} AiPlayerView;

typedef struct AiWorld {
    Level *L;
    NhNav *nav;
    NhRng rng;
    AiGuard g[AI_MAX_GUARDS];
    int n;
    AiAnomaly an[AI_ANOMALY_MAX];
    int nan;
    AiEvent ev[AI_EVENTS_MAX];
    int nev;
    uint32_t tick;
    int alerts;          /* radio alerts so far */
    int alarm_threshold; /* alerts that trip the building alarm */
    bool alarm;
    float difficulty;    /* 0.7 easy .. 1.4 brutal: scales gain and ranges */
    float max_suspicion; /* for the HUD and heartbeat rumble */
    float checkin_period;
    float checkin_t;
    NhNoise pending[8];  /* distractions queued for the next tick */
    int npending;
    int caught;          /* guard index + 1 that caught the player this heist */
    float player_light;  /* light on the player last tick, including guard torches */
} AiWorld;

void ai_init(AiWorld *w, Level *L, NhNav *nav, uint32_t seed);
int ai_add_guard(AiWorld *w, int arch, NhVec3 pos, float yaw, const AiPatrolPt *route, int nroute);
void ai_tick(AiWorld *w, const AiPlayerView *pv);
/* Events raised by the last tick (barks, alarms, caught...). */
static inline int ai_event_count(const AiWorld *w) { return w->nev; }

bool ai_can_takedown(const AiWorld *w, int g, NhVec3 player, float *dist);
void ai_knockout(AiWorld *w, int g);
int ai_add_anomaly(AiWorld *w, int kind, NhVec3 pos, int id);
void ai_clear_anomaly(AiWorld *w, int kind, int id);
/* A noise that isn't the player (thrown object, phone, noisemaker, alarm). */
void ai_distraction(AiWorld *w, NhVec3 pos, float radius, int kind);
/* Mid-heist event: send one guard somewhere (phone call, delivery). */
void ai_send_guard(AiWorld *w, int g, NhVec3 pos, float seconds);
/* Building alarm raised by something else (camera operator, panel, laser). */
void ai_raise_alarm(AiWorld *w, NhVec3 where);

/* World-space eye and view direction of a guard (for rendering cones and torches). */
NhVec3 ai_guard_eye(const AiGuard *g);
NhVec3 ai_guard_dir(const AiGuard *g);
/* Count of walls/closed doors/floors between two points (noise attenuation). */
int ai_count_blockers(const Level *L, NhVec3 a, NhVec3 b);
/* Visibility (0..1) of the player from a guard, ignoring suspicion: the
 * product of cone, distance, light and occlusion terms. Exposed for tests and
 * the debug view. */
float ai_visibility(const AiWorld *w, const AiGuard *g, const AiPlayerView *pv, float *dist);
/* Suspicion gained per second at a given visibility (pure, for tests). */
float ai_gain_rate(const AiWorld *w, const AiGuard *g, const AiPlayerView *pv, float vis, float dist);

#endif
