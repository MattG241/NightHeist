#include "nh_synth.h"

#include <math.h>
#include <string.h>

#include "nh_rng.h"

/* ---- the note renderer ----------------------------------------------------- */

enum { W_SINE, W_TRI, W_SAW, W_SQUARE, W_NOISE };

typedef struct Note {
    float t;              /* start, seconds */
    float f0, f1;         /* pitch in Hz, gliding exponentially to f1 (0 = steady) */
    float glide;          /* glide time in seconds, 0 = the whole note */
    int wave;
    float amp;
    float att, dec, sus;  /* attack (linear), decay time constant, sustain level */
    float hold, rel;      /* sus > 0: gate length from the start, then a linear release */
    float lp, lp1;        /* two one-pole low-passes, cutoff swept from lp to lp1; 0 = off */
    float hp;             /* one-pole high-pass, 0 = off */
    float bp, q;          /* band-pass centre and Q, 0 = off */
    float drive;          /* tanh saturation, 0 = off */
    float detune;         /* a second oscillator at f * (1 + detune) */
    float vib, vib_hz;    /* vibrato depth (fraction of f) and rate */
    float trem, trem_hz;  /* tremolo depth and rate */
} Note;

typedef struct Buf {
    float *s;
    int n;
    bool wrap; /* loops: tails fold back onto the start */
    NhRng rng;
} Buf;

#define SR ((float)NH_SYNTH_RATE)
#define TAU 6.28318531f

static float s_sin[1025];

static void init_tables(void)
{
    if (s_sin[256] > 0.5f)
        return;
    for (int i = 0; i <= 1024; i++)
        s_sin[i] = sinf((float)i * TAU / 1024.0f);
}

static inline float tsin(float ph)
{
    float x = ph * 1024.0f;
    int i = (int)x;
    float f = x - (float)i;
    i &= 1023;
    return s_sin[i] + (s_sin[i + 1] - s_sin[i]) * f;
}

static inline void put(Buf *b, int i, float v)
{
    if (b->wrap) {
        i %= b->n;
        if (i < 0)
            i += b->n;
    } else if (i < 0 || i >= b->n) {
        return;
    }
    b->s[i] += v;
}

static float osc(int wave, float ph, NhRng *r)
{
    switch (wave) {
    case W_SINE: return tsin(ph);
    case W_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case W_SAW: return 2.0f * ph - 1.0f;
    case W_SQUARE: return ph < 0.5f ? 1.0f : -1.0f;
    default: return nh_rng_float(r) * 2.0f - 1.0f;
    }
}

static float note_length(const Note *n)
{
    if (n->sus > 0.0f)
        return NH_MAX(n->hold, n->att) + n->rel;
    return n->att + n->dec * 7.0f;
}

static void note(Buf *b, const Note *n)
{
    int len = (int)(note_length(n) * SR) + 1;
    int start = (int)(n->t * SR);
    float f = n->f0, k = 1.0f;
    int glide_n = (int)((n->glide > 0.0f ? n->glide : note_length(n)) * SR) + 1;
    if (n->f1 > 0.0f && n->f0 > 0.0f)
        k = powf(n->f1 / n->f0, 1.0f / (float)glide_n);
    float lpc = n->lp, lpk = 1.0f;
    if (n->lp > 0.0f && n->lp1 > 0.0f)
        lpk = powf(n->lp1 / n->lp, 1.0f / (float)len);
    float hpw = TAU * n->hp / SR, hpa = hpw / (1.0f + hpw);
    /* Band-pass: the topology-preserving state-variable filter (stable up to Nyquist). */
    float g = 0.0f, kq = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
    if (n->bp > 0.0f) {
        g = tanf(3.14159265f * NH_MIN(n->bp, SR * 0.45f) / SR);
        kq = 1.0f / (n->q > 0.0f ? n->q : 1.0f);
        a1 = 1.0f / (1.0f + g * (g + kq));
        a2 = g * a1;
        a3 = g * a2;
    }
    float att_n = n->att * SR, hold_n = NH_MAX(n->hold, n->att) * SR, rel_n = n->rel * SR;
    float dk = n->dec > 0.0f ? expf(-1.0f / (n->dec * SR)) : 0.0f;
    float d = 1.0f; /* decaying part of the envelope, 1 -> 0 */
    float ph = 0.0f, ph2 = 0.0f, lp1s = 0.0f, lp2s = 0.0f, hps = 0.0f;
    float vibp = 0.0f, tremp = 0.0f;
    for (int i = 0; i < len; i++) {
        float fi = (float)i;
        float env;
        if (fi < att_n)
            env = fi / att_n;
        else {
            d *= dk;
            env = n->sus > 0.0f ? n->sus + (1.0f - n->sus) * d : d;
            if (n->sus > 0.0f && fi >= hold_n)
                env *= rel_n > 0.0f ? NH_MAX(0.0f, 1.0f - (fi - hold_n) / rel_n) : 0.0f;
        }
        float fr = f;
        if (n->vib > 0.0f) {
            fr *= 1.0f + n->vib * tsin(vibp);
            vibp += n->vib_hz / SR;
            vibp -= (float)(int)vibp;
        }
        float x = osc(n->wave, ph, &b->rng);
        if (n->detune != 0.0f)
            x = 0.5f * (x + osc(n->wave, ph2, &b->rng));
        ph += fr / SR;
        ph -= (float)(int)ph;
        ph2 += fr * (1.0f + n->detune) / SR;
        ph2 -= (float)(int)ph2;
        if (i < glide_n)
            f *= k;
        if (n->bp > 0.0f) {
            float v3 = x - ic2;
            float v1 = a1 * ic1 + a2 * v3;
            float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            x = v1;
        }
        if (n->lp > 0.0f) {
            float w = TAU * NH_MIN(lpc, SR * 0.45f) / SR, a = w / (1.0f + w);
            lp1s += a * (x - lp1s);
            lp2s += a * (lp1s - lp2s);
            x = lp2s;
            lpc *= lpk;
        }
        if (n->hp > 0.0f) {
            hps += hpa * (x - hps);
            x -= hps;
        }
        if (n->drive > 0.0f)
            x = tanhf(x * n->drive) / tanhf(n->drive);
        if (n->trem > 0.0f) {
            x *= 1.0f - n->trem * 0.5f * (1.0f + tsin(tremp));
            tremp += n->trem_hz / SR;
            tremp -= (float)(int)tremp;
        }
        put(b, start + i, x * env * n->amp);
    }
}

#define N(...) note(b, &(Note){ __VA_ARGS__ })

static float midi(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }

/* Whole cycles per loop, so held tones close the loop without a click. */
static float loop_freq(const Buf *b, float f)
{
    float secs = (float)b->n / SR;
    return floorf(f * secs + 0.5f) / secs;
}

/* ---- small instruments -------------------------------------------------------- */

static void click(Buf *b, float t, float amp, float hp)
{
    N(.t = t, .wave = W_NOISE, .amp = amp, .dec = 0.004f, .hp = hp);
}

static void thump(Buf *b, float t, float f0, float f1, float dec, float amp)
{
    N(.t = t, .f0 = f0, .f1 = f1, .glide = dec * 2.0f, .wave = W_SINE, .amp = amp, .att = 0.002f, .dec = dec);
}

