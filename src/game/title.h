/* Night Heist - the title screen: a city skyline at night, the menu
 * (continue, new career, controls, quit) and a summary of the saved career. */
#ifndef NH_TITLE_H
#define NH_TITLE_H

#include "core/nh_campaign.h"
#include "platform/platform.h"

typedef enum TitleAction { TA_NONE, TA_CONTINUE, TA_NEW, TA_QUIT } TitleAction;

typedef struct Title {
    int sel;
    int page; /* 0 menu, 1 controls, 2 confirm a new career */
    bool has_save;
    uint32_t tick;
    float cursor_x, cursor_y;
    bool cursor_valid;
} Title;

void title_init(Title *t, bool has_save);
TitleAction title_tick(Title *t, const NhInput *in);
void title_render(const Title *t, const NhCampaign *c);

#endif
