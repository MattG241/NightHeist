#include "nh_campaign.h"

#include <stdio.h>
#include <string.h>

#include "nh_proptab.h"
#include "nh_rng.h"

const NhDistrictInfo nh_districts[DI_COUNT] = {
    [DI_DOCKS] = { "The Docklands", "Warehouses and shipping offices. Lax security, cheap thrills.",
                   { BT_WAREHOUSE, BT_OFFICE, 255 }, 0, 0, 100 },
    [DI_MIDTOWN] = { "Midtown", "Glass towers and banks. Cameras everywhere.", { BT_OFFICE, BT_BANK, 255 }, 1, 70, 135 },
    [DI_OLDTOWN] = { "The Old Quarter", "Museums and old money. Lasers and veterans.", { BT_MUSEUM, BT_MANSION, 255 }, 2,
                     190, 170 },
    [DI_HEIGHTS] = { "The Heights", "Private vaults and penthouses. Elites with thermal goggles.",
                     { BT_BANK, BT_MANSION, BT_MUSEUM }, 3, 340, 220 },
};

const char *const nh_mod_names[MOD_COUNT] = {
    "Night shift", "Skeleton crew", "Blackout", "Cleaners in", "Dog patrol", "Thermal team",
    "Lockdown", "Inside man", "Fast response", "Storm", "Bonus target", "Clean job",
};

const char *const nh_mod_desc[MOD_COUNT] = {
    "Two extra guards on duty.",
    "Two fewer guards tonight.",
    "The building starts in the dark.",
    "Every light is on.",
    "An extra dog team on the grounds.",
    "Elite guards with thermal goggles.",
    "Harder locks; every outside door is alarmed.",
    "You start with every keypad code.",
    "Police arrive in half the time.",
    "Rain covers your footsteps.",
    "The client pays double for the target.",
    "The client wants no takedowns.",
};

const NhCrewInfo nh_crew[CR_COUNT] = {
    [CR_HACKER] = { "Nadia Volkova", "Hacker", "Hacks are one layer shorter; starts with one keypad code.",
                    "Takes an 18% cut.", "Wipe her old employer's servers", 18, 0 },
    [CR_LOCKSMITH] = { "Eddie Price", "Locksmith", "Locks have one pin fewer.", "His slips are noisier.",
                       "Crack the vault that ruined his family", 14, 40 },
    [CR_DRIVER] = { "Rosa Delgado", "Driver", "Police take 30 s longer to arrive.", "Hates heat: +1 heat per job.",
                    "Steal back her impounded car's papers", 12, 0 },
    [CR_MUSCLE] = { "Big Tam", "Muscle", "Silent takedowns; carries bodies and loot faster.",
                    "Leaves a mess: bodies are found sooner.", "Settle a score at a rival's warehouse", 12, 90 },
    [CR_LOOKOUT] = { "Kit Mercer", "Lookout", "Guards and cameras show on the map.", "Radio gear: one fewer bag slot.",
                     "Get the tapes that put her brother away", 10, 130 },
    [CR_FACE] = { "Oscar Bell", "Face", "The fence pays 15% more.", "Takes a 20% cut.",
                  "Lift the painting he once forged", 20, 220 },
};

const NhUpgradeInfo nh_upgrades[UP_COUNT] = {
    [UP_TORCH] = { "Torch", "Longer battery, wider beam.", 3, { 2500, 7000, 16000 } },
    [UP_BAG] = { "Bag", "+4 slots per level.", 3, { 3000, 9000, 20000 } },
    [UP_SHOES] = { "Soft soles", "Quieter footsteps.", 2, { 4500, 13000, 0 } },
    [UP_PICKS] = { "Picks", "One pin fewer per level.", 2, { 5000, 15000, 0 } },
    [UP_GADGETS] = { "Gadget kit", "+1 charge on every gadget per level.", 3, { 6000, 16000, 32000 } },
    [UP_THERMAL] = { "Thermal cells", "Double thermal battery.", 1, { 9000, 0, 0 } },
    [UP_DARTS] = { "Fast darts", "Darts drop guards in 1.5 s.", 1, { 7000, 0, 0 } },
    [UP_SCANNER] = { "Scanner", "Objectives and safes always on the map.", 1, { 12000, 0, 0 } },
};