static void noise_burst(Buf *b, float t, float amp, float att, float dec, float bp, float q)
{
    N(.t = t, .wave = W_NOISE, .amp = amp, .att = att, .dec = dec, .bp = bp, .q = q);
}

static void ring(Buf *b, float t, float f, float dec, float amp)
{
    N(.t = t, .f0 = f, .wave = W_SINE, .amp = amp, .att = 0.001f, .dec = dec);
}

static void beep(Buf *b, float t, float f, float len, float amp)
{
    N(.t = t, .f0 = f, .wave = W_SQUARE, .amp = amp, .att = 0.003f, .sus = 1.0f, .hold = len, .rel = 0.01f, .lp = 5000.0f);
}

/* A vowel: a buzzy source through two formant band-passes. */
static void vowel(Buf *b, float t, float f0, float f1, float len, float amp, float fa, float fb)
{
    N(.t = t, .f0 = f0, .f1 = f1, .wave = W_SAW, .amp = amp, .att = 0.03f, .sus = 0.8f, .hold = len, .rel = 0.08f, .bp = fa,
      .q = 4.0f, .vib = 0.02f, .vib_hz = 5.5f);
    N(.t = t, .f0 = f0, .f1 = f1, .wave = W_SAW, .amp = amp * 0.6f, .att = 0.03f, .sus = 0.8f, .hold = len, .rel = 0.08f,
      .bp = fb, .q = 6.0f, .vib = 0.02f, .vib_hz = 5.5f);
}

static void tinkle(Buf *b, float t, float spread, int count, float amp)
{
    for (int i = 0; i < count; i++) {
        float f = 2800.0f + 3200.0f * nh_rng_float(&b->rng);
        ring(b, t + spread * nh_rng_float(&b->rng), f, 0.05f + 0.06f * nh_rng_float(&b->rng), amp);
    }
}

static void kick(Buf *b, float t, float amp) { thump(b, t, 140.0f, 42.0f, 0.12f, amp), click(b, t, amp * 0.3f, 2000.0f); }

static void snare(Buf *b, float t, float amp)
{
    noise_burst(b, t, amp, 0.001f, 0.06f, 2200.0f, 0.7f);
    thump(b, t, 210.0f, 170.0f, 0.04f, amp * 0.6f);
}

static void hat(Buf *b, float t, float amp) { N(.t = t, .wave = W_NOISE, .amp = amp, .dec = 0.012f, .hp = 7000.0f); }

/* ---- sound effects -------------------------------------------------------------- */

static void fx_step(Buf *b, int surf)
{
    switch (surf) {
    case 0: /* concrete */
        noise_burst(b, 0, 0.8f, 0.002f, 0.012f, 1400.0f, 1.2f);
        thump(b, 0, 110.0f, 70.0f, 0.015f, 0.5f);
        break;
    case 1: /* carpet */ N(.wave = W_NOISE, .amp = 0.7f, .att = 0.01f, .dec = 0.018f, .lp = 450.0f); break;
    case 2: /* tile */
        N(.wave = W_NOISE, .amp = 0.6f, .dec = 0.008f, .hp = 2500.0f);
        ring(b, 0, 2900.0f, 0.01f, 0.15f);
        thump(b, 0, 120.0f, 90.0f, 0.012f, 0.3f);
        break;
    case 3: /* wood */
        thump(b, 0, 190.0f, 150.0f, 0.022f, 0.6f);
        noise_burst(b, 0, 0.4f, 0.002f, 0.014f, 900.0f, 1.5f);
        break;
    case 4: /* marble */
        N(.wave = W_NOISE, .amp = 0.7f, .dec = 0.006f, .hp = 3000.0f);
        ring(b, 0, 3400.0f, 0.02f, 0.12f);
        ring(b, 0, 4700.0f, 0.018f, 0.08f);
        break;
    case 5: /* metal */
        ring(b, 0, 420.0f, 0.08f, 0.4f);
        ring(b, 0, 1130.0f, 0.06f, 0.3f);
        ring(b, 0, 2210.0f, 0.04f, 0.2f);
        ring(b, 0, 3050.0f, 0.03f, 0.1f);
        click(b, 0, 0.4f, 2000.0f);
        break;
    case 6: /* asphalt */
        noise_burst(b, 0, 0.6f, 0.002f, 0.012f, 2200.0f, 1.0f);
        N(.wave = W_NOISE, .amp = 0.4f, .dec = 0.015f, .lp = 300.0f);
        break;
    default: /* grass */
        N(.wave = W_NOISE, .amp = 0.5f, .att = 0.015f, .dec = 0.025f, .hp = 3500.0f);
        noise_burst(b, 0, 0.3f, 0.01f, 0.02f, 1500.0f, 1.0f);
        break;
    }
}

static void fx_land(Buf *b)
{
    thump(b, 0, 90.0f, 40.0f, 0.045f, 0.8f);
    N(.wave = W_NOISE, .amp = 0.5f, .dec = 0.03f, .lp = 600.0f);
}

static void fx_door_open(Buf *b)
{
    click(b, 0, 0.6f, 3000.0f);
    click(b, 0.02f, 0.4f, 2500.0f);
    N(.t = 0.06f, .f0 = 170.0f, .f1 = 240.0f, .wave = W_SAW, .amp = 0.5f, .att = 0.06f, .sus = 0.6f, .hold = 0.45f,
      .rel = 0.12f, .bp = 900.0f, .q = 7.0f, .vib = 0.04f, .vib_hz = 11.0f);
}

static void fx_door_close(Buf *b)
{
    thump(b, 0, 85.0f, 50.0f, 0.04f, 0.9f);
    N(.wave = W_NOISE, .amp = 0.4f, .dec = 0.025f, .lp = 800.0f);
    click(b, 0.03f, 0.5f, 3000.0f);
}

static void fx_door_locked(Buf *b)
{
    for (int i = 0; i < 3; i++) {
        noise_burst(b, (float)i * 0.07f, 0.6f, 0.001f, 0.006f, 2500.0f, 2.0f);
        ring(b, (float)i * 0.07f, 900.0f + 60.0f * (float)i, 0.015f, 0.2f);
    }
}

static void fx_door_vault(Buf *b)
{
    N(.f0 = 48.0f, .wave = W_SAW, .amp = 0.5f, .att = 0.3f, .sus = 0.8f, .hold = 1.6f, .rel = 0.6f, .lp = 220.0f);
    ring(b, 1.5f, 210.0f, 0.4f, 0.4f);
    ring(b, 1.5f, 530.0f, 0.3f, 0.3f);
    thump(b, 1.5f, 70.0f, 40.0f, 0.1f, 0.8f);
    N(.wave = W_NOISE, .amp = 0.15f, .att = 0.2f, .sus = 0.5f, .hold = 1.4f, .rel = 0.3f, .bp = 3000.0f, .q = 0.8f);
}

static void fx_pick_tick(Buf *b)
{
    click(b, 0, 0.6f, 4000.0f);
    ring(b, 0, 5200.0f, 0.004f, 0.25f);
}

static void fx_pick_set(Buf *b)
{
    click(b, 0, 0.8f, 3000.0f);
    ring(b, 0, 1800.0f, 0.012f, 0.4f);
    thump(b, 0.005f, 400.0f, 300.0f, 0.01f, 0.3f);
}

