/* Night Heist - the safehouse between jobs: the job board and planning,
 * crew, gear upgrades, the fence (loot sets, heat, lying low) and settings.
 * Driven by the pointer (A to click) or the D-pad. */
#ifndef NH_HIDEOUT_H
#define NH_HIDEOUT_H

#include "core/nh_campaign.h"
#include "core/nh_save.h"
#include "platform/platform.h"

typedef enum HideTab { TAB_JOBS, TAB_CREW, TAB_GEAR, TAB_FENCE, TAB_SETTINGS, TAB_COUNT } HideTab;

typedef enum HideAction { HA_NONE, HA_START_JOB, HA_SAVE } HideAction;

typedef struct Hideout {
    int tab;
    int sel;
    int plan;            /* job being planned, -1 for the board */
    bool show_pay;
    NhPayday pay;
    char pay_title[48];
    char msg[64];
    int msg_t;
    float cursor_x, cursor_y;
    bool cursor_valid;
    uint32_t tick;
} Hideout;

void hideout_init(Hideout *ho);
void hideout_payday(Hideout *ho, const NhPayday *pay, const char *title);
/* Returns HA_START_JOB with *job set to the board index, or HA_SAVE when
 * settings or purchases changed. */
HideAction hideout_tick(Hideout *ho, NhCampaign *c, NhSaveSettings *s, const NhInput *in, int *job);
void hideout_render(const Hideout *ho, const NhCampaign *c, const NhSaveSettings *s);

#endif
