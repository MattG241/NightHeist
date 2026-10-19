/* Tests for the campaign layer: save/load (round trip, corruption, version
 * checks), job board rules, crew and upgrades, and the economy curve: a
 * simulated player works through the whole campaign on real generated
 * buildings, and the pacing has to land where PACING.md says. */
#include <stdlib.h>
#include <string.h>

#include "core/nh_campaign.h"
#include "core/nh_save.h"
#include "nh_test.h"

static Level s_L;
static NhPlan s_P;

void test_save(void)
{
    NhCampaign c, c2;
    NhSaveSettings s, s2;
    camp_new(&c, 1234);
    save_default_settings(&s);
    c.money = 98765;
    c.reputation = 321;
    c.heat[2] = 55;
    c.upgrade[UP_BAG] = 2;
    c.crew_loyalty[CR_FACE] = 77;
    c.history_fp[0] = 0xDEADBEEFu;
    c.history_n = 1;
    c.sp_hist[1] = 0x123456789ABCDEFull;
    c.play_time = 4321.5f;
    s.controls.box_w = 0.42f;
    s.music = 3;
    uint8_t buf[SAVE_MAX_BYTES];
    size_t n = save_encode(&c, &s, buf, sizeof(buf));
    CHECK(n > 16 && n < SAVE_MAX_BYTES);
    char why[64];
    CHECK(save_decode(buf, n, &c2, &s2, why, sizeof(why)));
    CHECK(c2.money == 98765 && c2.reputation == 321 && c2.heat[2] == 55 && c2.upgrade[UP_BAG] == 2);
    CHECK(c2.crew_loyalty[CR_FACE] == 77 && c2.history_fp[0] == 0xDEADBEEFu && c2.sp_hist[1] == 0x123456789ABCDEFull);
    CHECK(c2.play_time == 4321.5f && c2.board_n == c.board_n);
    CHECK(c2.board[0].seed == c.board[0].seed && strcmp(c2.board[0].name, c.board[0].name) == 0);
    CHECK(s2.controls.box_w == 0.42f && s2.music == 3);
    /* Byte-exact: encoding the decoded campaign again gives the same bytes. */
    uint8_t buf2[SAVE_MAX_BYTES];
    size_t n2 = save_encode(&c2, &s2, buf2, sizeof(buf2));
    CHECK(n2 == n && memcmp(buf, buf2, n) == 0);
    /* Every single-byte corruption is caught. */
    int caught = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t t[SAVE_MAX_BYTES];
        memcpy(t, buf, n);
        t[i] ^= 0x5A;
        caught += !save_decode(t, n, &c2, &s2, why, sizeof(why));
    }
    CHECK(caught == (int)n);
    CHECK(!save_decode(buf, n - 1, &c2, &s2, why, sizeof(why)));
    uint8_t newer[SAVE_MAX_BYTES];
    memcpy(newer, buf, n);
    newer[5] = SAVE_VERSION + 1;
    CHECK(!save_decode(newer, n, &c2, &s2, why, sizeof(why)) && strstr(why, "newer"));
    CHECK(nh_crc32("123456789", 9) == 0xCBF43926u);
    /* A version-1 save (before the control scheme and tips) still loads:
     * cut those fields out of a v2 payload and fix up the header. */
    c.tips_seen = 0xABCDu;
    s.controls.scheme = NH_CONTROLS_POINTER;
    n = save_encode(&c, &s, buf, sizeof(buf));
    const size_t scheme_at = 16 + 16 + 8;
    const size_t tips_at = scheme_at + 1 + 12 + DI_COUNT * 3 + 4 + CR_COUNT + UP_COUNT + LS_COUNT * 2 + 1 +
                           HISTORY_MAX * 4 + 24 + 6 + 4 + 4 + 4;
    uint8_t v1[SAVE_MAX_BYTES];
    size_t m = 0;
    for (size_t i = 0; i < n; i++)
        if (i != scheme_at && (i < tips_at || i >= tips_at + 4))
            v1[m++] = buf[i];
    uint32_t plen = (uint32_t)(m - 16), crc = nh_crc32(v1 + 16, plen);
    v1[4] = 0, v1[5] = 1;
    v1[8] = (uint8_t)(plen >> 24), v1[9] = (uint8_t)(plen >> 16), v1[10] = (uint8_t)(plen >> 8), v1[11] = (uint8_t)plen;
    v1[12] = (uint8_t)(crc >> 24), v1[13] = (uint8_t)(crc >> 16), v1[14] = (uint8_t)(crc >> 8), v1[15] = (uint8_t)crc;
    CHECK(save_decode(v1, m, &c2, &s2, why, sizeof(why)));
    CHECK(c2.money == 98765 && c2.tips_seen == 0 && s2.controls.scheme == NH_CONTROLS_SIMPLE);
    CHECK(c2.board_n == c.board_n && strcmp(c2.board[0].name, c.board[0].name) == 0);
}