static void fx_pick_slip(Buf *b)
{
    N(.wave = W_NOISE, .amp = 0.5f, .att = 0.01f, .sus = 0.6f, .hold = 0.12f, .rel = 0.04f, .bp = 3200.0f, .q = 2.0f);
    ring(b, 0.1f, 2600.0f, 0.02f, 0.5f);
    ring(b, 0.1f, 3900.0f, 0.015f, 0.25f);
}

static void fx_unlock(Buf *b)
{
    click(b, 0, 0.6f, 3000.0f);
    click(b, 0.04f, 0.7f, 2500.0f);
    thump(b, 0.05f, 150.0f, 90.0f, 0.025f, 0.6f);
    ring(b, 0.05f, 1400.0f, 0.03f, 0.2f);
}

static void fx_safe_click(Buf *b)
{
    ring(b, 0, 3000.0f, 0.003f, 0.4f);
    click(b, 0, 0.3f, 5000.0f);
}

static void fx_safe_set(Buf *b)
{
    ring(b, 0, 700.0f, 0.02f, 0.6f);
    click(b, 0, 0.6f, 2000.0f);
    thump(b, 0, 220.0f, 160.0f, 0.02f, 0.4f);
}

static void fx_safe_open(Buf *b)
{
    thump(b, 0, 120.0f, 70.0f, 0.04f, 0.8f);
    click(b, 0, 0.5f, 2000.0f);
    N(.t = 0.12f, .f0 = 120.0f, .f1 = 95.0f, .wave = W_SAW, .amp = 0.6f, .att = 0.05f, .sus = 0.6f, .hold = 0.6f,
      .rel = 0.15f, .bp = 500.0f, .q = 5.0f, .vib = 0.05f, .vib_hz = 8.0f);
}

static void fx_keypad_ok(Buf *b)
{
    beep(b, 0, 1850.0f, 0.06f, 0.4f);
    beep(b, 0.1f, 2470.0f, 0.1f, 0.4f);
}

static void fx_keypad_bad(Buf *b)
{
    N(.f0 = 300.0f, .wave = W_SQUARE, .amp = 0.4f, .att = 0.005f, .sus = 1.0f, .hold = 0.3f, .rel = 0.02f, .detune = 0.03f,
      .lp = 2500.0f);
}

static void fx_swipe(Buf *b)
{
    N(.wave = W_NOISE, .amp = 0.4f, .att = 0.03f, .sus = 0.7f, .hold = 0.12f, .rel = 0.04f, .lp = 1500.0f, .lp1 = 5000.0f);
    beep(b, 0.2f, 2470.0f, 0.06f, 0.35f);
}

static void fx_hack_ok(Buf *b)
{
    beep(b, 0.0f, 880.0f, 0.04f, 0.35f);
    beep(b, 0.05f, 1320.0f, 0.04f, 0.35f);
    beep(b, 0.1f, 1760.0f, 0.06f, 0.35f);
}

static void fx_hack_done(Buf *b)
{
    static const float k[] = { 72, 76, 79, 84 };
    for (int i = 0; i < 4; i++)
        beep(b, (float)i * 0.07f, midi(k[i]), 0.06f, 0.3f);
    ring(b, 0.3f, midi(96), 0.4f, 0.3f);
    ring(b, 0.3f, midi(91), 0.4f, 0.2f);
}

static void fx_hack_fail(Buf *b)
{
    N(.f0 = 600.0f, .f1 = 140.0f, .wave = W_SQUARE, .amp = 0.4f, .att = 0.005f, .sus = 0.9f, .hold = 0.5f, .rel = 0.05f,
      .lp = 3000.0f);
    N(.t = 0.5f, .f0 = 110.0f, .wave = W_SAW, .amp = 0.4f, .att = 0.005f, .sus = 1.0f, .hold = 0.3f, .rel = 0.05f,
      .detune = 0.04f, .lp = 1200.0f);
}

static void fx_pickup(Buf *b)
{
    tinkle(b, 0, 0.09f, 5, 0.25f);
    noise_burst(b, 0, 0.3f, 0.005f, 0.03f, 2500.0f, 1.0f);
}

static void fx_pickup_heavy(Buf *b)
{
    vowel(b, 0, 130.0f, 100.0f, 0.18f, 0.35f, 550.0f, 1000.0f);
    thump(b, 0.15f, 75.0f, 40.0f, 0.06f, 0.8f);
    N(.t = 0.15f, .wave = W_NOISE, .amp = 0.3f, .dec = 0.05f, .lp = 400.0f);
}

static void fx_cash(Buf *b)
{
    N(.wave = W_NOISE, .amp = 0.3f, .att = 0.02f, .sus = 0.6f, .hold = 0.15f, .rel = 0.05f, .bp = 1200.0f, .q = 1.5f);
    ring(b, 0.15f, 2600.0f, 0.25f, 0.5f);
    ring(b, 0.15f, 3900.0f, 0.18f, 0.3f);
    tinkle(b, 0.25f, 0.2f, 8, 0.2f);
}

static void fx_throw(Buf *b)
{
    N(.wave = W_NOISE, .amp = 0.5f, .att = 0.08f, .sus = 0.5f, .hold = 0.18f, .rel = 0.1f, .lp = 600.0f, .lp1 = 3000.0f);
}

static void fx_coin(Buf *b)
{
    ring(b, 0, 4200.0f, 0.05f, 0.4f);
    ring(b, 0, 6100.0f, 0.04f, 0.25f);
    ring(b, 0, 2900.0f, 0.06f, 0.3f);
    ring(b, 0.12f, 4250.0f, 0.03f, 0.2f);
    ring(b, 0.12f, 2950.0f, 0.04f, 0.15f);
    ring(b, 0.19f, 4300.0f, 0.02f, 0.1f);
}

static void fx_noisemaker(Buf *b) /* loop, 1 s */
{
    for (int i = 0; i < 8; i++)
        beep(b, (float)i * 0.125f, i & 1 ? 700.0f : 950.0f, 0.09f, 0.4f);
}

static void fx_smoke(Buf *b)
{
    thump(b, 0, 200.0f, 60.0f, 0.02f, 0.6f);
    N(.wave = W_NOISE, .amp = 0.5f, .att = 0.05f, .sus = 0.6f, .hold = 1.8f, .rel = 0.8f, .bp = 5000.0f, .q = 0.7f);
}

static void fx_emp(Buf *b)
{
    N(.f0 = 1800.0f, .f1 = 60.0f, .glide = 0.5f, .wave = W_SAW, .amp = 0.5f, .dec = 0.15f, .drive = 2.0f);
    noise_burst(b, 0, 0.5f, 0.002f, 0.1f, 3000.0f, 0.8f);
    thump(b, 0, 70.0f, 40.0f, 0.15f, 0.8f);
}

