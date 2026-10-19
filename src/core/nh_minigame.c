#include "nh_minigame.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "nh_math.h"

/* ---- lockpick -------------------------------------------------------------- */

void lp_init(NhLockpick *lp, int level, NhRng *rng)
{
    memset(lp, 0, sizeof(*lp));
    level = NH_CLAMP(level, 1, 3);
    lp->npins = NH_MIN(2 + level, LP_MAX_PINS);
    lp->window = NH_DEG2RAD(13.0f - 2.5f * (float)level);
    for (int i = 0; i < lp->npins; i++) {
        /* Spread over +-55 degrees, never two neighbours in the same spot. */
        float t;
        do
            t = NH_DEG2RAD(-55.0f + 110.0f * nh_rng_float(rng));
        while (i > 0 && fabsf(t - lp->target[i - 1]) < NH_DEG2RAD(20.0f));
        lp->target[i] = t;
    }
}

MgEvent lp_tick(NhLockpick *lp, float twist, bool press_a, float dt)
{
    if (lp->done)
        return MG_NONE;
    lp->time += dt;
    if (lp->stun > 0.0f) {
        lp->stun -= dt;
        lp->feel = 0.0f;
        return MG_NONE;
    }
    float err = fabsf(twist - lp->target[lp->cur]);
    lp->feel = NH_CLAMP(1.0f - err / (lp->window * 4.0f), 0.0f, 1.0f);
    if (press_a) {
        if (err <= lp->window) {
            lp->cur++;
            lp->in_spot = false;
            if (lp->cur >= lp->npins) {
                lp->done = true;
                return MG_DONE;
            }
            return MG_SET;
        }
        lp->slips++;
        lp->stun = 0.6f;
        return MG_SLIP;
    }
    /* A tick when the pick slides into the sweet spot. */
    bool in = err <= lp->window;
    bool entered = in && !lp->in_spot;
    lp->in_spot = in;
    return entered ? MG_TICK : MG_NONE;
}

/* ---- safecrack -------------------------------------------------------------- */

void sc_init(NhSafecrack *sc, int tier, NhRng *rng)
{
    memset(sc, 0, sizeof(*sc));
    tier = NH_CLAMP(tier, 1, 3);
    sc->nnums = NH_MIN(1 + tier, SC_MAX_NUMS);
    for (int i = 0; i < sc->nnums; i++)
        sc->combo[i] = (int)nh_rng_range(rng, 100);
    sc->dial = sc->last_dial = 0.0f;
    sc->tol = 1;
}

int sc_direction(const NhSafecrack *sc) { return (sc->cur & 1) ? -1 : 1; }

static float wrap100(float v)
{
    while (v < 0.0f)
        v += 100.0f;
    while (v >= 100.0f)
        v -= 100.0f;
    return v;
}

MgEvent sc_tick(NhSafecrack *sc, float twist, bool press_a, float dt)
{
    if (sc->done)
        return MG_NONE;
    sc->time += dt;
    sc->clunk = fmaxf(0.0f, sc->clunk - dt * 3.0f);
    /* Twist sets the speed: a dead zone, then up to 40 numbers a second. */
    float t = twist / NH_DEG2RAD(60.0f);
    t = NH_CLAMP(t, -1.0f, 1.0f);
    float speed = fabsf(t) < 0.08f ? 0.0f : (t > 0 ? 1.0f : -1.0f) * (fabsf(t) - 0.08f) / 0.92f * 40.0f;
    sc->last_dial = sc->dial;
    float delta = speed * dt;
    sc->dial = wrap100(sc->dial + delta);
    MgEvent ev = MG_NONE;
    int want = sc_direction(sc);
    /* Did we sweep over the target number in the wanted direction? */
    if (delta != 0.0f && ((delta > 0) == (want > 0))) {
        float target = (float)sc->combo[sc->cur] + 0.5f;
        float a = sc->last_dial, b = a + delta; /* unwrapped */
        for (int k = -1; k <= 1; k++) {
            float tt = target + 100.0f * (float)k;
            if ((tt - a) * (tt - b) <= 0.0f) {
                sc->passed = true;
                sc->clunk = 1.0f;
                ev = MG_TICK;
            }
        }
    }
    if (press_a) {
        int at = (int)sc->dial;
        int d = abs(at - sc->combo[sc->cur]);
        d = NH_MIN(d, 100 - d);
        if (d <= sc->tol && sc->passed) {
            sc->cur++;
            sc->passed = false;
            if (sc->cur >= sc->nnums) {
                sc->done = true;
                return MG_DONE;
            }
            return MG_SET;
        }
        sc->mistakes++;
        sc->passed = false;
        return MG_SLIP;
    }
    return ev;
}

/* ---- hack ----------------------------------------------------------------- */

void hk_init(NhHack *hk, int tier, NhRng *rng)
{
    memset(hk, 0, sizeof(*hk));
    tier = NH_CLAMP(tier, 1, 3);
    hk->layers = NH_MIN(1 + tier, HK_LAYERS_MAX);
    hk->tol = 0.13f - 0.02f * (float)tier;
    hk->time_limit = 22.0f + 8.0f * (float)hk->layers;
    for (int i = 0; i < hk->layers; i++) {
        hk->freq[i] = 0.15f + 0.7f * nh_rng_float(rng);
        hk->amp[i] = 0.2f + 0.7f * nh_rng_float(rng);
    }
}

MgEvent hk_tick(NhHack *hk, float px, float py, bool valid, float dt)
{
    if (hk->done || hk->failed)
        return MG_NONE;
    hk->time += dt;
    if (hk->time >= hk->time_limit) {
        hk->failed = true;
        return MG_FAIL;
    }
    float tol = hk->tol * (1.0f - 0.15f * (float)hk->cur);
    float df = fabsf(px - hk->freq[hk->cur]), da = fabsf((1.0f - py) - hk->amp[hk->cur]);
    float err = valid ? fmaxf(df, da) : 1.0f;
    hk->match = NH_CLAMP(1.0f - err / (tol * 3.0f), 0.0f, 1.0f);
    if (err < tol)
        hk->lock += dt / 1.2f;
    else
        hk->lock = fmaxf(0.0f, hk->lock - dt * 0.8f);
    if (hk->lock >= 1.0f) {
        hk->lock = 0.0f;
        hk->cur++;
        if (hk->cur >= hk->layers) {
            hk->done = true;
            return MG_DONE;
        }
        return MG_SET;
    }
    return MG_NONE;
}