const NhLootSetInfo nh_loot_sets[LS_COUNT] = {
    [LS_PAINTINGS] = { "Old masters", 3, 15000 },
    [LS_JEWELS] = { "Crown jewels", 4, 18000 },
    [LS_WATCHES] = { "Watch collection", 3, 10000 },
    [LS_DATA] = { "Data dump", 4, 12000 },
    [LS_GOLD] = { "Bullion", 3, 20000 },
};

/* Typical take (units of $10) by type and size, from generator loot totals. */
static const uint16_t k_base_pay[BT_COUNT][BS_COUNT] = {
    { 2000, 3500, 6500 }, { 2500, 4500, 8000 }, { 3500, 6000, 10000 }, { 3000, 5500, 9000 }, { 3500, 6000, 10000 },
};

void camp_new(NhCampaign *c, uint32_t seed)
{
    memset(c, 0, sizeof(*c));
    c->seed = seed;
    c->money = 2000;
    c->crew_hired = (uint8_t)((1u << CR_DRIVER) | (1u << CR_HACKER));
    c->crew_selected = (uint8_t)(1u << CR_DRIVER);
    camp_make_board(c);
}

int camp_level(const NhCampaign *c)
{
    int n = 0;
    for (int d = 0; d < DI_COUNT; d++)
        n += c->district_done[d] != 0;
    return n;
}

bool camp_district_open(const NhCampaign *c, int d)
{
    if (d < 0 || d >= DI_COUNT)
        return false;
    /* A district opens with reputation, but not before the previous finale. */
    return c->reputation >= nh_districts[d].unlock_rep && (d == 0 || c->district_done[d - 1]);
}

bool camp_crew_on(const NhCampaign *c, int m)
{
    return ((c->crew_hired & c->crew_selected) >> m) & 1u;
}

bool camp_crew_perk2(const NhCampaign *c, int m) { return (c->crew_perk2 >> m) & 1u; }

/* Career step at which each gadget and modifier first shows up. PACING.md
 * lays this against play time. */
static const uint8_t k_gadget_at[GD_COUNT] = {
    [GD_LOCKPICK] = 0, [GD_COIN] = 0,    [GD_TASER] = 0,  [GD_NOISEMAKER] = 1, [GD_SMOKE] = 3,
    [GD_JAMMER] = 5,   [GD_DART] = 8,    [GD_CLONER] = 11, [GD_EMP] = 13,      [GD_DIALER] = 16,
    [GD_MIRROR] = 19,  [GD_DRILL] = 22,  [GD_CUTTER] = 24, [GD_THERMAL] = 28,
};
static const uint8_t k_mod_at[MOD_COUNT] = {
    [MOD_NIGHT_SHIFT] = 0, [MOD_SKELETON] = 0,    [MOD_LIGHTS_ON] = 2,     [MOD_BLACKOUT] = 4,
    [MOD_STORM] = 6,       [MOD_INSIDE_MAN] = 9,  [MOD_BONUS_TARGET] = 12, [MOD_FAST_POLICE] = 15,
    [MOD_NO_KILLS] = 17,   [MOD_DOGS] = 21,       [MOD_LOCKDOWN] = 26,     [MOD_THERMAL] = 30,
};

int camp_career(const NhCampaign *c) { return c->jobs_done + c->jobs_failed; }

bool camp_gadget_unlocked(const NhCampaign *c, int g)
{
    return g >= 0 && g < GD_COUNT && (c->postgame || camp_career(c) >= k_gadget_at[g]);
}

bool camp_mod_unlocked(const NhCampaign *c, int m)
{
    return m >= 0 && m < MOD_COUNT && (c->postgame || camp_career(c) >= k_mod_at[m]);
}