static void fx_taser(Buf *b)
{
    N(.f0 = 120.0f, .wave = W_SAW, .amp = 0.4f, .att = 0.005f, .sus = 0.9f, .hold = 0.6f, .rel = 0.05f, .drive = 4.0f,
      .trem = 0.6f, .trem_hz = 30.0f);
    N(.f0 = 240.0f, .wave = W_SQUARE, .amp = 0.2f, .att = 0.005f, .sus = 0.9f, .hold = 0.6f, .rel = 0.05f, .lp = 2500.0f);
    for (int i = 0; i < 12; i++)
        N(.t = 0.6f * nh_rng_float(&b->rng), .wave = W_NOISE, .amp = 0.4f, .dec = 0.004f, .hp = 4000.0f);
}

static void fx_dart(Buf *b)
{
    noise_burst(b, 0, 0.6f, 0.003f, 0.02f, 1200.0f, 1.0f);
    N(.f0 = 1500.0f, .f1 = 400.0f, .wave = W_SINE, .amp = 0.3f, .dec = 0.02f);
}

static void fx_jammer(Buf *b)
{
    N(.wave = W_NOISE, .amp = 0.4f, .att = 0.02f, .sus = 0.5f, .hold = 0.5f, .rel = 0.05f, .bp = 2500.0f, .q = 0.5f,
      .trem = 0.8f, .trem_hz = 17.0f);
    beep(b, 0.0f, 1000.0f, 0.03f, 0.2f);
    beep(b, 0.08f, 1500.0f, 0.03f, 0.2f);
}

static void fx_glass_cut(Buf *b)
{
    N(.f0 = 2800.0f, .f1 = 3300.0f, .wave = W_SAW, .amp = 0.4f, .att = 0.05f, .sus = 0.7f, .hold = 0.8f, .rel = 0.1f,
      .bp = 3000.0f, .q = 8.0f, .vib = 0.03f, .vib_hz = 7.0f);
    N(.wave = W_NOISE, .amp = 0.2f, .att = 0.05f, .sus = 0.6f, .hold = 0.8f, .rel = 0.1f, .hp = 5000.0f);
    click(b, 0.95f, 0.5f, 1500.0f);
}

static void fx_glass_break(Buf *b)
{
    N(.wave = W_NOISE, .amp = 0.6f, .dec = 0.07f, .hp = 3000.0f);
    for (int i = 0; i < 14; i++)
        ring(b, 0.25f * nh_rng_float(&b->rng), 3000.0f + 5000.0f * nh_rng_float(&b->rng),
             0.02f + 0.06f * nh_rng_float(&b->rng), 0.2f);
}

static void fx_drill(Buf *b) /* loop, 1 s */
{
    float f = loop_freq(b, 210.0f);
    N(.f0 = f, .wave = W_SAW, .amp = 0.4f, .sus = 1.0f, .hold = 1.0f, .drive = 3.0f, .lp = 2500.0f, .trem = 0.15f,
      .trem_hz = loop_freq(b, 6.0f));
    N(.f0 = f * 0.5f, .wave = W_SQUARE, .amp = 0.2f, .sus = 1.0f, .hold = 1.0f, .lp = 800.0f);
    N(.wave = W_NOISE, .amp = 0.15f, .sus = 1.0f, .hold = 1.0f, .bp = 4000.0f, .q = 1.0f);
}

static void fx_phone(Buf *b) /* loop, 2 s */
{
    N(.f0 = 1300.0f, .wave = W_SINE, .amp = 0.35f, .att = 0.01f, .sus = 1.0f, .hold = 0.8f, .rel = 0.02f, .trem = 1.0f,
      .trem_hz = 20.0f);
    N(.f0 = 1800.0f, .wave = W_SINE, .amp = 0.25f, .att = 0.01f, .sus = 1.0f, .hold = 0.8f, .rel = 0.02f, .trem = 1.0f,
      .trem_hz = 20.0f);
}

static void fx_thermal_on(Buf *b)
{
    click(b, 0, 0.4f, 3000.0f);
    N(.f0 = 400.0f, .f1 = 2400.0f, .glide = 0.5f, .wave = W_SINE, .amp = 0.3f, .att = 0.02f, .sus = 0.3f, .dec = 0.2f,
      .hold = 0.5f, .rel = 0.2f);
}

static void fx_thermal_off(Buf *b)
{
    N(.f0 = 1800.0f, .f1 = 300.0f, .glide = 0.3f, .wave = W_SINE, .amp = 0.3f, .dec = 0.1f);
}

static void fx_torch(Buf *b)
{
    click(b, 0, 0.6f, 3000.0f);
    ring(b, 0, 2200.0f, 0.004f, 0.3f);
    click(b, 0.04f, 0.3f, 3000.0f);
}

static void fx_power_down(Buf *b)
{
    thump(b, 0, 120.0f, 70.0f, 0.03f, 0.7f);
    click(b, 0, 0.5f, 2000.0f);
    N(.f0 = 120.0f, .f1 = 30.0f, .glide = 0.8f, .wave = W_SAW, .amp = 0.35f, .sus = 0.8f, .hold = 0.8f, .rel = 0.3f,
      .lp = 600.0f);
}

static void fx_power_up(Buf *b)
{
    thump(b, 0, 120.0f, 70.0f, 0.03f, 0.7f);
    N(.f0 = 30.0f, .f1 = 120.0f, .glide = 0.5f, .wave = W_SAW, .amp = 0.3f, .att = 0.05f, .sus = 0.6f, .hold = 0.7f,
      .rel = 0.2f, .lp = 600.0f);
    ring(b, 0.5f, 5000.0f, 0.02f, 0.2f);
    ring(b, 0.62f, 5200.0f, 0.02f, 0.15f);
}

static void fx_camera_beep(Buf *b)
{
    beep(b, 0, 2000.0f, 0.045f, 0.4f);
    beep(b, 0.1f, 2000.0f, 0.045f, 0.4f);
}

static void fx_laser_trip(Buf *b)
{
    N(.f0 = 3500.0f, .f1 = 1200.0f, .glide = 0.2f, .wave = W_SQUARE, .amp = 0.35f, .att = 0.002f, .sus = 0.8f, .hold = 0.25f,
      .rel = 0.05f, .lp = 4000.0f);
    beep(b, 0.3f, 1200.0f, 0.15f, 0.3f);
}

static void fx_alarm(Buf *b) /* loop, 2 s: hi-lo, whole cycles per half */
{
    for (int i = 0; i < 4; i++)
        N(.t = (float)i * 0.5f, .f0 = i & 1 ? 660.0f : 880.0f, .wave = W_SAW, .amp = 0.4f, .att = 0.01f, .sus = 1.0f,
          .hold = 0.48f, .rel = 0.02f, .lp = 3000.0f);
}

/* Police wail: one sinusoidal pitch sweep per loop around a mean pitch with
 * whole cycles per loop, so the phase closes at the loop point. */
static void fx_siren(Buf *b)
{
    float ph = 0.0f, lp = 0.0f, mean = loop_freq(b, 1075.0f), secs = (float)b->n / SR;
    /* Two passes so the filter has settled when the second one is kept. */
    for (int j = 0; j < 2 * b->n; j++) {
        int i = j % b->n;
        float t = (float)i / SR;
        float f = mean - 425.0f * cosf(TAU * t / secs);
        float x = tanhf(2.0f * (2.0f * ph - 1.0f));
        lp += 0.45f * (x - lp);
        if (j >= b->n)
            b->s[i] += lp * 0.4f;
        ph += f / SR;
        ph -= (float)(int)ph;
    }
}

