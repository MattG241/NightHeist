#include "hideout.h"

#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "platform/gfx.h"
#include "text.h"

static const NhColor FG = { 230, 230, 230, 255 };
static const NhColor DIM = { 140, 140, 150, 255 };
static const NhColor HI = { 255, 200, 90, 255 };
static const NhColor GOOD = { 120, 220, 130, 255 };
static const NhColor BAD = { 235, 80, 60, 255 };

static const char *const k_tabs[TAB_COUNT] = { "JOBS", "CREW", "GEAR", "FENCE", "SETTINGS" };

#define ROW_Y0 96.0f
#define ROW_H 54.0f
#define SETTING_ROWS 14
#define SETTING_H 24.0f

void hideout_init(Hideout *ho)
{
    memset(ho, 0, sizeof(*ho));
    ho->plan = -1;
    ho->players = 1;
}

void hideout_payday(Hideout *ho, const NhPayday *pay, const char *title)
{
    ho->pay = *pay;
    ho->show_pay = true;
    if (pay->net > 0)
        audio_play(SFX_CASH, 0.7f, 0.0f, 1.0f);
    snprintf(ho->pay_title, sizeof(ho->pay_title), "%s", title);
}

static void say(Hideout *ho, const char *m)
{
    snprintf(ho->msg, sizeof(ho->msg), "%s", m);
    ho->msg_t = 180;
}

static int rows_in_tab(const Hideout *ho, const NhCampaign *c)
{
    if (ho->plan >= 0)
        return CR_COUNT + 1; /* crew toggles + GO */
    switch (ho->tab) {
    case TAB_JOBS: return c->board_n;
    case TAB_CREW: return CR_COUNT;
    case TAB_GEAR: return UP_COUNT;
    case TAB_FENCE: return 1;
    default: return SETTING_ROWS;
    }
}

static float row_h(const Hideout *ho)
{
    if (ho->plan >= 0)
        return 26.0f;
    return ho->tab == TAB_JOBS || ho->tab == TAB_CREW ? ROW_H : ho->tab == TAB_SETTINGS ? SETTING_H : 34.0f;
}

static float row_y0(const Hideout *ho) { return ho->plan >= 0 ? 214.0f : ROW_Y0; }

/* Settings rows: label and value text; A raises, - lowers. */
static void setting_text(const NhSaveSettings *s, int i, char *label, char *val, size_t n)
{
    static const char *const names[SETTING_ROWS] = {
        "Controls",    "Brightness",   "Turn speed",   "Pointer smoothing", "Turn box width", "Turn box height",
        "Crouch",      "Invert look",  "Rumble",            "Subtitles",      "Music volume",
        "Sound volume", "Difficulty",  "Playtest log (SD)",
    };
    snprintf(label, n, "%s", names[i]);
    const NhControlSettings *cs = &s->controls;
    switch (i) {
    case 0: snprintf(val, n, "%s", cs->scheme == NH_CONTROLS_SIMPLE ? "simple: stick turns" : "pointer turns"); break;
    case 1: snprintf(val, n, "%d", s->brightness); break;
    case 2: snprintf(val, n, "%.1f", (double)cs->turn_speed); break;
    case 3: snprintf(val, n, "%.2f", (double)cs->ir_smoothing); break;
    case 4: snprintf(val, n, "%.2f", (double)cs->box_w); break;
    case 5: snprintf(val, n, "%.2f", (double)cs->box_h); break;
    case 6: snprintf(val, n, "%s", cs->crouch_toggle ? "toggle" : "hold"); break;
    case 7: snprintf(val, n, "%s", cs->invert_y ? "on" : "off"); break;
    case 8: snprintf(val, n, "%s", s->rumble ? "on" : "off"); break;
    case 9: snprintf(val, n, "%s", s->subtitles ? "on" : "off"); break;
    case 10: snprintf(val, n, "%d", s->music); break;
    case 11: snprintf(val, n, "%d", s->sfx); break;
    case 12: snprintf(val, n, "%s", s->difficulty == 0 ? "easy" : s->difficulty == 1 ? "normal" : "hard"); break;
    default: snprintf(val, n, "%s", s->playlog ? "on" : "off"); break;
    }
}