void test_board(void)
{
    NhCampaign c;
    camp_new(&c, 7);
    CHECK(c.board_n >= 3);
    CHECK(camp_district_open(&c, DI_DOCKS) && !camp_district_open(&c, DI_MIDTOWN));
    for (int i = 0; i < c.board_n; i++) {
        CHECK(c.board[i].district == DI_DOCKS);
        CHECK(c.board[i].type == BT_WAREHOUSE || c.board[i].type == BT_OFFICE);
        CHECK(!c.board[i].finale);
    }
    /* Same seed, same board; boards differ from one another. */
    NhCampaign d;
    camp_new(&d, 7);
    CHECK(memcmp(c.board, d.board, sizeof(c.board)) == 0);
    uint32_t first = c.board[0].seed;
    camp_make_board(&c);
    CHECK(c.board[0].seed != first);
    /* Upgrades and hiring obey money and reputation. */
    c.money = 0;
    CHECK(!camp_buy_upgrade(&c, UP_BAG));
    c.money = 100000;
    CHECK(camp_buy_upgrade(&c, UP_BAG) && c.upgrade[UP_BAG] == 1);
    CHECK(!camp_hire(&c, CR_FACE)); /* reputation too low */
    c.reputation = 500;
    CHECK(camp_hire(&c, CR_FACE) && !camp_hire(&c, CR_FACE));
    CHECK(!camp_buy_upgrade(&c, UP_THERMAL)); /* no goggles yet */
    c.jobs_done = 30;
    for (int k = 0; k < 5; k++)
        camp_buy_upgrade(&c, UP_THERMAL);
    CHECK(c.upgrade[UP_THERMAL] == 1);
    c.jobs_done = 0;
    /* Modifiers reach the generator. */
    NhJobOffer o = c.board[0];
    o.mods = (1u << MOD_NIGHT_SHIFT) | (1u << MOD_DOGS);
    NhGenParams gp;
    camp_offer_params(&c, &o, &gp);
    CHECK(gp.mods == o.mods && gp.navoid == c.history_n);
    NhPlan base;
    NhGenParams plain = gp;
    plain.mods = 0;
    CHECK(nh_gen_building(&s_L, &plain, &base));
    CHECK(nh_gen_building(&s_L, &gp, &s_P));
    CHECK(s_P.nguards >= base.nguards + 1);
    bool dog = false;
    for (int i = 0; i < s_P.nguards; i++)
        dog |= s_P.guards[i].arch == ARCH_DOG;
    CHECK(dog);
    /* Crew perks apply to the job. */
    NhJob j;
    job_init(&j, &s_L, &s_P, 1);
    float before = j.police_delay;
    c.crew_selected = (uint8_t)((1u << CR_DRIVER) | (1u << CR_HACKER));
    camp_prepare_job(&c, &o, &j);
    CHECK(j.police_delay > before);
    CHECK(j.codes != 0);
    CHECK(j.bag_cap == 16);
    /* A new career starts with the basic kit; the rest arrives on schedule. */
    CHECK(j.charges[GD_COIN] > 0 && j.charges[GD_EMP] == 0 && j.charges[GD_THERMAL] == 0);
    for (int i = 0; i < c.board_n; i++)
        for (int m = 0; m < MOD_COUNT; m++)
            CHECK(!(c.board[i].mods & (1u << m)) || camp_mod_unlocked(&c, m));
    int gap = 0, max_gap = 0, news = 0;
    for (int step = 1; step <= 30; step++) {
        char buf[64];
        bool any = camp_unlock_news(step, buf, sizeof(buf)) > 0;
        news += any;
        gap = any ? 0 : gap + 1;
        max_gap = NH_MAX(max_gap, gap);
    }
    CHECK(news >= 20 && max_gap <= 1); /* never two jobs running without something new */
    /* Walking away moves nothing on: no career step, no loyalty, no news. */
    NhCampaign w;
    camp_new(&w, 5);
    w.crew_selected = 1u << CR_DRIVER;
    NhJobOffer wo = w.board[0];
    NhGenParams wp;
    camp_offer_params(&w, &wo, &wp);
    CHECK(nh_gen_building(&s_L, &wp, &s_P));
    NhJob wj;
    job_init(&wj, &s_L, &s_P, wo.seed);
    NhJobResult ab = { 0 };
    ab.outcome = JOB_ABORTED;
    NhPayday wpay;
    uint8_t loyal = w.crew_loyalty[CR_DRIVER];
    camp_finish_job(&w, &wo, &ab, &wj, &s_L, &s_P, s_P.fingerprint, s_P.setpieces, &wpay);
    CHECK(camp_career(&w) == 0 && w.crew_loyalty[CR_DRIVER] == loyal && !wpay.news[0]);
}