static void fx_radio(Buf *b)
{
    N(.f0 = 2600.0f, .wave = W_SINE, .amp = 0.3f, .dec = 0.015f);
    N(.wave = W_NOISE, .amp = 0.25f, .sus = 0.8f, .hold = 0.12f, .rel = 0.03f, .bp = 2000.0f, .q = 1.5f);
    float t = 0.15f;
    while (t < 1.0f) {
        float len = 0.06f + 0.1f * nh_rng_float(&b->rng);
        float f = 140.0f + 60.0f * nh_rng_float(&b->rng);
        N(.t = t, .f0 = f, .f1 = f * (0.8f + 0.4f * nh_rng_float(&b->rng)), .wave = W_SAW, .amp = 0.4f, .att = 0.01f,
          .sus = 0.8f, .hold = len, .rel = 0.02f, .bp = 1200.0f + 800.0f * nh_rng_float(&b->rng), .q = 3.0f, .drive = 2.0f);
        t += len + 0.02f + 0.05f * nh_rng_float(&b->rng);
    }
    N(.t = t, .wave = W_NOISE, .amp = 0.3f, .sus = 0.8f, .hold = 0.15f, .rel = 0.03f, .bp = 2000.0f, .q = 1.5f);
}

static void fx_guard_huh(Buf *b) { vowel(b, 0, 110.0f, 165.0f, 0.35f, 0.5f, 500.0f, 1500.0f); }

static void fx_guard_alert(Buf *b)
{
    vowel(b, 0, 230.0f, 180.0f, 0.3f, 0.6f, 800.0f, 1800.0f);
    N(.t = 0.0f, .f0 = 230.0f, .f1 = 180.0f, .wave = W_SAW, .amp = 0.15f, .att = 0.02f, .sus = 0.8f, .hold = 0.3f,
      .rel = 0.08f, .bp = 2600.0f, .q = 5.0f, .drive = 2.0f);
}

static void fx_guard_mutter(Buf *b)
{
    float t = 0.0f;
    for (int i = 0; i < 4; i++) {
        float len = 0.1f + 0.12f * nh_rng_float(&b->rng);
        float f = 95.0f + 30.0f * nh_rng_float(&b->rng);
        vowel(b, t, f, f * 0.9f, len, 0.35f, 400.0f + 300.0f * nh_rng_float(&b->rng), 1100.0f + 600.0f * nh_rng_float(&b->rng));
        t += len + 0.08f;
    }
}

static void fx_dog_bark(Buf *b)
{
    for (int i = 0; i < 2; i++) {
        float t = (float)i * 0.28f;
        N(.t = t, .f0 = 380.0f, .f1 = 240.0f, .glide = 0.12f, .wave = W_SAW, .amp = 0.6f, .att = 0.008f, .dec = 0.05f,
          .drive = 2.0f, .bp = 900.0f, .q = 2.0f);
        noise_burst(b, t, 0.3f, 0.005f, 0.03f, 1500.0f, 1.0f);
    }
}

static void fx_takedown(Buf *b)
{
    thump(b, 0, 90.0f, 40.0f, 0.05f, 0.9f);
    N(.wave = W_NOISE, .amp = 0.4f, .dec = 0.04f, .lp = 1500.0f);
    vowel(b, 0.03f, 120.0f, 85.0f, 0.15f, 0.4f, 600.0f, 1100.0f);
}

static void fx_body_thud(Buf *b)
{
    thump(b, 0, 70.0f, 35.0f, 0.07f, 0.9f);
    N(.wave = W_NOISE, .amp = 0.4f, .dec = 0.05f, .lp = 500.0f);
}

static void fx_hide(Buf *b)
{
    N(.f0 = 300.0f, .f1 = 260.0f, .wave = W_SAW, .amp = 0.4f, .att = 0.03f, .sus = 0.6f, .hold = 0.25f, .rel = 0.05f,
      .bp = 1200.0f, .q = 8.0f, .vib = 0.05f, .vib_hz = 9.0f);
    click(b, 0.3f, 0.6f, 2500.0f);
    thump(b, 0.3f, 160.0f, 110.0f, 0.02f, 0.4f);
}

static void fx_heartbeat(Buf *b)
{
    N(.f0 = 55.0f, .f1 = 40.0f, .wave = W_SINE, .amp = 0.9f, .att = 0.005f, .dec = 0.03f, .lp = 300.0f);
    N(.t = 0.18f, .f0 = 50.0f, .f1 = 38.0f, .wave = W_SINE, .amp = 0.7f, .att = 0.005f, .dec = 0.03f, .lp = 300.0f);
}

static void fx_stinger(Buf *b)
{
    static const float ch[] = { 38, 50, 57, 62, 65, 69 };
    for (int i = 0; i < 6; i++)
        N(.f0 = midi(ch[i]), .wave = W_SAW, .amp = 0.18f, .att = 0.005f, .dec = 0.3f, .detune = 0.006f, .lp = 4000.0f,
          .lp1 = 500.0f);
    N(.wave = W_NOISE, .amp = 0.3f, .dec = 0.05f, .hp = 2000.0f);
    thump(b, 0, 75.0f, 50.0f, 0.25f, 0.7f);
}

static void fx_caught(Buf *b)
{
    static const float ch[] = { 26, 38, 45, 50 };
    for (int i = 0; i < 4; i++)
        N(.f0 = midi(ch[i]), .wave = W_SAW, .amp = 0.25f, .att = 0.02f, .sus = 0.7f, .hold = 1.2f, .rel = 0.6f,
          .detune = 0.005f, .lp = 700.0f, .lp1 = 300.0f);
    ring(b, 0, 180.0f, 0.6f, 0.3f);
    ring(b, 0, 243.0f, 0.5f, 0.2f);
    thump(b, 0, 60.0f, 40.0f, 0.3f, 0.8f);
}

static void fx_success(Buf *b)
{
    static const float k[] = { 62, 65, 69, 74 };
    for (int i = 0; i < 4; i++)
        N(.t = (float)i * 0.11f, .f0 = midi(k[i]), .wave = W_TRI, .amp = 0.4f, .att = 0.005f, .dec = 0.25f);
    static const float ch[] = { 50, 57, 62, 65, 69 };
    for (int i = 0; i < 5; i++)
        N(.t = 0.44f, .f0 = midi(ch[i]), .wave = W_SAW, .amp = 0.12f, .att = 0.02f, .sus = 0.6f, .hold = 0.8f, .rel = 0.6f,
          .detune = 0.005f, .lp = 2000.0f);
    ring(b, 0.44f, midi(86), 0.5f, 0.15f);
}

static void fx_fail(Buf *b)
{
    static const float k[] = { 69, 65, 62, 57 };
    for (int i = 0; i < 4; i++)
        N(.t = (float)i * 0.18f, .f0 = midi(k[i]), .wave = W_SAW, .amp = 0.3f, .att = 0.01f, .sus = 0.6f, .hold = 0.16f,
          .rel = 0.08f, .lp = 1500.0f);
    N(.t = 0.72f, .f0 = midi(38), .wave = W_SAW, .amp = 0.3f, .att = 0.02f, .sus = 0.7f, .hold = 0.9f, .rel = 0.5f,
      .detune = 0.006f, .lp = 600.0f);
}