static void adjust_setting(NhSaveSettings *s, int i, int dir)
{
    NhControlSettings *cs = &s->controls;
    switch (i) {
    case 0: cs->scheme = (cs->scheme + NH_CONTROLS_COUNT + dir) % NH_CONTROLS_COUNT; break;
    case 1: s->brightness = (uint8_t)NH_CLAMP((int)s->brightness + dir, 1, 5); break;
    case 2: cs->turn_speed = NH_CLAMP(cs->turn_speed + 0.2f * (float)dir, 1.0f, 6.0f); break;
    case 3: cs->ir_smoothing = NH_CLAMP(cs->ir_smoothing + 0.05f * (float)dir, 0.0f, 0.9f); break;
    case 4: cs->box_w = NH_CLAMP(cs->box_w + 0.05f * (float)dir, 0.2f, 0.9f); break;
    case 5: cs->box_h = NH_CLAMP(cs->box_h + 0.05f * (float)dir, 0.2f, 0.9f); break;
    case 6: cs->crouch_toggle = !cs->crouch_toggle; break;
    case 7: cs->invert_y = !cs->invert_y; break;
    case 8: s->rumble = !s->rumble; break;
    case 9: s->subtitles = !s->subtitles; break;
    case 10: s->music = (uint8_t)NH_CLAMP((int)s->music + dir, 0, 10); break;
    case 11: s->sfx = (uint8_t)NH_CLAMP((int)s->sfx + dir, 0, 10); break;
    case 12: s->difficulty = (uint8_t)((s->difficulty + 3 + dir) % 3); break;
    default: s->playlog = !s->playlog; break;
    }
}

static int crew_count(uint8_t bits)
{
    int n = 0;
    for (int m = 0; m < CR_COUNT; m++)
        n += (bits >> m) & 1u;
    return n;
}