int camp_unlock_news(int career, char *buf, int len)
{
    int n = 0;
    buf[0] = 0;
    if (career <= 0)
        return 0;
    for (int g = 0; g < GD_COUNT; g++)
        if (k_gadget_at[g] == career && n < len)
            n += snprintf(buf + n, (size_t)(len - n), "%sNew gadget: %s", n ? "  " : "", nh_gadgets[g].name);
    for (int m = 0; m < MOD_COUNT; m++)
        if (k_mod_at[m] == career && n < len)
            n += snprintf(buf + n, (size_t)(len - n), "%sNew on the board: %s", n ? "  " : "", nh_mod_names[m]);
    return NH_MIN(n, len - 1);
}

static int pick_size(const NhCampaign *c, NhRng *r, int district)
{
    int jobs = c->district_jobs[district];
    int roll = (int)nh_rng_range(r, 100);
    if (jobs < 2)
        return roll < 70 ? BS_SMALL : BS_MEDIUM;
    if (jobs < 5)
        return roll < 25 ? BS_SMALL : roll < 85 ? BS_MEDIUM : BS_LARGE;
    return roll < 50 ? BS_MEDIUM : BS_LARGE;
}

static int32_t estimate(int district, int type, int size, int sec, uint16_t mods)
{
    int32_t p = k_base_pay[type][size] * nh_districts[district].pay_mult / 100;
    p = p * (100 + sec * 15) / 100;
    if (mods & (1u << MOD_BONUS_TARGET))
        p = p * 3 / 2;
    return p;
}

static void make_offer(NhCampaign *c, NhRng *r, NhJobOffer *o, int district, bool finale, int loyalty)
{
    memset(o, 0, sizeof(*o));
    const NhDistrictInfo *di = &nh_districts[district];
    int nt = 0;
    while (nt < 3 && di->types[nt] != 255)
        nt++;
    o->district = (uint8_t)district;
    o->type = di->types[nh_rng_range(r, (uint32_t)nt)];
    o->size = (uint8_t)(finale ? BS_LARGE : pick_size(c, r, district));
    int sec = di->base_security + c->heat[district] / 34 + (finale ? 1 : 0) + (c->postgame ? 1 : 0);
    o->security = (uint8_t)NH_CLAMP(sec, 0, 4);
    o->seed = nh_rng_u32(r);
    o->finale = finale;
    /* Modifiers: more as the campaign goes on. */
    int nmods = (int)nh_rng_range(r, 2) + (c->district_jobs[district] >= 3) + (c->postgame ? 1 : 0) + (finale ? 1 : 0);
    for (int k = 0; k < nmods; k++) {
        int m = (int)nh_rng_range(r, MOD_COUNT);
        /* Not yet in the schedule, or contradictory */
        if (!camp_mod_unlocked(c, m) || (m == MOD_NIGHT_SHIFT && (o->mods & (1u << MOD_SKELETON))) ||
            (m == MOD_SKELETON && (o->mods & (1u << MOD_NIGHT_SHIFT))) ||
            (m == MOD_BLACKOUT && (o->mods & (1u << MOD_LIGHTS_ON))) ||
            (m == MOD_LIGHTS_ON && (o->mods & (1u << MOD_BLACKOUT))) || (m == MOD_THERMAL && o->security < 2))
            continue;
        o->mods |= (uint16_t)(1u << m);
    }
    if (finale)
        o->mods |= 1u << MOD_BONUS_TARGET;
    if (loyalty >= 0) {
        o->loyalty_crew = (uint8_t)(loyalty + 1);
        static const uint8_t lt[CR_COUNT] = { BT_OFFICE, BT_BANK, BT_WAREHOUSE, BT_WAREHOUSE, BT_OFFICE, BT_MUSEUM };
        o->type = lt[loyalty];
        o->size = BS_MEDIUM;
    }
    o->est_payout = estimate(district, o->type, o->size, o->security, o->mods);
    if (finale)
        o->est_payout *= 2;
    if (loyalty >= 0)
        snprintf(o->name, sizeof(o->name), "%s", nh_crew[loyalty].loyalty_job);
    else if (finale)
        snprintf(o->name, sizeof(o->name), "FINALE: %s %s", nh_size_names[o->size], nh_building_names[o->type]);
    else
        snprintf(o->name, sizeof(o->name), "%s %s", nh_size_names[o->size], nh_building_names[o->type]);
}