/* A plausible player: grade improves with upgrades, gets worse with security. */
static void simulate_result(NhRng *r, const NhCampaign *c, const NhJobOffer *o, const NhPlan *p, NhJob *j,
                            NhJobResult *res)
{
    int skill = 50 + 6 * (c->upgrade[UP_PICKS] + c->upgrade[UP_SHOES] + c->upgrade[UP_GADGETS]) - 8 * o->security;
    int roll = (int)nh_rng_range(r, 100);
    memset(res, 0, sizeof(*res));
    if (roll < 6) {
        res->outcome = JOB_BUSTED;
        res->grade = 'F';
        res->heat = 6;
        return;
    }
    res->outcome = roll < 12 ? JOB_PARTIAL : JOB_SUCCESS;
    int q = skill + (int)nh_rng_range(r, 50) - 20;
    res->grade = q > 80 ? 'S' : q > 62 ? 'A' : q > 45 ? 'B' : q > 30 ? 'C' : 'D';
    res->ghost = res->grade == 'S';
    res->heat = res->ghost ? 0 : 1 + (int)nh_rng_range(r, 4);
    /* Bag the target plus a share of the rest. */
    int loot = 0;
    for (int i = 0; i < p->nent; i++) {
        if (p->ent[i].kind != ENT_LOOT)
            continue;
        bool take = (i == p->objective && res->outcome == JOB_SUCCESS) || nh_rng_range(r, 100) < 45;
        if (take) {
            loot += p->ent[i].value;
            job_mark_taken(j, i);
        }
    }
    res->loot = loot;
    res->bonus = 300;
    int mult = res->grade == 'S' ? 120 : res->grade == 'A' ? 110 : res->grade == 'B' ? 100 : res->grade == 'C' ? 90 : 80;
    res->payout = (loot + res->bonus) * mult / 100;
}