static HideAction tick_menu(Hideout *ho, NhCampaign *c, NhSaveSettings *s, const NhInput *in, int *job)
{
    ho->tick++;
    if (ho->msg_t > 0)
        ho->msg_t--;
    ho->cursor_valid = in->ir_valid;
    if (in->ir_valid) {
        ho->cursor_x = in->ir_x;
        ho->cursor_y = in->ir_y;
    }
    if (ho->show_pay) {
        if (in->pressed & (NH_BTN_A | NH_BTN_B))
            ho->show_pay = false;
        return HA_NONE;
    }
    /* Tabs: D-pad left/right or clicking the tab strip. */
    if (ho->plan < 0) {
        int dt = (in->pressed & NH_BTN_RIGHT) ? 1 : (in->pressed & NH_BTN_LEFT) ? -1 : 0;
        if (dt) {
            ho->tab = (ho->tab + dt + TAB_COUNT) % TAB_COUNT;
            ho->sel = 0;
        }
        if (in->ir_valid && in->ir_y < 70 && in->ir_y > 44 && (in->pressed & NH_BTN_A)) {
            int t = (int)((in->ir_x - 20.0f) / 120.0f);
            if (t >= 0 && t < TAB_COUNT) {
                ho->tab = t;
                ho->sel = 0;
                return HA_NONE;
            }
        }
    } else if (in->pressed & NH_BTN_B) {
        ho->plan = -1;
        ho->sel = 0;
        return HA_NONE;
    } else if (in->pressed & (NH_BTN_LEFT | NH_BTN_RIGHT)) {
        /* Planning: the D-pad picks one or two players. */
        if (ho->players == 1 && !ho->p2_connected)
            say(ho, "Connect Wii Remote 2 (with a Nunchuk) for co-op");
        else
            ho->players = ho->players == 1 ? 2 : 1;
        return HA_NONE;
    }
    if (!ho->p2_connected)
        ho->players = 1;
    int nrows = rows_in_tab(ho, c);
    if (in->pressed & NH_BTN_DOWN)
        ho->sel = (ho->sel + 1) % NH_MAX(1, nrows);
    if (in->pressed & NH_BTN_UP)
        ho->sel = (ho->sel + nrows - 1) % NH_MAX(1, nrows);
    /* Pointer hover picks the row. */
    if (in->ir_valid) {
        int r = (int)((in->ir_y - row_y0(ho)) / row_h(ho));
        if (in->ir_y >= row_y0(ho) && r >= 0 && r < nrows)
            ho->sel = r;
    }
    bool a = (in->pressed & NH_BTN_A) != 0, minus = (in->pressed & NH_BTN_MINUS) != 0;
    if (!a && !minus)
        return HA_NONE;
    if (ho->plan >= 0) {
        if (ho->sel < CR_COUNT && a) {
            int m = ho->sel;
            if (!((c->crew_hired >> m) & 1u)) {
                say(ho, "Not on the crew yet: hire them in the CREW tab");
            } else if ((c->crew_selected >> m) & 1u) {
                c->crew_selected &= (uint8_t)~(1u << m);
            } else if (crew_count(c->crew_selected & c->crew_hired) >= 2) {
                say(ho, "Two crew at most on a job");
            } else {
                c->crew_selected |= (uint8_t)(1u << m);
            }
            return HA_NONE;
        }
        if (ho->sel == CR_COUNT && a) {
            *job = ho->plan;
            ho->plan = -1;
            return HA_START_JOB;
        }
        return HA_NONE;
    }
    switch (ho->tab) {
    case TAB_JOBS:
        if (a && ho->sel < c->board_n) {
            ho->plan = ho->sel;
            ho->sel = CR_COUNT;
        }
        return HA_NONE;
    case TAB_CREW:
        if (a) {
            int m = ho->sel;
            if ((c->crew_hired >> m) & 1u)
                say(ho, "Already on the crew. Pick who comes along when planning a job.");
            else if (c->reputation < nh_crew[m].hire_rep)
                say(ho, "They won't work with you yet: more reputation");
            else if (!camp_hire(c, m))
                say(ho, "Not enough money");
            else {
                say(ho, "Hired");
                return HA_SAVE;
            }
        }
        return HA_NONE;
    case TAB_GEAR:
        if (a) {
            if (camp_upgrade_cost(c, ho->sel) < 0)
                say(ho, "Fully upgraded");
            else if (!camp_upgrade_available(c, ho->sel))
                say(ho, "Nothing to fit it to yet");
            else if (!camp_buy_upgrade(c, ho->sel))
                say(ho, "Not enough money");
            else {
                say(ho, "Upgraded");
                return HA_SAVE;
            }
        }
        return HA_NONE;
    case TAB_FENCE:
        if (a) {
            if (camp_lie_low(c)) {
                say(ho, "You lie low for a week. Heat drops everywhere.");
                return HA_SAVE;
            }
            say(ho, "Not enough money to lie low");
        }
        return HA_NONE;
    default:
        adjust_setting(s, ho->sel, a ? 1 : -1);
        return HA_SAVE;
    }
}

/* The menu, plus its clicks: moves, selects, backs out, refusals and the till. */
HideAction hideout_tick(Hideout *ho, NhCampaign *c, NhSaveSettings *s, const NhInput *in, int *job)
{
    int tab = ho->tab, sel = ho->sel, plan = ho->plan, msg_t = ho->msg_t;
    bool pay = ho->show_pay;
    int32_t money = c->money;
    uint8_t crew = c->crew_selected;
    HideAction ha = tick_menu(ho, c, s, in, job);
    if (ha == HA_START_JOB || (pay && !ho->show_pay) || crew != c->crew_selected || (plan < 0 && ho->plan >= 0))
        audio_play(SFX_UI_SELECT, 0.5f, 0.0f, 1.0f);
    else if (ha == HA_SAVE)
        audio_play(c->money < money ? SFX_CASH : SFX_UI_SELECT, 0.5f, 0.0f, 1.0f);
    else if (plan >= 0 && ho->plan < 0)
        audio_play(SFX_UI_BACK, 0.5f, 0.0f, 1.0f);
    else if (ho->msg_t == 180 && msg_t != 180)
        audio_play(SFX_UI_DENY, 0.5f, 0.0f, 1.0f);
    else if (tab != ho->tab || sel != ho->sel)
        audio_play(SFX_UI_MOVE, 0.35f, 0.0f, 1.0f);
    return ha;
}

/* ---- rendering ----------------------------------------------------------------------- */

static void row_box(const Hideout *ho, int i, float y, float hgt)
{
    bool sel = ho->sel == i;
    text_rect(20, y, 600, hgt - 4, sel ? nh_rgba(60, 50, 30, 220) : nh_rgba(20, 22, 30, 200));
    if (sel)
        text_rect(20, y, 4, hgt - 4, HI);
}

