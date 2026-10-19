/* Tests for heist systems: minigames (an ideal player always succeeds,
 * mistakes are punished), job objectives and rating, and security
 * (cameras, lasers, power, alarmed doors). */
#include <math.h>
#include <string.h>

#include "core/nh_job.h"
#include "core/nh_minigame.h"
#include "core/nh_nav.h"
#include "core/nh_security.h"
#include "nh_test.h"

static const float DT = 1.0f / 60.0f;

void test_minigames(void)
{
    NhRng rng;
    nh_rng_seed(&rng, 5, 1);
    for (int level = 1; level <= 3; level++) {
        /* Lockpick: twist to each pin, then press A. */
        NhLockpick lp;
        lp_init(&lp, level, &rng);
        CHECK(lp.npins == 2 + level);
        int guard = 0;
        MgEvent ev = MG_NONE;
        float twist = 0.0f;
        while (!lp.done && guard++ < 6000) {
            float target = lp.target[lp.cur];
            twist = nh_approach(twist, target, NH_DEG2RAD(90.0f) * DT);
            bool press = fabsf(twist - target) < 1e-4f && lp.stun <= 0.0f;
            ev = lp_tick(&lp, twist, press, DT);
        }
        CHECK(lp.done && ev == MG_DONE);
        CHECK(lp.slips == 0);
        /* Pressing A in the wrong place slips and stuns. */
        lp_init(&lp, level, &rng);
        float wrong = lp.target[0] + lp.window * 3.0f;
        CHECK(lp_tick(&lp, wrong, true, DT) == MG_SLIP);
        CHECK(lp.cur == 0 && lp.slips == 1 && lp.stun > 0.0f);
        CHECK(lp_tick(&lp, lp.target[0], true, DT) == MG_NONE); /* still stunned */

        /* Safecrack: spin the right way until the clunk, stop, press A. */
        NhSafecrack sc;
        sc_init(&sc, level, &rng);
        CHECK(sc.nnums == 1 + level);
        guard = 0;
        while (!sc.done && guard++ < 20000) {
            float tw = (float)sc_direction(&sc) * NH_DEG2RAD(20.0f);
            ev = sc_tick(&sc, tw, false, DT);
            if (ev == MG_TICK)
                ev = sc_tick(&sc, 0.0f, true, DT);
        }
        CHECK(sc.done);
        CHECK(sc.mistakes == 0);
        /* Stopping on the right number without having passed it in the
         * right direction doesn't count. */
        sc_init(&sc, level, &rng);
        sc.dial = (float)sc.combo[0];
        CHECK(sc_tick(&sc, 0.0f, true, DT) == MG_SLIP);

        /* Hack: hold the pointer on the signal. */
        NhHack hk;
        hk_init(&hk, level, &rng);
        guard = 0;
        while (!hk.done && !hk.failed && guard++ < 10000)
            hk_tick(&hk, hk.freq[hk.cur], 1.0f - hk.amp[hk.cur], true, DT);
        CHECK(hk.done && !hk.failed);
        CHECK(hk.time < hk.time_limit * 0.5f);
        /* Waving around never finishes and times out into a trace. */
        hk_init(&hk, level, &rng);
        guard = 0;
        while (!hk.done && !hk.failed && guard++ < 100000)
            hk_tick(&hk, 1.0f - hk.freq[hk.cur] > 0.5f ? 0.0f : 1.0f, 0.5f, true, DT);
        CHECK(hk.failed);
    }
}

static Level s_L;
static NhPlan s_P;

