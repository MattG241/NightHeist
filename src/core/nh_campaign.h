/* Night Heist - the campaign: money, reputation, heat, four districts and a
 * post-game, a crew of six, upgrades, the fence and its loot sets, the job
 * board and the history that keeps jobs from going stale. Pure data and
 * rules (no platform calls); saved by nh_save. */
#ifndef NH_CAMPAIGN_H
#define NH_CAMPAIGN_H

#include "nh_job.h"

typedef enum NhDistrict { DI_DOCKS, DI_MIDTOWN, DI_OLDTOWN, DI_HEIGHTS, DI_COUNT } NhDistrict;

typedef struct NhDistrictInfo {
    const char *name;
    const char *blurb;
    uint8_t types[3];      /* building types offered (NhBuildingType), 255 = none */
    uint8_t base_security;
    int16_t unlock_rep;    /* reputation needed */
    uint16_t pay_mult;     /* % */
} NhDistrictInfo;

extern const NhDistrictInfo nh_districts[DI_COUNT];

extern const char *const nh_mod_names[MOD_COUNT];
extern const char *const nh_mod_desc[MOD_COUNT];

typedef enum NhCrewRole { CR_HACKER, CR_LOCKSMITH, CR_DRIVER, CR_MUSCLE, CR_LOOKOUT, CR_FACE, CR_COUNT } NhCrewRole;

typedef struct NhCrewInfo {
    const char *name;
    const char *role;
    const char *perk;
    const char *flaw;
    const char *loyalty_job;
    uint8_t cut;          /* % of the take */
    int16_t hire_rep;     /* reputation needed to hire */
} NhCrewInfo;

extern const NhCrewInfo nh_crew[CR_COUNT];

typedef enum NhUpgrade {
    UP_TORCH,       /* battery + brighter */
    UP_BAG,         /* +4 slots per level */
    UP_SHOES,       /* quieter footsteps */
    UP_PICKS,       /* fewer pins */
    UP_GADGETS,     /* +1 charge on everything per level */
    UP_THERMAL,     /* longer battery */
    UP_DARTS,       /* faster knockout */
    UP_SCANNER,     /* objectives and cameras on the map */
    UP_COUNT
} NhUpgrade;

typedef struct NhUpgradeInfo {
    const char *name;
    const char *desc;
    uint8_t max_level;
    uint16_t cost[3];     /* units of $10 per level */
} NhUpgradeInfo;

extern const NhUpgradeInfo nh_upgrades[UP_COUNT];

/* Loot sets the fence pays extra for. */
typedef enum NhLootSet { LS_PAINTINGS, LS_JEWELS, LS_WATCHES, LS_DATA, LS_GOLD, LS_COUNT } NhLootSet;

typedef struct NhLootSetInfo {
    const char *name;
    uint8_t need;
    uint16_t bonus; /* units of $10 */
} NhLootSetInfo;

extern const NhLootSetInfo nh_loot_sets[LS_COUNT];

#define JOB_BOARD_MAX 5
#define HISTORY_MAX 24

typedef struct NhJobOffer {
    uint32_t seed;
    uint8_t district, type, size, security;
    uint16_t mods;          /* bit per NhModifier */
    uint8_t loyalty_crew;   /* crew member + 1 if this is their loyalty job */
    uint8_t finale;         /* district finale */
    int32_t est_payout;     /* units of $10, rough */
    char name[40];
} NhJobOffer;

typedef struct NhCampaign {
    uint32_t seed;
    int32_t money;               /* units of $10 */
    int32_t reputation;
    uint8_t heat[DI_COUNT];      /* 0..100 */
    uint8_t district_done[DI_COUNT]; /* finale completed */
    uint8_t postgame;
    uint8_t crew_hired;          /* bit per member */
    uint8_t crew_loyalty[CR_COUNT]; /* 0..100 */
    uint8_t crew_perk2;          /* bit: loyalty job done, perk upgraded */
    uint8_t upgrade[UP_COUNT];
    uint8_t set_count[LS_COUNT]; /* items held towards each set */
    uint8_t sets_done[LS_COUNT];
    uint32_t history_fp[HISTORY_MAX]; /* recent layout fingerprints */
    uint64_t sp_hist[3];         /* set pieces of the last three jobs */
    uint8_t history_n;
    uint16_t jobs_done, jobs_failed, ghosts;
    uint8_t district_jobs[DI_COUNT];
    uint32_t total_earned;
    float play_time;
    NhJobOffer board[JOB_BOARD_MAX];
    uint8_t board_n;
    uint32_t board_gen;          /* how many boards have been made */
    uint32_t tips_seen;          /* first-time tips already shown (bit per tip) */
    uint8_t crew_selected;       /* bit per member brought on the next job */
} NhCampaign;

void camp_new(NhCampaign *c, uint32_t seed);
bool camp_district_open(const NhCampaign *c, int d);
/* Fill the job board for the current progress. */
void camp_make_board(NhCampaign *c);
/* Generator parameters for an offer (modifiers applied). */
void camp_offer_params(const NhCampaign *c, const NhJobOffer *o, NhGenParams *gp);
/* Apply the crew and upgrades to a fresh job state. */
void camp_prepare_job(const NhCampaign *c, const NhJobOffer *o, NhJob *j);
/* Book a finished job: pay, cuts, reputation, heat, loot sets, loyalty, history. */
typedef struct NhPayday {
    int32_t gross, crew_cut, set_bonus, net;
    int rep, heat;
    int set_completed;           /* LootSet + 1 */
    bool district_finished, unlocked_district;
    char news[64];               /* what this job unlocked, "" if nothing */
} NhPayday;
void camp_finish_job(NhCampaign *c, const NhJobOffer *o, const NhJobResult *r, const NhJob *j, const Level *L,
                     const NhPlan *plan, uint32_t fingerprint, uint64_t setpieces, NhPayday *out);
/* Upgrades and hiring. */
int32_t camp_upgrade_cost(const NhCampaign *c, int up);
bool camp_buy_upgrade(NhCampaign *c, int up);
bool camp_upgrade_available(const NhCampaign *c, int up); /* its gadget is in the kit */
int32_t camp_hire_cost(const NhCampaign *c, int member);
bool camp_hire(NhCampaign *c, int member);
/* Lying low cools heat everywhere at a price. */
bool camp_lie_low(NhCampaign *c);
int camp_level(const NhCampaign *c); /* 0..4: districts finished */
bool camp_crew_on(const NhCampaign *c, int member); /* hired and selected for this job */
bool camp_crew_perk2(const NhCampaign *c, int member);
/* The novelty schedule: gadgets and job modifiers arrive as the career goes
 * on (jobs finished or failed), one new thing every job or two. */
int camp_career(const NhCampaign *c);
bool camp_gadget_unlocked(const NhCampaign *c, int gadget);
bool camp_mod_unlocked(const NhCampaign *c, int mod);
/* Writes what arrives at career step `career` ("" if nothing); returns its length. */
int camp_unlock_news(int career, char *buf, int len);

#endif