static void stars(float x, float y, int n, int max)
{
    for (int i = 0; i < max; i++)
        text_rect(x + (float)i * 12, y, 9, 9, i < n ? HI : nh_rgba(60, 60, 70, 255));
}

static void draw_jobs(const Hideout *ho, const NhCampaign *c)
{
    for (int i = 0; i < c->board_n; i++) {
        const NhJobOffer *o = &c->board[i];
        float y = ROW_Y0 + (float)i * ROW_H;
        row_box(ho, i, y, ROW_H);
        text_printf(32, y + 6, 2, o->finale ? HI : o->loyalty_crew ? GOOD : FG, "%s", o->name);
        text_printf(32, y + 26, 1, DIM, "%s", nh_districts[o->district].name);
        stars(200, y + 26, o->security + 1, 5);
        char mods[96] = "";
        for (int m = 0; m < MOD_COUNT; m++)
            if ((o->mods >> m) & 1u) {
                size_t len = strlen(mods);
                snprintf(mods + len, sizeof(mods) - len, "%s%s", len ? ", " : "", nh_mod_names[m]);
            }
        text_printf(270, y + 26, 1, mods[0] ? HI : DIM, "%s", mods[0] ? mods : "no surprises");
        text_printf(500, y + 8, 2, GOOD, "$%dk", (int)(o->est_payout * 10 / 1000));
    }
}

static void draw_plan(const Hideout *ho, const NhCampaign *c)
{
    const NhJobOffer *o = &c->board[ho->plan];
    text_rect(20, 80, 600, 124, nh_rgba(20, 22, 30, 220));
    text_printf(32, 88, 2, o->finale ? HI : FG, "%s", o->name);
    text_printf(32, 108, 1, DIM, "%s: %s", nh_districts[o->district].name, nh_districts[o->district].blurb);
    text_printf(32, 122, 1, FG, "security");
    stars(100, 122, o->security + 1, 5);
    text_printf(400, 92, 2, GOOD, "~$%dk", (int)(o->est_payout * 10 / 1000));
    float y = 140;
    int shown = 0;
    for (int m = 0; m < MOD_COUNT; m++)
        if ((o->mods >> m) & 1u) {
            text_printf(32, y, 1, HI, "%s: %s", nh_mod_names[m], nh_mod_desc[m]);
            y += 12;
            shown++;
        }
    if (!shown)
        text_draw(32, y, 1, DIM, "Nothing unusual tonight.");
    text_draw(32, 196, 1, DIM, "Bring up to two crew (A to toggle):");
    for (int m = 0; m < CR_COUNT; m++) {
        float ry = 214 + (float)m * 26;
        row_box(ho, m, ry, 26);
        bool hired = (c->crew_hired >> m) & 1u, on = camp_crew_on(c, m);
        text_printf(32, ry + 5, 2, !hired ? DIM : on ? GOOD : FG, "%s %s", on ? "[x]" : "[ ]", nh_crew[m].name);
        text_printf(300, ry + 2, 1, DIM, "%s  %d%% cut", nh_crew[m].role, nh_crew[m].cut);
        text_printf(300, ry + 12, 1, hired ? FG : DIM, "%s", hired ? nh_crew[m].perk : "not hired");
    }
    float gy = 214 + (float)CR_COUNT * 26;
    row_box(ho, CR_COUNT, gy, 34);
    text_draw(32, gy + 7, 3, HI, "GO");
    text_draw(120, gy + 12, 1, DIM, "B: back to the board");
    text_printf(330, gy + 4, 2, ho->players > 1 ? GOOD : FG, "%s", ho->players > 1 ? "2 PLAYERS" : "1 PLAYER");
    text_draw(330, gy + 22, 1, DIM,
              ho->p2_connected ? "D-pad left/right: co-op split screen" : "co-op: connect Wii Remote 2");
}