void test_job(void)
{
    NhGenParams gp;
    memset(&gp, 0, sizeof(gp));
    gp.seed = 77;
    gp.type = BT_BANK;
    gp.size = BS_MEDIUM;
    gp.security = 2;
    CHECK(nh_gen_building(&s_L, &gp, &s_P));
    NhJob j;
    job_init(&j, &s_L, &s_P, 1);
    CHECK(j.nobj >= 2);
    CHECK(j.obj[0].type == OBJ_STEAL && !j.obj[0].optional && j.obj[0].ent == s_P.objective);
    CHECK(strstr(j.obj[0].text, "Steal the") != NULL);
    CHECK(j.charges[GD_LOCKPICK] == 255 && j.charges[GD_COIN] > 0);
    CHECK(!job_main_done(&j));
    /* Taking the target completes the main objective. */
    job_event(&j, &s_P, JE_LOOT, s_P.objective, s_P.ent[s_P.objective].value);
    CHECK(job_main_done(&j));
    CHECK(job_taken(&j, s_P.objective));
    CHECK(j.bag_value == s_P.ent[s_P.objective].value);
    job_event(&j, &s_P, JE_EXTRACT, -1, 0);
    CHECK(j.outcome == JOB_SUCCESS);
    NhJobResult r;
    job_result(&j, &r);
    CHECK(r.ghost && r.stealth >= 100 && r.grade == 'S');
    CHECK(r.payout >= r.loot);
    CHECK(r.heat == 0);

    /* Being spotted and raising the alarm cost stealth and add heat; the
     * alarm starts the police clock. */
    job_init(&j, &s_L, &s_P, 2);
    job_event(&j, &s_P, JE_SPOTTED, -1, 0);
    job_event(&j, &s_P, JE_ALARM, -1, 0);
    CHECK(j.alarm && j.police_eta > 0.0f);
    for (int i = 0; i < 60 * 80; i++)
        job_tick(&j, DT);
    CHECK(j.police_here);
    job_event(&j, &s_P, JE_LOOT, s_P.objective, 100);
    job_event(&j, &s_P, JE_EXTRACT, -1, 0);
    job_result(&j, &r);
    CHECK(!r.ghost && r.stealth < 70 && r.grade != 'S' && r.heat >= 4);
    for (int i = 0; i < j.nobj; i++)
        if (j.obj[i].type == OBJ_GHOST || j.obj[i].type == OBJ_NO_ALARM)
            CHECK(j.obj[i].failed && !j.obj[i].done);

    /* Leaving without the target is a partial; getting caught pays nothing. */
    job_init(&j, &s_L, &s_P, 3);
    job_event(&j, &s_P, JE_EXTRACT, -1, 0);
    CHECK(j.outcome == JOB_PARTIAL);
    job_result(&j, &r);
    CHECK(r.bonus == 0); /* walking straight back to the van earns nothing */
    job_init(&j, &s_L, &s_P, 4);
    job_event(&j, &s_P, JE_LOOT, s_P.objective, 500);
    job_event(&j, &s_P, JE_CAUGHT, -1, 0);
    job_result(&j, &r);
    CHECK(r.outcome == JOB_BUSTED && r.grade == 'F' && r.payout == 0 && r.heat >= 6);
    /* Events after the end change nothing. */
    job_event(&j, &s_P, JE_EXTRACT, -1, 0);
    CHECK(j.outcome == JOB_BUSTED);
    /* Better stealth never pays less (same haul). */
    int last = 1 << 30;
    for (int spotted = 0; spotted < 6; spotted++) {
        job_init(&j, &s_L, &s_P, 9);
        for (int k = 0; k < spotted; k++)
            job_event(&j, &s_P, JE_SPOTTED, -1, 0);
        job_event(&j, &s_P, JE_LOOT, s_P.objective, 1000);
        job_event(&j, &s_P, JE_EXTRACT, -1, 0);
        job_result(&j, &r);
        CHECK(r.payout <= last);
        last = r.payout;
    }
}

static NhNav s_nav;
static AiWorld s_ai;

