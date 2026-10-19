/* Night Heist - an active job: the world, the player and torch, guards,
 * security, the player's interactions and gadgets, and the job's state from
 * the street to the getaway (or the back of a police car). */
#ifndef NH_HEIST_H
#define NH_HEIST_H

#include "core/nh_ai.h"
#include "core/nh_campaign.h"
#include "core/nh_gen.h"
#include "core/nh_job.h"
#include "core/nh_minigame.h"
#include "core/nh_nav.h"
#include "core/nh_player.h"
#include "core/nh_security.h"
#include "guards.h"
#include "platform/platform.h"
#include "world.h"

#define HEIST_FOVY 62.0f

typedef struct GameSettings {
    NhControlSettings controls;
    bool show_debug;
    bool rumble;
    bool subtitles;
    float music_volume, sfx_volume;
} GameSettings;

/* What C would do right now. */
typedef enum ActKind {
    ACT_NOTHING, ACT_DOOR, ACT_LOOT, ACT_ITEM, ACT_KEYPAD, ACT_SAFE, ACT_TERMINAL, ACT_CONSOLE, ACT_FUSE,
    ACT_PANEL, ACT_PHONE, ACT_PHOTO, ACT_HIDE, ACT_BODY_PICK, ACT_BODY_DROP, ACT_BODY_STASH, ACT_LADDER_UP,
    ACT_LADDER_DOWN, ACT_HATCH_UP, ACT_HATCH_DOWN, ACT_VENT, ACT_EXTRACT, ACT_DROP_HEAVY, ACT_COUNT
} ActKind;

typedef struct Action {
    uint8_t kind;
    int16_t target;     /* door, entity, prop or guard index */
    float hold;         /* seconds C must be held, 0 = instant */
    char label[48];
    bool blocked;       /* shown greyed out (e.g. locked without a key) */
} Action;

typedef enum Screen { SCR_PLAY, SCR_MAP, SCR_OBJECTIVES, SCR_SUMMARY } Screen;
typedef enum Minigame { MG_KIND_NONE, MG_KIND_LOCK, MG_KIND_SAFE, MG_KIND_HACK } MinigameKind;

typedef struct Projectile {
    uint8_t active, gadget;
    NhVec3 pos, vel;
    float t;          /* age; for landed effects, time left */
    bool landed;
} Projectile;

/* What the crew, upgrades and modifiers change on a job. */
typedef struct HeistPerks {
    int pick_bonus, hack_bonus;   /* fewer pins / signal layers */
    bool silent_takedowns, map_guards, map_objectives, noisy_slips;
    float carry_speed, torch_life, torch_fov, thermal_life, dart_time, noise_scale;
} HeistPerks;

typedef struct Heist {
    World world;
    NhPlayer player;
    bool torch_on;
    float battery;          /* 0..1 */
    NhVec3 torch_dir;       /* smoothed aim */
    float light_level;      /* 0..1 at the player, static + torch */
    uint64_t tick;
    int debug_view;         /* 0 off, 1 AI overlay, 2 AI overlay + thermal */
    char status[64];
    int status_ticks;
    /* Guards */
    NhNav *nav;
    AiWorld ai;
    GuardsVis gv;
    AiPlayerView pv;
    int takedown;           /* guard the A button would take down, -1 if none */
    float caught_t;         /* > 0 while the "caught" sequence plays */
    char subtitle[64];
    int subtitle_ticks;
    int rumble_ticks;
    float heartbeat_t;
    float alpha;            /* render interpolation, for the opaque hook */
    /* The job */
    bool generated;
    NhGenParams job;
    NhPlan plan;
    NhJob state;
    NhSecurity sec;
    char job_name[64];
    bool want_rebuild;      /* regenerate with `job` at the next frame */
    Screen screen;
    NhJobResult result;
    /* Interaction */
    Action act;
    float hold_t;           /* progress on the current hold */
    bool c_latch;           /* wait for C to be released before the next action */
    int hide_prop;          /* hiding in this prop, -1 */
    NhVec3 hide_return;     /* where to step out */
    MinigameKind mg;
    int mg_target;          /* door or entity */
    NhLockpick lp;
    NhSafecrack sc;
    NhHack hk;
    NhRng rng;
    float drill_t;          /* safe drill running on mg_target */
    int drill_target;
    /* Gadgets and effects */
    Projectile proj[8];
    bool thermal;
    float thermal_battery;
    float cams_looped;      /* camera feeds looped from the console (seconds) */
    float police_wave_t;
    uint8_t explored[LV_ROOMS_MAX];
    uint8_t rebake[LV_ROOMS_MAX];
    float fuse_restore_t[16];
    bool police_spawned;
    /* Campaign */
    bool campaign;          /* started from the job board */
    bool done;              /* summary confirmed: back to the safehouse */
    NhJobOffer offer;
    HeistPerks perks;
    /* Audio: loop handles and what the sound update tracks between ticks */
    int snd_alarm, snd_siren, snd_drill, snd_phone, snd_proj[8];
    float phone_t;            /* a desk phone ringing (dialer gadget) */
    NhVec3 phone_pos;
    float guard_step[AI_MAX_GUARDS];
    NhVec3 guard_prev[AI_MAX_GUARDS];
    float cam_beep_t, entry_beep_t;
    bool was_torch, was_thermal, subtitles, summary_audio;
} Heist;

/* Start a job: a generated building, or the hand-built test level if job is NULL. */
/* camp/offer may be NULL (debug jobs). difficulty: 0 easy, 1 normal, 2 hard. */
bool heist_start(Heist *h, const NhGenParams *job, const NhCampaign *camp, const NhJobOffer *offer, int difficulty,
                 NhArena *level_arena, NhArena *scratch);
void heist_end(Heist *h);
void heist_tick(Heist *h, const NhInput *in, GameSettings *gs);
void heist_render(Heist *h, float alpha, const GameSettings *gs, float fps);
void heist_camera(const Heist *h, float alpha, NhMat34 *view, NhVec3 *eye);

/* Shared between heist.c, interact.c and gadgets.c */
void heist_status(Heist *h, const char *msg);
void heist_noise(Heist *h, NhVec3 pos, float radius, int kind);
void heist_alarm(Heist *h, NhVec3 where, const char *why);
void heist_set_power(Heist *h, int circuit, bool on);

#endif
