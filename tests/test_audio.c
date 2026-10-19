/* Tests for the synthesiser: every effect and music stem renders, is
 * deterministic, sits at its level, and loops without a click. */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/nh_synth.h"
#include "nh_test.h"

typedef struct Stats {
    int peak;
    double rms, mean;
    int max_jump, seam;
} Stats;

static Stats measure(const int16_t *s, int n)
{
    Stats st = { 0 };
    double sum = 0.0, sq = 0.0;
    for (int i = 0; i < n; i++) {
        int v = s[i];
        st.peak = NH_MAX(st.peak, abs(v));
        sum += v;
        sq += (double)v * v;
        if (i > 0)
            st.max_jump = NH_MAX(st.max_jump, abs(v - s[i - 1]));
    }
    st.mean = sum / n;
    st.rms = sqrt(sq / n);
    st.seam = abs(s[0] - s[n - 1]);
    return st;
}

void test_synth(void)
{
    int16_t *a = malloc(sizeof(int16_t) * NH_SYNTH_RATE * 16);
    int16_t *b = malloc(sizeof(int16_t) * NH_SYNTH_RATE * 16);
    float *scratch = malloc(sizeof(float) * NH_SYNTH_RATE * 16);
    clock_t t0 = clock();
    long total = 0;
    for (int s = 0; s < SFX_COUNT; s++) {
        int n = nh_sfx_length(s);
        CHECK(n > 0 && n <= NH_SYNTH_RATE * 5);
        nh_sfx_render(s, a, scratch);
        total += n;
        Stats st = measure(a, n);
        if (st.peak < 32767 * 3 / 10 || st.rms < 200.0) {
            fprintf(stderr, "  %s: peak %d rms %.0f\n", nh_sfx_name(s), st.peak, st.rms);
            CHECK(false);
        }
        if (nh_sfx_loops(s)) {
            if (st.seam > st.max_jump)
                fprintf(stderr, "  %s: seam %d, largest step inside %d\n", nh_sfx_name(s), st.seam, st.max_jump);
            CHECK(st.seam <= st.max_jump);
            CHECK(fabs(st.mean) < 1500.0);
        } else {
            CHECK(abs(a[n - 1]) < 64); /* faded out: no click when it stops */
        }
    }
    double sfx_s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    /* Same recipe, same samples. */
    for (int s = 0; s < SFX_COUNT; s += 7) {
        int n = nh_sfx_length(s);
        nh_sfx_render(s, a, scratch);
        nh_sfx_render(s, b, scratch);
        CHECK(memcmp(a, b, sizeof(int16_t) * (size_t)n) == 0);
    }
    t0 = clock();
    long music = 0;
    for (int th = 0; th < THEME_COUNT; th++) {
        int n = nh_music_length(th);
        CHECK(n > NH_SYNTH_RATE * 8 && n < NH_SYNTH_RATE * 16);
        CHECK(nh_music_layers(th) >= 2 && nh_music_layers(th) <= NH_MUSIC_LAYERS);
        for (int l = 0; l < nh_music_layers(th); l++) {
            nh_music_render(th, l, a, scratch);
            music += n;
            Stats st = measure(a, n);
            CHECK(st.peak > 32767 / 2 && st.peak < 32767 * 6 / 10);
            CHECK(st.rms > 800.0);
            CHECK(st.seam <= st.max_jump);
            CHECK(fabs(st.mean) < 1000.0);
            if (th == 0 && l == 0) {
                nh_music_render(th, l, b, scratch);
                CHECK(memcmp(a, b, sizeof(int16_t) * (size_t)n) == 0);
            }
        }
    }
    double mus_s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("  %d effects (%.0f KB) in %.2f s, music %.0f KB in %.2f s\n", SFX_COUNT, total * 2 / 1024.0, sfx_s,
           music * 2 / 1024.0, mus_s);
    free(a);
    free(b);
    free(scratch);
}