static void fx_van(Buf *b)
{
    N(.wave = W_NOISE, .amp = 0.4f, .att = 0.05f, .sus = 0.7f, .hold = 0.35f, .rel = 0.05f, .bp = 900.0f, .q = 1.5f);
    thump(b, 0.42f, 90.0f, 45.0f, 0.06f, 0.9f);
    click(b, 0.42f, 0.5f, 1500.0f);
    N(.t = 0.6f, .f0 = 38.0f, .f1 = 75.0f, .glide = 0.6f, .wave = W_SAW, .amp = 0.4f, .att = 0.1f, .sus = 0.8f, .hold = 1.2f,
      .rel = 0.4f, .drive = 2.0f, .lp = 400.0f);
}

static void fx_ui_move(Buf *b) { ring(b, 0, 1400.0f, 0.012f, 0.4f); }

static void fx_ui_select(Buf *b)
{
    ring(b, 0, 1400.0f, 0.02f, 0.4f);
    ring(b, 0.045f, 2100.0f, 0.03f, 0.4f);
}

static void fx_ui_back(Buf *b) { N(.f0 = 1400.0f, .f1 = 900.0f, .glide = 0.08f, .wave = W_SINE, .amp = 0.4f, .dec = 0.025f); }

static void fx_ui_deny(Buf *b)
{
    N(.f0 = 220.0f, .wave = W_SQUARE, .amp = 0.3f, .att = 0.003f, .sus = 0.8f, .hold = 0.12f, .rel = 0.02f, .detune = 0.06f,
      .lp = 2000.0f);
}

/* ---- the effects table ------------------------------------------------------------ */

typedef struct SfxDef {
    const char *name;
    float seconds;
    bool loop;
    float peak; /* output level after normalising */
    void (*fn)(Buf *b);
} SfxDef;

static void fx_step0(Buf *b) { fx_step(b, 0); }
static void fx_step1(Buf *b) { fx_step(b, 1); }
static void fx_step2(Buf *b) { fx_step(b, 2); }
static void fx_step3(Buf *b) { fx_step(b, 3); }
static void fx_step4(Buf *b) { fx_step(b, 4); }
static void fx_step5(Buf *b) { fx_step(b, 5); }
static void fx_step6(Buf *b) { fx_step(b, 6); }
static void fx_step7(Buf *b) { fx_step(b, 7); }
static void fx_keypad_beep(Buf *b) { beep(b, 0, 1850.0f, 0.07f, 0.4f); }
static void fx_hack_beep(Buf *b) { beep(b, 0, 1200.0f, 0.035f, 0.3f); }

static const SfxDef k_sfx[SFX_COUNT] = {
    [SFX_STEP_CONCRETE] = { "step_concrete", 0.12f, false, 0.6f, fx_step0 },
    [SFX_STEP_CARPET] = { "step_carpet", 0.16f, false, 0.45f, fx_step1 },
    [SFX_STEP_TILE] = { "step_tile", 0.12f, false, 0.6f, fx_step2 },
    [SFX_STEP_WOOD] = { "step_wood", 0.18f, false, 0.6f, fx_step3 },
    [SFX_STEP_MARBLE] = { "step_marble", 0.15f, false, 0.6f, fx_step4 },
    [SFX_STEP_METAL] = { "step_metal", 0.6f, false, 0.6f, fx_step5 },
    [SFX_STEP_ASPHALT] = { "step_asphalt", 0.12f, false, 0.55f, fx_step6 },
    [SFX_STEP_GRASS] = { "step_grass", 0.22f, false, 0.45f, fx_step7 },
    [SFX_LAND] = { "land", 0.35f, false, 0.8f, fx_land },
    [SFX_DOOR_OPEN] = { "door_open", 0.7f, false, 0.7f, fx_door_open },
    [SFX_DOOR_CLOSE] = { "door_close", 0.35f, false, 0.8f, fx_door_close },
    [SFX_DOOR_LOCKED] = { "door_locked", 0.3f, false, 0.6f, fx_door_locked },
    [SFX_DOOR_VAULT] = { "door_vault", 2.6f, false, 0.9f, fx_door_vault },
    [SFX_PICK_TICK] = { "pick_tick", 0.05f, false, 0.35f, fx_pick_tick },
    [SFX_PICK_SET] = { "pick_set", 0.12f, false, 0.7f, fx_pick_set },
    [SFX_PICK_SLIP] = { "pick_slip", 0.3f, false, 0.7f, fx_pick_slip },
    [SFX_UNLOCK] = { "unlock", 0.3f, false, 0.8f, fx_unlock },
    [SFX_SAFE_CLICK] = { "safe_click", 0.04f, false, 0.35f, fx_safe_click },
    [SFX_SAFE_SET] = { "safe_set", 0.2f, false, 0.7f, fx_safe_set },
    [SFX_SAFE_OPEN] = { "safe_open", 1.0f, false, 0.8f, fx_safe_open },
    [SFX_KEYPAD_BEEP] = { "keypad_beep", 0.1f, false, 0.4f, fx_keypad_beep },
    [SFX_KEYPAD_OK] = { "keypad_ok", 0.25f, false, 0.45f, fx_keypad_ok },
    [SFX_KEYPAD_BAD] = { "keypad_bad", 0.35f, false, 0.45f, fx_keypad_bad },
    [SFX_SWIPE] = { "swipe", 0.3f, false, 0.5f, fx_swipe },
    [SFX_HACK_BEEP] = { "hack_beep", 0.06f, false, 0.3f, fx_hack_beep },
    [SFX_HACK_OK] = { "hack_ok", 0.2f, false, 0.4f, fx_hack_ok },
    [SFX_HACK_DONE] = { "hack_done", 1.2f, false, 0.5f, fx_hack_done },
    [SFX_HACK_FAIL] = { "hack_fail", 0.9f, false, 0.5f, fx_hack_fail },
    [SFX_PICKUP] = { "pickup", 0.4f, false, 0.5f, fx_pickup },
    [SFX_PICKUP_HEAVY] = { "pickup_heavy", 0.6f, false, 0.7f, fx_pickup_heavy },
    [SFX_CASH] = { "cash", 1.2f, false, 0.7f, fx_cash },
    [SFX_THROW] = { "throw", 0.3f, false, 0.4f, fx_throw },
    [SFX_COIN] = { "coin", 0.45f, false, 0.6f, fx_coin },
    [SFX_NOISEMAKER] = { "noisemaker", 1.0f, true, 0.6f, fx_noisemaker },
    [SFX_SMOKE] = { "smoke", 2.8f, false, 0.6f, fx_smoke },
    [SFX_EMP] = { "emp", 1.2f, false, 0.9f, fx_emp },
    [SFX_TASER] = { "taser", 0.7f, false, 0.7f, fx_taser },
    [SFX_DART] = { "dart", 0.2f, false, 0.5f, fx_dart },
    [SFX_JAMMER] = { "jammer", 0.6f, false, 0.5f, fx_jammer },
    [SFX_GLASS_CUT] = { "glass_cut", 1.1f, false, 0.5f, fx_glass_cut },
    [SFX_GLASS_BREAK] = { "glass_break", 0.8f, false, 0.8f, fx_glass_break },
    [SFX_DRILL] = { "drill", 1.0f, true, 0.6f, fx_drill },
    [SFX_PHONE] = { "phone", 2.0f, true, 0.6f, fx_phone },
    [SFX_THERMAL_ON] = { "thermal_on", 0.9f, false, 0.4f, fx_thermal_on },
    [SFX_THERMAL_OFF] = { "thermal_off", 0.5f, false, 0.35f, fx_thermal_off },
    [SFX_TORCH] = { "torch", 0.08f, false, 0.4f, fx_torch },
    [SFX_POWER_DOWN] = { "power_down", 1.3f, false, 0.8f, fx_power_down },
    [SFX_POWER_UP] = { "power_up", 1.0f, false, 0.7f, fx_power_up },
    [SFX_CAMERA_BEEP] = { "camera_beep", 0.2f, false, 0.4f, fx_camera_beep },
    [SFX_LASER_TRIP] = { "laser_trip", 0.5f, false, 0.6f, fx_laser_trip },
    [SFX_ALARM] = { "alarm", 2.0f, true, 0.6f, fx_alarm },
    [SFX_SIREN] = { "siren", 4.0f, true, 0.55f, fx_siren },
    [SFX_RADIO] = { "radio", 1.4f, false, 0.45f, fx_radio },
    [SFX_GUARD_HUH] = { "guard_huh", 0.5f, false, 0.6f, fx_guard_huh },
    [SFX_GUARD_ALERT] = { "guard_alert", 0.45f, false, 0.8f, fx_guard_alert },
    [SFX_GUARD_MUTTER] = { "guard_mutter", 1.3f, false, 0.4f, fx_guard_mutter },
    [SFX_DOG_BARK] = { "dog_bark", 0.6f, false, 0.8f, fx_dog_bark },
    [SFX_TAKEDOWN] = { "takedown", 0.45f, false, 0.8f, fx_takedown },
    [SFX_BODY_THUD] = { "body_thud", 0.5f, false, 0.8f, fx_body_thud },
    [SFX_HIDE] = { "hide", 0.45f, false, 0.5f, fx_hide },
    [SFX_HEARTBEAT] = { "heartbeat", 0.45f, false, 0.7f, fx_heartbeat },
    [SFX_STINGER] = { "stinger", 2.2f, false, 0.8f, fx_stinger },
    [SFX_CAUGHT] = { "caught", 2.5f, false, 0.8f, fx_caught },
    [SFX_SUCCESS] = { "success", 2.0f, false, 0.6f, fx_success },
    [SFX_FAIL] = { "fail", 2.0f, false, 0.6f, fx_fail },
    [SFX_VAN] = { "van", 2.3f, false, 0.7f, fx_van },
    [SFX_UI_MOVE] = { "ui_move", 0.08f, false, 0.3f, fx_ui_move },
    [SFX_UI_SELECT] = { "ui_select", 0.2f, false, 0.4f, fx_ui_select },
    [SFX_UI_BACK] = { "ui_back", 0.15f, false, 0.35f, fx_ui_back },
    [SFX_UI_DENY] = { "ui_deny", 0.2f, false, 0.35f, fx_ui_deny },
};