void camp_make_board(NhCampaign *c)
{
    NhRng r;
    nh_rng_seed(&r, ((uint64_t)c->seed << 16) ^ c->board_gen, 0xB0Au);
    c->board_gen++;
    c->board_n = 0;
    int open[DI_COUNT], nopen = 0;
    for (int d = 0; d < DI_COUNT; d++)
        if (camp_district_open(c, d))
            open[nopen++] = d;
    /* The newest district gets most of the work. */
    int newest = open[nopen - 1];
    /* A finale when the newest district has seen enough jobs. */
    if (!c->district_done[newest] && c->district_jobs[newest] >= 5)
        make_offer(c, &r, &c->board[c->board_n++], newest, true, -1);
    /* A loyalty job for the most loyal crew member who's ready. */
    for (int m = 0; m < CR_COUNT && c->board_n < JOB_BOARD_MAX; m++)
        if (((c->crew_hired >> m) & 1u) && !camp_crew_perk2(c, m) && c->crew_loyalty[m] >= 60) {
            make_offer(c, &r, &c->board[c->board_n++], newest, false, m);
            break;
        }
    while (c->board_n < JOB_BOARD_MAX - (nopen > 1 ? 0 : 1)) {
        int d = nopen > 1 && nh_rng_range(&r, 100) < 35 ? open[nh_rng_range(&r, (uint32_t)nopen)] : newest;
        make_offer(c, &r, &c->board[c->board_n++], d, false, -1);
    }
}

void camp_offer_params(const NhCampaign *c, const NhJobOffer *o, NhGenParams *gp)
{
    memset(gp, 0, sizeof(*gp));
    gp->seed = o->seed;
    gp->type = o->type;
    gp->size = o->size;
    gp->security = o->security;
    gp->mods = o->mods;
    gp->avoid_layouts = c->history_fp;
    gp->navoid = c->history_n;
    gp->avoid_setpieces = c->sp_hist[0] | c->sp_hist[1] | c->sp_hist[2];
}

void camp_prepare_job(const NhCampaign *c, const NhJobOffer *o, NhJob *j)
{
    j->bag_cap = 12 + 4 * c->upgrade[UP_BAG] - (camp_crew_on(c, CR_LOOKOUT) ? 1 : 0);
    for (int g = 1; g < GD_COUNT; g++)
        if (!camp_gadget_unlocked(c, g))
            j->charges[g] = 0;
        else if (j->charges[g] != 255)
            j->charges[g] = (uint8_t)NH_MIN(250, j->charges[g] + c->upgrade[UP_GADGETS]);
    if (camp_crew_on(c, CR_MUSCLE) && camp_crew_perk2(c, CR_MUSCLE))
        j->charges[GD_TASER] = (uint8_t)(j->charges[GD_TASER] + 2);
    if (camp_crew_on(c, CR_LOCKSMITH) && camp_crew_perk2(c, CR_LOCKSMITH))
        j->charges[GD_DRILL] = (uint8_t)(j->charges[GD_DRILL] + 1);
    float delay = j->police_delay;
    if (camp_crew_on(c, CR_DRIVER))
        delay += camp_crew_perk2(c, CR_DRIVER) ? 60.0f : 30.0f;
    if (o->mods & (1u << MOD_FAST_POLICE))
        delay *= 0.5f;
    j->police_delay = delay;
    if (o->mods & (1u << MOD_INSIDE_MAN))
        j->codes = 0xFFu;
    if (camp_crew_on(c, CR_HACKER))
        j->codes |= camp_crew_perk2(c, CR_HACKER) ? 0xFFu : 0x02u;
    if (o->mods & (1u << MOD_NO_KILLS)) {
        /* Replace the conduct objective. */
        for (int i = 0; i < j->nobj; i++)
            if (j->obj[i].type == OBJ_GHOST || j->obj[i].type == OBJ_NO_ALARM || j->obj[i].type == OBJ_NO_KO ||
                j->obj[i].type == OBJ_LOOT || j->obj[i].type == OBJ_TIME) {
                j->obj[i].type = OBJ_NO_KO;
                j->obj[i].optional = 0;
                j->obj[i].bonus = 0;
                snprintf(j->obj[i].text, sizeof(j->obj[i].text), "Client's orders: no takedowns");
            }
    }
}

