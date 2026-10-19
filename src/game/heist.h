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
#include "ui.h"
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
    ACT_LADDER_DOWN, ACT_HATCH_UP, ACT_HATCH_DOWN, ACT_VENT, ACT_EXTRACT, ACT_DROP_HEAVY, ACT_FREE, ACT_COUNT
} ActKind;

typedef struct Action {
    uint8_t kind;
    int16_t target;     /* door, entity, prop or guard index */
    float hold;         /* seconds C must be held, 0 = instant */
    char label[48];
    bool blocked;       /* shown greyed out (e.g. locked without a key) */
} Action;

typedef enum Screen { SCR_PLAY, SCR_MAP, SCR_OBJECTIVES, SCR_SUMMARY, SCR_PAUSE, SCR_BRIEF } Screen;
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

/* Everything that belongs to one player. In co-op there are two, and h->me
 * points at the one being updated or drawn. */
typedef struct HeistPlayer {
    int index;              /* 0 or 1 */
    NhPlayer player;
    bool torch_on;
    float battery;          /* 0..1 */
    NhVec3 torch_dir;       /* smoothed aim */
    float light_level;      /* 0..1 at the player, static + torch */
    char status[64];
    int status_ticks;
    int takedown;           /* guard the A button would take down, -1 if none */
    float caught_t;         /* > 0 while the "caught" sequence plays */
    int rumble_ticks;
    float heartbeat_t;
    Screen view;            /* SCR_PLAY, or this player's map / objectives */
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
    int gadget;             /* selected gadget */
    int16_t holding;        /* heavy loot entity in the hands, -1 */
    int16_t carrying;       /* guard being carried, -1 */
    bool thermal;
    float thermal_battery;
    bool was_torch, was_thermal;
    AiPlayerView pv;
    /* Co-op */
    bool cuffed;            /* caught: waits for the partner to free them */
    float cuffed_t;         /* seconds since cuffed */
    float aspect;           /* this player's view shape, for aiming */
    /* Guidance (guide.c) */
    NhVec3 guide[3];        /* the next corners of the route to the goal */
    int nguide;
    float guide_len;        /* metres along the route */
    int guide_floor;        /* floor the goal is on */
    float guide_t;          /* until the route is worked out again */
    bool left_start;        /* has moved away from the van at least once */
    int tip;                /* tip on screen (GuideTip + 1), 0 none */
    float tip_t;
    float lit_t;            /* seconds spent standing in bright light */
} HeistPlayer;

typedef struct Heist {
    World world;
    HeistPlayer pl[NH_MAX_PLAYERS];
    int nplayers;
    HeistPlayer *me;        /* the player being updated or drawn */
    uint64_t tick;
    int debug_view;         /* 0 off, 1 AI overlay, 2 AI overlay + thermal */
    /* Guards */
    NhNav *nav;
    AiWorld ai;
    GuardsVis gv;
    char subtitle[64];
    int subtitle_ticks;
    float alpha;            /* render interpolation, for the opaque hook */
    /* The job */
    bool generated;
    NhGenParams job;
    NhPlan plan;
    NhJob state;
    NhSecurity sec;
    char job_name[64];
    bool want_rebuild;      /* regenerate with `job` at the next frame */
    Screen screen;          /* SCR_PLAY, SCR_PAUSE or SCR_SUMMARY for everyone */
    NhJobResult result;
    NhRng rng;
    float drill_t;          /* safe drill running on drill_target */
    int drill_target;
    /* Gadgets and effects */
    Projectile proj[8];
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
    bool subtitles, summary_audio;
    int pause_sel, pause_page; /* page 0 menu, 1 controls, 2 confirm abandoning */
    int pause_owner;           /* the player who opened the pause menu */
    uint32_t tips_seen;        /* first-time tips shown (copied back to the campaign) */
} Heist;

/* Start a job: a generated building, or the hand-built test level if job is NULL. */
/* camp/offer may be NULL (debug jobs). difficulty: 0 easy, 1 normal, 2 hard. players: 1, or 2 for split-screen co-op. */
bool heist_start(Heist *h, const NhGenParams *job, const NhCampaign *camp, const NhJobOffer *offer, int difficulty,
                 int players, NhArena *level_arena, NhArena *scratch);
void heist_end(Heist *h);
/* in: one input per player (in[1] is ignored in single player). */
void heist_tick(Heist *h, const NhInput in[NH_MAX_PLAYERS], GameSettings *gs);
void heist_render(Heist *h, float alpha, const GameSettings *gs, float fps);
void heist_camera(const Heist *h, float alpha, NhMat34 *view, NhVec3 *eye);

/* Shared between heist.c, interact.c and gadgets.c */
void heist_status(Heist *h, const char *msg);     /* to the current player */
void heist_status_all(Heist *h, const char *msg); /* to everyone */
void heist_noise(Heist *h, NhVec3 pos, float radius, int kind);
void heist_alarm(Heist *h, NhVec3 where, const char *why);
void heist_set_power(Heist *h, int circuit, bool on);
UiMenu heist_pause_menu(const Heist *h);

#endif
