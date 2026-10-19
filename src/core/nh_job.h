/* Night Heist - the state of one job: objectives, inventory, gadgets,
 * statistics, the alarm and police clock, and the end-of-job rating.
 * Pure logic driven by events from the game, so it is unit-testable. */
#ifndef NH_JOB_H
#define NH_JOB_H

#include "nh_gen.h"

typedef enum NhGadget {
    GD_LOCKPICK,   /* picks locks (always carried) */
    GD_COIN,       /* thrown: a small noise where it lands */
    GD_NOISEMAKER, /* thrown: a loud beeping decoy */
    GD_SMOKE,      /* thrown: a cloud guards and cameras can't see through */
    GD_TASER,      /* close range knockout, noisy */
    GD_DART,       /* tranquiliser dart, silent, takes a few seconds */
    GD_JAMMER,     /* blinds one camera for 20 s */
    GD_EMP,        /* thrown: cameras, lasers and lights out nearby for 15 s */
    GD_CUTTER,     /* cut a window quietly */
    GD_CLONER,     /* copy a guard's keycard from behind */
    GD_THERMAL,    /* see warm bodies through walls (battery) */
    GD_MIRROR,     /* bend one laser beam out of the way */
    GD_DRILL,      /* drill a safe instead of cracking it: slow and loud */
    GD_DIALER,     /* ring a desk phone to pull a guard over */
    GD_COUNT
} NhGadget;

typedef struct NhGadgetInfo {
    const char *name;
    const char *hint;
    uint8_t charges;   /* default loadout; 255 = unlimited */
    uint8_t thrown;
    float range;       /* metres */
} NhGadgetInfo;

extern const NhGadgetInfo nh_gadgets[GD_COUNT];

typedef enum NhObjType {
    OBJ_STEAL,      /* take the target and get out */
    OBJ_HACK,       /* download from a terminal */
    OBJ_SAFE,       /* open a particular safe */
    OBJ_PHOTO,      /* photograph documents */
    OBJ_BUG,        /* plant a bug on a desk phone */
    OBJ_WIPE,       /* wipe a server terminal */
    OBJ_LOOT,       /* bag at least `target` worth */
    OBJ_GHOST,      /* never be spotted */
    OBJ_NO_ALARM,   /* the alarm never sounds */
    OBJ_NO_KO,      /* no takedowns */
    OBJ_TIME,       /* out within `target` seconds */
    OBJ_COUNT
} NhObjType;

extern const char *const nh_obj_names[OBJ_COUNT];

typedef struct NhObjective {
    uint8_t type, optional, done, failed;
    int16_t ent;        /* entity it concerns, -1 */
    uint32_t target;    /* value (units of $10) or seconds */
    uint32_t bonus;     /* paid on completion (units of $10) */
    char text[56];
} NhObjective;

#define JOB_OBJ_MAX 6

typedef enum NhJobEvent {
    JE_LOOT,          /* ent, value */
    JE_HACKED,        /* ent */
    JE_SAFE_OPEN,     /* ent */
    JE_PHOTO,         /* ent */
    JE_BUG,           /* ent */
    JE_WIPED,         /* ent */
    JE_SPOTTED,
    JE_TAKEDOWN,
    JE_ALARM,
    JE_BODY_FOUND,
    JE_CAMERA_SPOTTED,
    JE_CAUGHT,
    JE_EXTRACT,
    JE_ABORT,
} NhJobEvent;

typedef enum NhOutcome { JOB_RUNNING, JOB_SUCCESS, JOB_PARTIAL, JOB_ABORTED, JOB_BUSTED } NhOutcome;

typedef struct NhJob {
    NhObjective obj[JOB_OBJ_MAX];
    int nobj;
    /* inventory */
    uint32_t keycards;    /* bit per key id */
    uint32_t codes;       /* bit per code id known */
    uint8_t charges[GD_COUNT];
    int gadget;           /* selected */
    int bag_value;        /* units of $10 */
    int bag_items;
    int bag_cap;          /* small items */
    int16_t holding;      /* heavy loot entity carried in the hands, -1 */
    int16_t carrying;     /* guard being carried, -1 */
    uint8_t taken[GEN_ENT_MAX / 8]; /* entities picked up / used */
    /* statistics */
    float time;
    int spotted, takedowns, alarms, bodies_found, camera_spots, safes, hacks, locks_picked;
    /* alarm and police */
    bool alarm;
    float police_eta;     /* seconds until the police arrive, < 0 none */
    bool police_here;
    float police_delay;   /* how long they take, set per district/heat */
    NhOutcome outcome;
} NhJob;

typedef struct NhJobResult {
    NhOutcome outcome;
    int loot;          /* units of $10 */
    int bonus;
    int payout;
    int stealth;       /* 0..100 */
    char grade;        /* S A B C D, or F for busted */
    int heat;          /* heat gained */
    int objectives_done, objectives_total;
    bool ghost;
} NhJobResult;

/* Objectives come from the plan: the main target plus optional ones that
 * fit what the generator put in the building. */
void job_init(NhJob *j, const Level *L, const NhPlan *plan, uint32_t seed);
/* Short name of a loot entity ("the gold"). */
const char *job_loot_name(const Level *L, const NhPlan *plan, int ent);
void job_tick(NhJob *j, float dt);
void job_event(NhJob *j, const NhPlan *plan, NhJobEvent ev, int ent, int value);
bool job_taken(const NhJob *j, int ent);
void job_mark_taken(NhJob *j, int ent);
/* Can the player leave with the main objective done? */
bool job_main_done(const NhJob *j);
void job_result(const NhJob *j, NhJobResult *out);

#endif