static int loot_set_of(int prop_type)
{
    switch (prop_type) {
    case PROP_PAINTING: return LS_PAINTINGS;
    case PROP_JEWELRY_BOX: return LS_JEWELS;
    case PROP_WATCH_CASE: return LS_WATCHES;
    case PROP_HARD_DRIVE: case PROP_LAPTOP: case PROP_DOCUMENTS: return LS_DATA;
    case PROP_GOLD_BARS: case PROP_CASH_STACK: return LS_GOLD;
    default: return -1;
    }
}

void camp_finish_job(NhCampaign *c, const NhJobOffer *o, const NhJobResult *r, const NhJob *j, const Level *L,
                     const NhPlan *plan, uint32_t fingerprint, uint64_t setpieces, NhPayday *out)
{
    memset(out, 0, sizeof(*out));
    int d = o->district;
    /* Pay */
    int32_t gross = r->payout * nh_districts[d].pay_mult / 100;
    if (o->mods & (1u << MOD_BONUS_TARGET) && r->outcome == JOB_SUCCESS && plan->objective >= 0)
        gross += plan->ent[plan->objective].value;
    if (o->finale && r->outcome == JOB_SUCCESS)
        gross += gross / 2;
    if (camp_crew_on(c, CR_FACE))
        gross = gross * (camp_crew_perk2(c, CR_FACE) ? 125 : 115) / 100;
    int cut = 0;
    for (int m = 0; m < CR_COUNT; m++)
        if (camp_crew_on(c, m))
            cut += nh_crew[m].cut;
    out->gross = gross;
    out->crew_cut = gross * cut / 100;
    /* Loot sets: count what came home. */
    if (r->outcome == JOB_SUCCESS || r->outcome == JOB_PARTIAL || r->outcome == JOB_ABORTED) {
        for (int i = 0; i < plan->nent; i++) {
            const NhEntity *e = &plan->ent[i];
            if (e->kind != ENT_LOOT || !job_taken(j, i) || e->prop < 0)
                continue;
            int s = loot_set_of(L->props[e->prop].type);
            if (s < 0 || c->sets_done[s])
                continue;
            c->set_count[s]++;
            if (c->set_count[s] >= nh_loot_sets[s].need) {
                c->sets_done[s] = 1;
                out->set_bonus += nh_loot_sets[s].bonus;
                out->set_completed = s + 1;
            }
        }
    }
    out->net = gross - out->crew_cut + out->set_bonus;
    c->money += out->net;
    c->total_earned += (uint32_t)NH_MAX(0, out->net);
    /* Reputation */
    int rep;
    switch (r->grade) {
    case 'S': rep = 25; break;
    case 'A': rep = 18; break;
    case 'B': rep = 12; break;
    case 'C': rep = 8; break;
    case 'D': rep = 4; break;
    default: rep = -10; break;
    }
    if (r->outcome == JOB_PARTIAL)
        rep = r->loot > 0 ? 3 : 0;
    if (r->outcome == JOB_ABORTED)
        rep = 0;
    if (r->ghost && r->outcome == JOB_SUCCESS)
        rep += 5;
    if (o->finale && r->outcome == JOB_SUCCESS)
        rep += 30;
    c->reputation = NH_MAX(0, c->reputation + rep);
    out->rep = rep;
    /* Heat: here up, elsewhere down. */
    int heat = r->heat * 5 + (camp_crew_on(c, CR_DRIVER) ? 5 : 0) + (camp_crew_on(c, CR_MUSCLE) ? 5 : 0);
    for (int k = 0; k < DI_COUNT; k++)
        c->heat[k] = (uint8_t)NH_CLAMP((int)c->heat[k] + (k == d ? heat : -6), 0, 100);
    out->heat = heat;
    /* Crew loyalty */
    for (int m = 0; m < CR_COUNT; m++)
        if (camp_crew_on(c, m))
            c->crew_loyalty[m] = (uint8_t)NH_MIN(100, c->crew_loyalty[m] + 12 + (r->grade == 'S' || r->grade == 'A' ? 8 : 0));
    if (o->loyalty_crew && r->outcome == JOB_SUCCESS)
        c->crew_perk2 |= (uint8_t)(1u << (o->loyalty_crew - 1));
    /* Progress */
    if (r->outcome == JOB_BUSTED) {
        c->jobs_failed++;
    } else {
        c->jobs_done++;
        c->district_jobs[d]++;
        if (r->ghost && r->outcome == JOB_SUCCESS)
            c->ghosts++;
    }
    if (o->finale && r->outcome == JOB_SUCCESS && !c->district_done[d]) {
        c->district_done[d] = 1;
        out->district_finished = true;
        if (camp_level(c) == DI_COUNT)
            c->postgame = 1;
        if (d + 1 < DI_COUNT && camp_district_open(c, d + 1))
            out->unlocked_district = true;
    }
    /* History for the anti-staleness rules. */
    memmove(&c->history_fp[1], &c->history_fp[0], sizeof(c->history_fp[0]) * (HISTORY_MAX - 1));
    c->history_fp[0] = fingerprint;
    c->history_n = (uint8_t)NH_MIN(HISTORY_MAX, c->history_n + 1);
    c->sp_hist[2] = c->sp_hist[1];
    c->sp_hist[1] = c->sp_hist[0];
    c->sp_hist[0] = setpieces;
    if (!c->postgame)
        camp_unlock_news(camp_career(c), out->news, (int)sizeof(out->news));
    camp_make_board(c);
}