const char *nh_sfx_name(int sfx) { return sfx >= 0 && sfx < SFX_COUNT ? k_sfx[sfx].name : "?"; }

int nh_sfx_length(int sfx)
{
    if (sfx < 0 || sfx >= SFX_COUNT)
        return 0;
    return (int)(k_sfx[sfx].seconds * SR) & ~15; /* ASND wants 32-byte multiples */
}

bool nh_sfx_loops(int sfx) { return sfx >= 0 && sfx < SFX_COUNT && k_sfx[sfx].loop; }

/* Peak-normalise a float buffer into 16-bit samples; quiet starts and ends
 * get a few-sample fade so one-shots never click. */
static void finish(const float *s, int n, float peak, bool loop, int16_t *out)
{
    float m = 1e-6f;
    for (int i = 0; i < n; i++)
        m = NH_MAX(m, fabsf(s[i]));
    float g = peak / m;
    int fade = loop ? 0 : NH_MIN(32, n / 4);
    for (int i = 0; i < n; i++) {
        float v = s[i] * g;
        if (i >= n - fade)
            v *= (float)(n - 1 - i) / (float)fade;
        int q = (int)lrintf(v * 32767.0f);
        out[i] = (int16_t)NH_CLAMP(q, -32767, 32767);
    }
}

void nh_sfx_render(int sfx, int16_t *out, float *scratch)
{
    init_tables();
    const SfxDef *d = &k_sfx[sfx];
    Buf b = { scratch, nh_sfx_length(sfx), d->loop, { 0, 0 } };
    memset(scratch, 0, sizeof(float) * (size_t)b.n);
    nh_rng_seed(&b.rng, 0x5F0Bu + (uint64_t)sfx, 77);
    d->fn(&b);
    finish(scratch, b.n, d->peak, d->loop, out);
}

/* ---- music --------------------------------------------------------------------- */

/* Heist: D minor, 96 BPM, 4 bars of i - VI - iv - V. */
static const float k_heist_bpm = 96.0f;
static const float k_heist_chords[4][4] = { { 50, 53, 57, 62 }, { 50, 53, 58, 62 }, { 50, 55, 58, 62 }, { 49, 52, 57, 61 } };
static const float k_heist_roots[4] = { 38, 34, 31, 33 };

/* Safehouse: 84 BPM, Dm7 - G7 - Cmaj7 - A7, a slow lounge groove. */
static const float k_safe_bpm = 84.0f;
static const float k_safe_chords[4][4] = { { 53, 57, 60, 64 }, { 53, 55, 59, 62 }, { 52, 55, 59, 60 }, { 49, 52, 55, 57 } };
static const float k_safe_walk[4][4] = { { 38, 41, 45, 48 }, { 43, 47, 50, 41 }, { 36, 40, 43, 47 }, { 45, 49, 52, 43 } };

int nh_music_layers(int theme) { return theme == THEME_HEIST ? 4 : 2; }

static float music_beat(int theme) { return 60.0f / (theme == THEME_HEIST ? k_heist_bpm : k_safe_bpm); }

int nh_music_length(int theme) { return (int)(16.0f * music_beat(theme) * SR) & ~15; }

static void heist_ambient(Buf *b, float beat)
{
    float bar = beat * 4.0f;
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++)
            N(.t = (float)c * bar, .f0 = midi(k_heist_chords[c][k]), .wave = W_SAW, .amp = 0.1f, .att = 0.9f, .sus = 0.8f,
              .dec = 0.5f, .hold = bar, .rel = 1.2f, .detune = 0.004f, .lp = 700.0f);
    /* A drone on D that breathes over the whole loop. */
    N(.f0 = loop_freq(b, midi(26)), .wave = W_SINE, .amp = 0.3f, .sus = 1.0f, .hold = (float)b->n / SR, .trem = 0.4f,
      .trem_hz = loop_freq(b, 0.2f));
    N(.f0 = loop_freq(b, midi(38)), .wave = W_TRI, .amp = 0.08f, .sus = 1.0f, .hold = (float)b->n / SR);
    /* Sparse bell plucks from D minor pentatonic. */
    static const float pent[] = { 74, 77, 79, 81, 84, 86 };
    for (int i = 0; i < 7; i++) {
        float t = (float)(nh_rng_range(&b->rng, 32)) * beat * 0.5f;
        float f = midi(pent[nh_rng_range(&b->rng, 6)]);
        ring(b, t, f, 0.5f, 0.06f);
        ring(b, t, f * 2.76f, 0.15f, 0.015f);
    }
}