void test_economy(void)
{
    NhCampaign c;
    camp_new(&c, 2024);
    NhRng r;
    nh_rng_seed(&r, 99, 3);
    int unlock_job[DI_COUNT] = { 0, -1, -1, -1 };
    int jobs = 0, min_money = 1 << 30, broke_streak = 0, max_broke = 0;
    float last_new = 0.0f, max_new_gap = 0.0f;
    printf("  job  district      money     rep  heat  upgrades\n");
    while (!c.postgame && jobs < 120) {
        /* Pick: the finale if offered, else a loyalty job, else the best payer in the newest district. */
        int pick = 0;
        for (int i = 0; i < c.board_n; i++) {
            const NhJobOffer *o = &c.board[i];
            if (o->finale) {
                pick = i;
                break;
            }
            if (o->est_payout > c.board[pick].est_payout && !c.board[pick].finale)
                pick = i;
        }
        NhJobOffer o = c.board[pick];
        NhGenParams gp;
        camp_offer_params(&c, &o, &gp);
        CHECK(nh_gen_building(&s_L, &gp, &s_P));
        NhJob j;
        job_init(&j, &s_L, &s_P, o.seed);
        camp_prepare_job(&c, &o, &j);
        NhJobResult res;
        simulate_result(&r, &c, &o, &s_P, &j, &res);
        NhPayday pay;
        camp_finish_job(&c, &o, &res, &j, &s_L, &s_P, s_P.fingerprint, s_P.setpieces, &pay);
        jobs++;
        float job_start = c.play_time;
        c.play_time += 60.0f * (float)(5 + 4 * o.size + 2 * o.security);
        /* Novelty: an unlock reported at payday shows up in the next job; a
         * loyalty job or a finale is new in itself. */
        if (o.loyalty_crew || o.finale)
            last_new = job_start;
        max_new_gap = NH_MAX(max_new_gap, c.play_time - last_new);
        if (pay.news[0] || pay.unlocked_district)
            last_new = c.play_time;
        if (getenv("NH_PACING")) /* the timeline PACING.md is written from */
            printf("  | %d | %.0f | %s | %s %s%s%s | %s%s |\n", jobs, (double)job_start / 60.0,
                   nh_districts[o.district].name, nh_size_names[o.size], nh_building_names[o.type],
                   o.finale ? " (finale)" : "", o.loyalty_crew ? " (loyalty)" : "", pay.news,
                   pay.unlocked_district ? "  New district" : "");
        for (int d = 1; d < DI_COUNT; d++)
            if (unlock_job[d] < 0 && camp_district_open(&c, d))
                unlock_job[d] = jobs;
        /* Spend: upgrades (cheapest first), crew, lie low when hot. */
        for (int pass = 0; pass < 3; pass++) {
            int best = -1;
            int32_t bc = 1 << 30;
            for (int u = 0; u < UP_COUNT; u++) {
                int32_t cost = camp_upgrade_cost(&c, u);
                if (cost > 0 && cost < bc && cost <= c.money / 2 && camp_upgrade_available(&c, u)) {
                    bc = cost;
                    best = u;
                }
            }
            if (best >= 0)
                camp_buy_upgrade(&c, best);
        }
        for (int m = 0; m < CR_COUNT; m++)
            if (camp_hire_cost(&c, m) < c.money / 3)
                camp_hire(&c, m);
        int hottest = 0;
        for (int d = 0; d < DI_COUNT; d++)
            hottest = NH_MAX(hottest, c.heat[d]);
        if (hottest > 70)
            camp_lie_low(&c);
        /* Bring the two cheapest hired crew. */
        c.crew_selected = (uint8_t)((1u << CR_DRIVER) | (c.crew_hired & (1u << CR_FACE) ? 1u << CR_FACE : 1u << CR_HACKER));
        min_money = NH_MIN(min_money, c.money);
        broke_streak = c.money < 100 ? broke_streak + 1 : 0;
        max_broke = NH_MAX(max_broke, broke_streak);
        int ups = 0;
        for (int u = 0; u < UP_COUNT; u++)
            ups += c.upgrade[u];
        if (jobs % 3 == 0 || pay.district_finished)
            printf("  %3d  %-12s %7d  %5d  %4d  %d%s\n", jobs, nh_districts[o.district].name + (strncmp(nh_districts[o.district].name, "The ", 4) ? 0 : 4), c.money * 10,
                   c.reputation, c.heat[o.district], ups, pay.district_finished ? "  <- finale" : "");
    }
    int ups = 0, max_ups = 0;
    for (int u = 0; u < UP_COUNT; u++) {
        ups += c.upgrade[u];
        max_ups += nh_upgrades[u].max_level;
    }
    printf("  post-game after %d jobs, %.1f hours, $%d earned, %d/%d upgrades, %d ghosts\n", jobs,
           (double)c.play_time / 3600.0, (int)c.total_earned * 10, ups, max_ups, c.ghosts);
    printf("  districts opened at jobs %d %d %d; longest stretch without something new: %.0f min\n", unlock_job[1],
           unlock_job[2], unlock_job[3], (double)max_new_gap / 60.0);
    CHECK(c.postgame);
    CHECK(jobs >= 20 && jobs <= 60);
    CHECK(c.play_time / 3600.0f >= 5.0f && c.play_time / 3600.0f <= 10.0f);
    CHECK(unlock_job[1] >= 4 && unlock_job[1] <= 14);
    CHECK(unlock_job[2] > unlock_job[1] && unlock_job[3] > unlock_job[2]);
    CHECK(min_money >= 0);
    CHECK(max_new_gap <= 45.0f * 60.0f); /* PACING.md: something new every 30-45 min */
    CHECK(max_broke <= 2);               /* never stuck without money for long */
    CHECK(ups * 10 >= max_ups * 6);      /* most upgrades bought by the end */
}