void test_security(void)
{
    NhGenParams gp;
    memset(&gp, 0, sizeof(gp));
    gp.type = BT_MUSEUM;
    gp.size = BS_LARGE;
    gp.security = 4;
    /* Find a seed with both cameras and lasers. */
    NhSecurity sec;
    for (gp.seed = 1; gp.seed < 200; gp.seed++) {
        CHECK(nh_gen_building(&s_L, &gp, &s_P));
        sec_init(&sec, &s_L, &s_P);
        if (sec.ncam >= 2 && sec.nlas >= 2)
            break;
    }
    CHECK(sec.ncam >= 2 && sec.nlas >= 2);
    nh_nav_build(&s_nav, &s_L, 0.3f);
    ai_init(&s_ai, &s_L, &s_nav, 1);
    /* Standing lit right under a camera's gaze gets you spotted. */
    SecCamera *c = &sec.cam[0];
    NhVec3 look = nh_dir_from_yaw_pitch(c->base_yaw, 0);
    NhVec3 spot = nh_v3(c->pos.x + look.x * 3.0f, lv_floor_y(lv_floor_of(c->pos.y)), c->pos.z + look.z * 3.0f);
    AiPlayerView pv;
    memset(&pv, 0, sizeof(pv));
    pv.pos = spot;
    pv.head = nh_v3(spot.x, spot.y + 1.65f, spot.z);
    pv.height = 1.85f;
    pv.light = 1.0f;
    pv.speed = 1.5f;
    pv.hiding_prop = -1;
    bool seen = false;
    NhVec3 where;
    for (int i = 0; i < 60 * 30 && !seen; i++)
        seen = (sec_tick(&sec, &s_L, &s_ai, &pv, DT, &where) & SEC_EV_CAMERA) != 0;
    CHECK(seen);
    /* Cut the security circuit: cameras and lasers go dead. */
    s_L.power[sec.security_circuit] = 0;
    CHECK(!sec_camera_live(&sec, &s_L, 0));
    CHECK(!sec_laser_live(&sec, &s_L, 0));
    seen = false;
    for (int i = 0; i < 60 * 30 && !seen; i++)
        seen = (sec_tick(&sec, &s_L, &s_ai, &pv, DT, &where) & SEC_EV_CAMERA) != 0;
    CHECK(!seen);
    s_L.power[sec.security_circuit] = 1;
    /* A jammed camera sees nothing either. */
    sec.cam[0].jam_t = 30.0f;
    CHECK(!sec_camera_live(&sec, &s_L, 0));
    sec.cam[0].jam_t = 0.0f;
    /* Smoke between the camera and you blocks it. */
    float dist;
    pv.crouched = false;
    float v0 = ai_point_visibility(&s_ai, c->pos, nh_v3_norm(nh_v3_sub(pv.pos, c->pos)), 60.0f, 15.0f, &pv, &dist);
    ai_add_smoke(&s_ai, nh_v3_lerp(c->pos, nh_v3(pv.pos.x, pv.pos.y + 1.0f, pv.pos.z), 0.5f), 1.5f, 10.0f);
    float v1 = ai_point_visibility(&s_ai, c->pos, nh_v3_norm(nh_v3_sub(pv.pos, c->pos)), 60.0f, 15.0f, &pv, &dist);
    CHECK(v0 > 0.0f && v1 == 0.0f);
    /* Walking through a laser trips it; stepping round it doesn't. */
    SecLaser *l = &sec.las[0];
    NhVec3 mid = nh_v3_lerp(l->a, l->b, 0.5f);
    pv.pos = nh_v3(mid.x, lv_floor_y(lv_floor_of(mid.y)), mid.z);
    CHECK(sec_tick(&sec, &s_L, &s_ai, &pv, DT, &where) & SEC_EV_LASER);
    l->mirrored = 1;
    CHECK(!(sec_tick(&sec, &s_L, &s_ai, &pv, DT, &where) & SEC_EV_LASER));
    CHECK(sec_segment_capsule(nh_v3(-1, 1, 0), nh_v3(1, 1, 0), nh_v3(0, 0, 0), 1.8f) < 0.01f);
    CHECK(sec_segment_capsule(nh_v3(-1, 2.5f, 0), nh_v3(1, 2.5f, 0), nh_v3(0, 0, 0), 1.8f) > 0.6f);
    /* Alarmed doors: 15 s to disarm. */
    int door = -1;
    for (int i = 0; i < s_L.ndoors; i++)
        if (s_L.doors[i].flags & LV_DOOR_F_ALARMED)
            door = i;
    CHECK(door >= 0);
    pv.in_vent = true;
    sec_door_opened(&sec, &s_L, door);
    bool timeout = false;
    for (int i = 0; i < 60 * 16 && !timeout; i++)
        timeout = (sec_tick(&sec, &s_L, &s_ai, &pv, DT, &where) & SEC_EV_ENTRY_TIMEOUT) != 0;
    CHECK(timeout);
    sec_door_opened(&sec, &s_L, door);
    sec_disarm(&sec);
    timeout = false;
    for (int i = 0; i < 60 * 16 && !timeout; i++)
        timeout = (sec_tick(&sec, &s_L, &s_ai, &pv, DT, &where) & SEC_EV_ENTRY_TIMEOUT) != 0;
    CHECK(!timeout);
}
