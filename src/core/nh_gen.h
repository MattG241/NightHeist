/* Night Heist - procedural building generator.
 *
 * Builds a whole job site into a Level: footprint, floors, a room plan per
 * building type (warehouse, office, bank, mansion, museum) in three sizes,
 * switchback stairwells, corridors, doors, windows, vents and a roof hatch,
 * room functions, set-piece rooms, furniture, lights on power circuits,
 * security (locks, keycards, keypad codes, cameras, lasers, alarm panels,
 * fuse boxes), loot and the main objective, guard rosters with patrol routes,
 * entry points and extraction zones.
 *
 * Every decision uses integer maths on the PCG32 stream, so the same seed
 * gives the same building on the Wii and on desktop. Floats appear only as
 * outputs (prop and light positions) and never feed back into decisions.
 *
 * Locks are placed constructively (each key goes somewhere reachable without
 * passing its own lock) and the result is then checked by a separate
 * reachability search; a failed check regenerates with the next attempt. */
#ifndef NH_GEN_H
#define NH_GEN_H

#include "nh_ai.h"
#include "nh_level.h"

typedef enum NhBuildingType { BT_WAREHOUSE, BT_OFFICE, BT_BANK, BT_MANSION, BT_MUSEUM, BT_COUNT } NhBuildingType;
typedef enum NhBuildingSize { BS_SMALL, BS_MEDIUM, BS_LARGE, BS_COUNT } NhBuildingSize;

extern const char *const nh_building_names[BT_COUNT];
extern const char *const nh_size_names[BS_COUNT];

typedef enum NhModifier {
    MOD_NIGHT_SHIFT,   /* more guards */
    MOD_SKELETON,      /* fewer guards */
    MOD_BLACKOUT,      /* half the building starts dark */
    MOD_LIGHTS_ON,     /* everything lit (cleaners in) */
    MOD_DOGS,          /* an extra dog team */
    MOD_THERMAL,       /* elites with thermal goggles */
    MOD_LOCKDOWN,      /* alarmed doors everywhere, harder locks */
    MOD_INSIDE_MAN,    /* all keypad codes known */
    MOD_FAST_POLICE,   /* police arrive in half the time */
    MOD_STORM,         /* rain covers footsteps (quieter) */
    MOD_BONUS_TARGET,  /* client pays double for the main target */
    MOD_NO_KILLS,      /* client insists: no takedowns */
    MOD_COUNT
} NhModifier;

typedef struct NhGenParams {
    uint32_t seed;
    uint8_t type, size;
    uint8_t security;          /* 0..4: lock strength, cameras, guard count */
    uint8_t extra_guards;
    uint16_t mods;             /* bit per NhModifier (night shift, blackout, dogs...) */
    uint64_t avoid_setpieces;  /* set pieces used recently: avoided when there's a choice */
    const uint32_t *avoid_layouts; /* fingerprints of recent buildings: never repeated */
    int navoid;
} NhGenParams;

typedef enum NhEntKind {
    ENT_NONE,
    ENT_LOOT,        /* pick-up valuables; ENTF_OBJECTIVE marks the job's target */
    ENT_KEYCARD,     /* key_id: which keycard */
    ENT_CODE,        /* note with a keypad code; key_id: which code */
    ENT_KEYPAD,      /* on the wall beside a code door; door: that door */
    ENT_CAMERA,      /* yaw: centre of its sweep */
    ENT_LASER,       /* pos: emitter, yaw: beam direction, value: length in cm */
    ENT_ALARM_PANEL,
    ENT_FUSE_BOX,    /* key_id: the circuit it cuts */
    ENT_CONSOLE,     /* security console (camera feeds) */
    ENT_SAFE,        /* lock_level in tier; contains loot */
    ENT_TERMINAL,    /* hackable computer */
    ENT_LADDER,      /* exterior ladder to the roof */
    ENT_HATCH,       /* roof hatch above a top-floor cell */
    ENT_PHONE,       /* desk phone (distraction) */
    ENT_COUNT
} NhEntKind;

enum {
    ENTF_OBJECTIVE = 1u << 0,
    ENTF_HEAVY = 1u << 1,     /* slows you down when carried */
    ENTF_SECURED = 1u << 2,   /* in a display case / behind glass */
};

typedef struct NhEntity {
    uint8_t kind, room, floor, key_id;
    uint8_t tier;   /* loot tier 0..4, safe/terminal difficulty 1..3 */
    uint8_t flags;
    uint8_t circuit;
    int16_t prop;   /* level prop, -1 if none */
    int16_t door;   /* door it relates to, -1 if none */
    uint16_t value; /* loot value in units of $10 */
    NhVec3 pos;
    float yaw;
} NhEntity;

#define GEN_ENT_MAX 384
#define GEN_GUARD_MAX 14
#define GEN_CODES 8

typedef struct NhGuardPlan {
    uint8_t arch;
    int8_t partner;
    NhVec3 pos;
    float yaw;
    AiPatrolPt route[AI_ROUTE_MAX];
    uint8_t nroute;
} NhGuardPlan;

typedef struct NhPlan {
    NhGenParams params;
    int attempts;
    uint32_t fingerprint;
    uint64_t setpieces;              /* bit i: set piece i used */
    int nsetpieces;
    NhEntity ent[GEN_ENT_MAX];
    int nent;
    NhGuardPlan guards[GEN_GUARD_MAX];
    int nguards;
    NhVec3 entry[4];
    float entry_yaw[4];
    int nentry;
    NhVec3 extract[2];
    int nextract;
    int objective;                   /* entity index of the main target */
    uint16_t codes[GEN_CODES];       /* keypad codes by code id (4 digits) */
    uint8_t security_circuit;        /* cameras, lasers, maglocks */
    int total_loot;                  /* sum of loot values (units of $10) */
} NhPlan;

/* Generate. Returns false only if no valid building came out of many
 * attempts (never observed; the tests run 10,000 seeds). */
bool nh_gen_building(Level *L, const NhGenParams *p, NhPlan *out);

/* Independent check of the lock-and-key graph: every room and the objective
 * reachable from an entry using only keys found on the way, and an
 * extraction point reachable. why gets a reason on failure. */
bool nh_gen_validate(const Level *L, const NhPlan *plan, char *why, int why_len);

/* Hash of the layout (room rectangles, functions, doors, stairs). */
uint32_t nh_gen_fingerprint(const Level *L);

/* Wall layout signature for distinctness tests: one bit per cell edge that
 * is not open, per floor. */
#define GEN_SIG_BYTES (LV_F_MAX * LV_D_MAX * LV_W_MAX * 2 / 8)
void nh_gen_signature(const Level *L, uint8_t sig[GEN_SIG_BYTES]);
/* 0 = identical walls, 1 = nothing in common (Jaccard distance). */
float nh_gen_signature_distance(const uint8_t *a, const uint8_t *b);

/* Dressing statistics by room function (rooms dressed, needed a retry, left bare). */
extern uint32_t nh_gen_stat_dressed[], nh_gen_stat_bare[], nh_gen_stat_retry[];

/* Set pieces, for the job board and the anti-staleness history. */
int nh_gen_setpiece_count(void);
const char *nh_gen_setpiece_name(int i);

#endif