int32_t camp_upgrade_cost(const NhCampaign *c, int up)
{
    int lvl = c->upgrade[up];
    if (lvl >= nh_upgrades[up].max_level)
        return -1;
    return nh_upgrades[up].cost[lvl];
}

bool camp_upgrade_available(const NhCampaign *c, int up)
{
    return up == UP_THERMAL ? camp_gadget_unlocked(c, GD_THERMAL)
         : up == UP_DARTS   ? camp_gadget_unlocked(c, GD_DART)
                            : true;
}

bool camp_buy_upgrade(NhCampaign *c, int up)
{
    int32_t cost = camp_upgrade_cost(c, up);
    if (cost < 0 || c->money < cost || !camp_upgrade_available(c, up))
        return false;
    c->money -= cost;
    c->upgrade[up]++;
    return true;
}

int32_t camp_hire_cost(const NhCampaign *c, int m)
{
    (void)c;
    return 3000 + nh_crew[m].hire_rep * 60;
}

bool camp_hire(NhCampaign *c, int m)
{
    if ((c->crew_hired >> m) & 1u)
        return false;
    int32_t cost = camp_hire_cost(c, m);
    if (c->reputation < nh_crew[m].hire_rep || c->money < cost)
        return false;
    c->money -= cost;
    c->crew_hired |= (uint8_t)(1u << m);
    return true;
}

bool camp_lie_low(NhCampaign *c)
{
    int32_t cost = 2500 + c->reputation * 10;
    if (c->money < cost)
        return false;
    c->money -= cost;
    for (int d = 0; d < DI_COUNT; d++)
        c->heat[d] = (uint8_t)NH_MAX(0, c->heat[d] - 35);
    camp_make_board(c);
    return true;
}