static void draw_crew(const Hideout *ho, const NhCampaign *c)
{
    for (int m = 0; m < CR_COUNT; m++) {
        float y = ROW_Y0 + (float)m * ROW_H;
        const NhCrewInfo *ci = &nh_crew[m];
        bool hired = (c->crew_hired >> m) & 1u;
        row_box(ho, m, y, ROW_H);
        text_printf(32, y + 4, 2, hired ? FG : DIM, "%s", ci->name);
        text_printf(250, y + 6, 1, HI, "%s", ci->role);
        text_printf(32, y + 22, 1, FG, "+ %s%s", ci->perk, camp_crew_perk2(c, m) ? "  (loyal: perk improved)" : "");
        text_printf(32, y + 34, 1, BAD, "- %s", ci->flaw);
        if (hired) {
            text_rect(470, y + 8, 120, 6, nh_rgba(50, 50, 60, 255));
            text_rect(470, y + 8, 1.2f * (float)c->crew_loyalty[m], 6, GOOD);
            text_draw(470, y + 18, 1, DIM, c->crew_loyalty[m] >= 60 && !camp_crew_perk2(c, m) ? "loyalty job ready" : "loyalty");
        } else {
            text_printf(470, y + 6, 1, c->reputation >= ci->hire_rep ? FG : DIM, "hire $%dk",
                        (int)(camp_hire_cost(c, m) * 10 / 1000));
            text_printf(470, y + 18, 1, c->reputation >= ci->hire_rep ? GOOD : BAD, "rep %d", ci->hire_rep);
        }
    }
}

static void draw_gear(const Hideout *ho, const NhCampaign *c)
{
    for (int u = 0; u < UP_COUNT; u++) {
        float y = ROW_Y0 + (float)u * 34;
        row_box(ho, u, y, 34);
        text_printf(32, y + 4, 2, FG, "%s", nh_upgrades[u].name);
        text_printf(32, y + 20, 1, DIM, "%s", nh_upgrades[u].desc);
        stars(330, y + 10, c->upgrade[u], nh_upgrades[u].max_level);
        int32_t cost = camp_upgrade_cost(c, u);
        if (cost < 0)
            text_draw(480, y + 8, 2, GOOD, "MAX");
        else if (!camp_upgrade_available(c, u))
            text_draw(480, y + 8, 2, DIM, "later");
        else
            text_printf(480, y + 8, 2, c->money >= cost ? FG : BAD, "$%dk", (int)(cost * 10 / 1000));
    }
}

static void draw_fence(const Hideout *ho, const NhCampaign *c)
{
    row_box(ho, 0, ROW_Y0, 34);
    text_printf(32, ROW_Y0 + 8, 2, FG, "Lie low for a week  ($%dk)", (int)((2500 + c->reputation * 10) * 10 / 1000));
    float y = ROW_Y0 + 50;
    text_draw(32, y, 2, HI, "Collections the fence wants");
    y += 22;
    for (int s = 0; s < LS_COUNT; s++) {
        const NhLootSetInfo *ls = &nh_loot_sets[s];
        text_printf(32, y, 2, c->sets_done[s] ? GOOD : FG, "%s", ls->name);
        text_printf(300, y, 2, DIM, "%d/%d", NH_MIN(c->set_count[s], ls->need), ls->need);
        text_printf(400, y, 2, c->sets_done[s] ? GOOD : FG, c->sets_done[s] ? "sold" : "+$%dk", (int)(ls->bonus * 10 / 1000));
        y += 20;
    }
    y += 14;
    text_draw(32, y, 2, HI, "Heat");
    y += 22;
    for (int d = 0; d < DI_COUNT; d++) {
        bool open = camp_district_open(c, d);
        text_printf(32, y, 2, open ? FG : DIM, "%s%s", nh_districts[d].name, c->district_done[d] ? "  (done)" : "");
        text_rect(330, y + 4, 200, 8, nh_rgba(50, 50, 60, 255));
        text_rect(330, y + 4, 2.0f * (float)c->heat[d], 8, c->heat[d] > 60 ? BAD : HI);
        if (!open)
            text_printf(540, y, 1, DIM, "rep %d", nh_districts[d].unlock_rep);
        y += 20;
    }
}

static void draw_settings(const Hideout *ho, const NhSaveSettings *s)
{
    for (int i = 0; i < SETTING_ROWS; i++) {
        float y = ROW_Y0 + (float)i * SETTING_H;
        row_box(ho, i, y, SETTING_H);
        char label[48], val[48];
        setting_text(s, i, label, val, sizeof(label));
        text_draw(32, y + 4, 2, FG, label);
        text_draw(330, y + 4, 2, HI, val);
    }
    text_draw(32, 448, 1, DIM, "A: raise / next    -: lower");
}