static void heist_pulse(Buf *b, float beat)
{
    float e = beat * 0.5f;
    for (int i = 0; i < 32; i++) {
        int c = i / 8;
        float m = k_heist_roots[c] + 12.0f + ((i % 8) == 6 ? 7.0f : 0.0f);
        N(.t = (float)i * e, .f0 = midi(m), .wave = W_SQUARE, .amp = (i % 2) ? 0.2f : 0.3f, .att = 0.004f, .dec = 0.07f,
          .lp = 420.0f);
        if (i % 2)
            hat(b, (float)i * e, 0.08f);
    }
    for (int i = 0; i < 4; i++)
        noise_burst(b, ((float)i * 4.0f + 3.0f) * beat, 0.12f, 0.001f, 0.01f, 3500.0f, 3.0f);
}

static void heist_tension(Buf *b, float beat)
{
    float s = beat * 0.25f;
    static const int up_down[8] = { 0, 1, 2, 3, 2, 1, 2, 3 };
    for (int i = 0; i < 64; i++) {
        int c = i / 16;
        N(.t = (float)i * s, .f0 = midi(k_heist_chords[c][up_down[i % 8]] + 12.0f), .wave = W_SAW, .amp = 0.09f,
          .att = 0.003f, .dec = 0.05f, .lp = 1800.0f);
    }
    for (int i = 0; i < 16; i += 2)
        kick(b, (float)i * beat, 0.5f);
    /* Toms into the loop point. */
    for (int i = 0; i < 4; i++)
        thump(b, (15.0f + (float)i * 0.25f) * beat, 180.0f - 25.0f * (float)i, 90.0f, 0.06f, 0.35f);
}

static void heist_action(Buf *b, float beat)
{
    float s = beat * 0.25f;
    for (int bar = 0; bar < 4; bar++) {
        float t0 = (float)bar * 4.0f * beat;
        kick(b, t0, 0.7f);
        kick(b, t0 + 1.5f * beat, 0.5f);
        kick(b, t0 + 2.0f * beat, 0.7f);
        kick(b, t0 + 2.75f * beat, 0.4f);
        snare(b, t0 + beat, 0.5f);
        snare(b, t0 + 3.0f * beat, 0.5f);
        for (int i = 0; i < 16; i++)
            hat(b, t0 + (float)i * s, i % 2 ? 0.05f : 0.1f);
        /* Driven bass on eighths, jumping the octave. */
        for (int i = 0; i < 8; i++) {
            float m = k_heist_roots[bar] + (i % 4 == 3 ? 24.0f : 12.0f);
            N(.t = t0 + (float)i * beat * 0.5f, .f0 = midi(m), .wave = W_SAW, .amp = 0.25f, .att = 0.003f, .dec = 0.09f,
              .drive = 3.0f, .lp = 900.0f);
        }
        /* Brass-ish stab on the downbeat. */
        for (int k = 0; k < 4; k++)
            N(.t = t0, .f0 = midi(k_heist_chords[bar][k] + 12.0f), .wave = W_SAW, .amp = 0.07f, .att = 0.01f, .dec = 0.15f,
              .detune = 0.006f, .lp = 2500.0f, .lp1 = 900.0f);
    }
}

/* An electric piano: sine with a touch of octave, a bell transient and tremolo. */
static void ep(Buf *b, float t, float m, float len, float amp)
{
    float f = midi(m);
    N(.t = t, .f0 = f, .wave = W_SINE, .amp = amp, .att = 0.004f, .sus = 0.4f, .dec = 0.4f, .hold = len, .rel = 0.3f,
      .trem = 0.25f, .trem_hz = 4.5f);
    N(.t = t, .f0 = f * 2.0f, .wave = W_SINE, .amp = amp * 0.3f, .att = 0.004f, .dec = 0.25f);
    ring(b, t, f * 14.0f, 0.02f, amp * 0.08f);
}

static void safe_keys(Buf *b, float beat)
{
    float bar = beat * 4.0f;
    for (int c = 0; c < 4; c++) {
        float t0 = (float)c * bar;
        for (int k = 0; k < 4; k++) {
            ep(b, t0, k_safe_chords[c][k], beat * 1.8f, 0.09f);
            ep(b, t0 + beat * 2.67f, k_safe_chords[c][k], beat * 0.6f, 0.06f);
        }
        for (int i = 0; i < 4; i++) {
            float m = k_safe_walk[c][i];
            N(.t = t0 + (float)i * beat, .f0 = midi(m), .wave = W_TRI, .amp = 0.35f, .att = 0.01f, .sus = 0.3f, .dec = 0.15f,
              .hold = beat * 0.9f, .rel = 0.08f, .lp = 600.0f);
        }
    }
    /* A little melody on top, once per loop. */
    static const float mel[] = { 76, 74, 72, 74, 0, 69, 72, 71 };
    for (int i = 0; i < 8; i++)
        if (mel[i] > 0.0f)
            ep(b, (8.0f + (float)i * 0.67f) * beat, mel[i], beat * 0.5f, 0.07f);
}

static void safe_brushes(Buf *b, float beat)
{
    for (int i = 0; i < 16; i++) {
        float t = (float)i * beat;
        if (i % 2 == 0)
            thump(b, t, 90.0f, 50.0f, 0.06f, 0.35f);
        else
            N(.t = t - 0.04f, .wave = W_NOISE, .amp = 0.12f, .att = 0.04f, .dec = 0.06f, .bp = 3500.0f, .q = 0.7f);
        /* Swung ride. */
        N(.t = t, .wave = W_NOISE, .amp = 0.06f, .dec = 0.05f, .hp = 6000.0f);
        N(.t = t + beat * 0.667f, .wave = W_NOISE, .amp = 0.04f, .dec = 0.03f, .hp = 6000.0f);
    }
    /* Record crackle. */
    for (int i = 0; i < 50; i++)
        click(b, (float)b->n / SR * nh_rng_float(&b->rng), 0.03f + 0.03f * nh_rng_float(&b->rng), 3000.0f);
}

void nh_music_render(int theme, int layer, int16_t *out, float *scratch)
{
    init_tables();
    Buf b = { scratch, nh_music_length(theme), true, { 0, 0 } };
    memset(scratch, 0, sizeof(float) * (size_t)b.n);
    nh_rng_seed(&b.rng, 0x30C0u + (uint64_t)theme * 16u + (uint64_t)layer, 5);
    float beat = music_beat(theme);
    if (theme == THEME_HEIST) {
        switch (layer) {
        case 0: heist_ambient(&b, beat); break;
        case 1: heist_pulse(&b, beat); break;
        case 2: heist_tension(&b, beat); break;
        default: heist_action(&b, beat); break;
        }
    } else if (layer == 0) {
        safe_keys(&b, beat);
    } else {
        safe_brushes(&b, beat);
    }
    finish(scratch, b.n, 0.55f, true, out);
}