void hideout_render(const Hideout *ho, const NhCampaign *c, const NhSaveSettings *s)
{
    gfx_begin_frame(nh_rgba(8, 9, 14, 255));
    /* Header */
    text_rect(0, 0, 640, 40, nh_rgba(16, 18, 26, 255));
    text_draw(20, 10, 3, HI, "SAFEHOUSE");
    text_printf(250, 8, 2, GOOD, "$%d", c->money * 10);
    text_printf(250, 24, 1, FG, "reputation %d   jobs %d   ghosts %d%s", c->reputation, c->jobs_done, c->ghosts,
                c->postgame ? "   LEGEND" : "");
    for (int d = 0; d < DI_COUNT; d++)
        text_rect(560 + (float)d * 18, 14, 14, 14,
                  c->district_done[d] ? GOOD : camp_district_open(c, d) ? HI : nh_rgba(50, 50, 60, 255));
    if (ho->plan < 0)
        for (int t = 0; t < TAB_COUNT; t++) {
            bool on = ho->tab == t;
            text_rect(20 + (float)t * 120, 46, 112, 24, on ? nh_rgba(70, 56, 30, 255) : nh_rgba(24, 26, 34, 255));
            text_draw(30 + (float)t * 120, 50, 2, on ? HI : DIM, k_tabs[t]);
        }
    if (ho->plan >= 0)
        draw_plan(ho, c);
    else
        switch (ho->tab) {
        case TAB_JOBS: draw_jobs(ho, c); break;
        case TAB_CREW: draw_crew(ho, c); break;
        case TAB_GEAR: draw_gear(ho, c); break;
        case TAB_FENCE: draw_fence(ho, c); break;
        default: draw_settings(ho, s); break;
        }
    if (ho->msg_t > 0)
        text_draw(320 - text_width(ho->msg, 2) * 0.5f, 462, 2, HI, ho->msg);
    else if (ho->plan < 0 && ho->tab != TAB_SETTINGS)
        text_draw(320 - text_width("D-pad: tabs    A: choose    HOME: title", 1) * 0.5f, 466, 1, DIM,
                  "D-pad: tabs    A: choose    HOME: title");
    if (ho->show_pay) {
        const NhPayday *p = &ho->pay;
        text_rect(110, 110, 420, 260, nh_rgba(10, 12, 18, 245));
        text_draw(320 - text_width(ho->pay_title, 3) * 0.5f, 124, 3, HI, ho->pay_title);
        float y = 170;
        text_printf(140, y, 2, FG, "Take            $%d", p->gross * 10);
        y += 22;
        if (p->crew_cut) {
            text_printf(140, y, 2, BAD, "Crew's cut     -$%d", p->crew_cut * 10);
            y += 22;
        }
        if (p->set_bonus) {
            text_printf(140, y, 2, GOOD, "%s  +$%d", nh_loot_sets[p->set_completed - 1].name, p->set_bonus * 10);
            y += 22;
        }
        text_printf(140, y, 2, HI, "Net             $%d", p->net * 10);
        y += 30;
        text_printf(140, y, 2, p->rep >= 0 ? GOOD : BAD, "Reputation %+d   heat %+d", p->rep, p->heat);
        y += 22;
        if (p->district_finished) {
            text_draw(140, y, 2, GOOD, p->unlocked_district ? "District done. A new district opens." : "District done.");
            y += 22;
        }
        if (p->news[0])
            text_draw(320 - text_width(p->news, 1) * 0.5f, y + 4, 1, HI, p->news);
        text_draw(320 - text_width("A  continue", 2) * 0.5f, 340, 2, FG, "A  continue");
    }
    /* Cursor */
    if (ho->cursor_valid) {
        NhColor cc = nh_rgba(255, 255, 255, 220);
        text_rect(ho->cursor_x - 1, ho->cursor_y - 8, 2, 16, cc);
        text_rect(ho->cursor_x - 8, ho->cursor_y - 1, 16, 2, cc);
    }
    gfx_end_frame();
}
