#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/nh_rng.h"
#include "platform/platform.h"

#define MAX_LOOPS 12

typedef struct Loop {
    int voice;
    float vol, pan;
} Loop;

static struct {
    bool ready;
    NhSound sfx[SFX_COUNT];
    NhSound music[THEME_COUNT][NH_MUSIC_LAYERS];
    float music_gain, sfx_gain;
    int theme;
    int stem_voice[NH_MUSIC_LAYERS];
    float stem_level[NH_MUSIC_LAYERS], stem_target[NH_MUSIC_LAYERS];
    Loop loops[MAX_LOOPS];
    NhVec3 lpos;
    float lyaw;
    int lfloor;
    const Level *L;
    NhRng rng;
} A = { .theme = -1 };

static int16_t *alloc_samples(int n)
{
    /* ASND reads by DMA: 32-byte aligned and padded. Never freed. */
    size_t bytes = ((size_t)n * sizeof(int16_t) + 31u) & ~(size_t)31u;
    uint8_t *raw = malloc(bytes + 32);
    if (!raw)
        return NULL;
    return (int16_t *)NH_ALIGN_UP((uintptr_t)raw, 32);
}

bool audio_init(void)
{
    int most = 0;
    for (int s = 0; s < SFX_COUNT; s++)
        most = NH_MAX(most, nh_sfx_length(s));
    for (int t = 0; t < THEME_COUNT; t++)
        most = NH_MAX(most, nh_music_length(t));
    float *scratch = malloc(sizeof(float) * (size_t)most);
    if (!scratch)
        return false;
    uint64_t t0 = plat_time_us();
    size_t bytes = 0;
    for (int s = 0; s < SFX_COUNT; s++) {
        int n = nh_sfx_length(s);
        int16_t *buf = alloc_samples(n);
        if (!buf)
            break;
        nh_sfx_render(s, buf, scratch);
        A.sfx[s] = (NhSound){ buf, (uint32_t)n, NH_SYNTH_RATE };
        bytes += (size_t)n * 2;
    }
    for (int t = 0; t < THEME_COUNT; t++)
        for (int l = 0; l < nh_music_layers(t); l++) {
            int n = nh_music_length(t);
            int16_t *buf = alloc_samples(n);
            if (!buf)
                break;
            nh_music_render(t, l, buf, scratch);
            A.music[t][l] = (NhSound){ buf, (uint32_t)n, NH_SYNTH_RATE };
            bytes += (size_t)n * 2;
        }
    free(scratch);
    printf("audio: %u KB synthesised in %u ms\n", (unsigned)(bytes / 1024),
           (unsigned)((plat_time_us() - t0) / 1000u));
    for (int i = 0; i < MAX_LOOPS; i++)
        A.loops[i].voice = -1;
    for (int l = 0; l < NH_MUSIC_LAYERS; l++)
        A.stem_voice[l] = -1;
    A.music_gain = 0.7f;
    A.sfx_gain = 0.8f;
    nh_rng_seed(&A.rng, 0xA0D10u, 3);
    A.ready = true;
    return true;
}

void audio_volumes(int music, int sfx)
{
    A.music_gain = (float)NH_CLAMP(music, 0, 10) / 10.0f;
    A.sfx_gain = (float)NH_CLAMP(sfx, 0, 10) / 10.0f;
    for (int i = 0; i < MAX_LOOPS; i++)
        if (A.loops[i].voice >= 0)
            plat_audio_set(A.loops[i].voice, A.loops[i].vol * A.sfx_gain, A.loops[i].pan);
}

void audio_play(int sfx, float vol, float pan, float pitch)
{
    if (!A.ready || sfx < 0 || sfx >= SFX_COUNT || vol * A.sfx_gain < 0.01f)
        return;
    NhSound s = A.sfx[sfx];
    s.rate = (uint32_t)((float)s.rate * NH_CLAMP(pitch, 0.5f, 2.0f));
    plat_audio_play(&s, vol * A.sfx_gain, pan);
}

void audio_listener(NhVec3 pos, float yaw, int floor, const Level *L)
{
    A.lpos = pos;
    A.lyaw = yaw;
    A.lfloor = floor;
    A.L = L;
}

/* Distance falloff, a wall or floor between muffles it, panned by bearing. */
static bool place(NhVec3 pos, float range, float *vol, float *pan)
{
    NhVec3 d = nh_v3_sub(pos, A.lpos);
    float dist = nh_v3_len(d);
    if (dist >= range)
        return false;
    float k = 1.0f - dist / range;
    *vol *= k * k;
    if (A.L) {
        int f = lv_floor_of(pos.y + 0.3f);
        if (f != A.lfloor)
            *vol *= 0.35f;
        else if (dist > 1.0f) {
            float t;
            NhVec3 from = nh_v3_add(A.lpos, nh_v3(0, -0.1f, 0)), to = nh_v3(pos.x, pos.y + 0.4f, pos.z);
            if (lv_raycast(A.L, from, to, &t))
                *vol *= 0.45f;
        }
    }
    float flat = sqrtf(d.x * d.x + d.z * d.z);
    if (flat > 0.3f) {
        /* Forward is (-sin yaw, -cos yaw); right is (cos yaw, -sin yaw). */
        float rx = cosf(A.lyaw), rz = -sinf(A.lyaw);
        *pan = 0.8f * (d.x * rx + d.z * rz) / flat;
    } else {
        *pan = 0.0f;
    }
    return *vol > 0.005f;
}

void audio_at(int sfx, NhVec3 pos, float vol, float range)
{
    float pan;
    if (!A.ready || !place(pos, range, &vol, &pan))
        return;
    audio_play(sfx, vol, pan, 0.94f + 0.12f * nh_rng_float(&A.rng));
}

int audio_loop(int sfx, float vol)
{
    if (!A.ready || sfx < 0 || sfx >= SFX_COUNT)
        return -1;
    for (int i = 0; i < MAX_LOOPS; i++) {
        Loop *lp = &A.loops[i];
        if (lp->voice >= 0)
            continue;
        lp->voice = plat_audio_loop(&A.sfx[sfx], vol * A.sfx_gain, 0.0f);
        if (lp->voice < 0)
            return -1;
        lp->vol = vol;
        lp->pan = 0.0f;
        return i;
    }
    return -1;
}

void audio_loop_set(int h, float vol, float pan)
{
    if (h < 0 || h >= MAX_LOOPS || A.loops[h].voice < 0)
        return;
    A.loops[h].vol = vol;
    A.loops[h].pan = pan;
    plat_audio_set(A.loops[h].voice, vol * A.sfx_gain, pan);
}

void audio_loop_at(int h, NhVec3 pos, float vol, float range)
{
    float pan = 0.0f;
    if (!place(pos, range, &vol, &pan))
        vol = 0.0f;
    audio_loop_set(h, vol, pan);
}

void audio_loop_stop(int *h)
{
    if (*h >= 0 && *h < MAX_LOOPS && A.loops[*h].voice >= 0) {
        plat_audio_stop(A.loops[*h].voice);
        A.loops[*h].voice = -1;
    }
    *h = -1;
}

void audio_music(int theme)
{
    if (!A.ready || theme == A.theme)
        return;
    for (int l = 0; l < NH_MUSIC_LAYERS; l++) {
        if (A.stem_voice[l] >= 0)
            plat_audio_stop(A.stem_voice[l]);
        A.stem_voice[l] = -1;
        A.stem_level[l] = A.stem_target[l] = 0.0f;
    }
    A.theme = theme;
    if (theme < 0 || theme >= THEME_COUNT)
        return;
    plat_audio_begin();
    for (int l = 0; l < nh_music_layers(theme); l++)
        A.stem_voice[l] = plat_audio_loop(&A.music[theme][l], 0.0f, 0.0f);
    plat_audio_end();
}

void audio_music_mix(const float level[NH_MUSIC_LAYERS])
{
    for (int l = 0; l < NH_MUSIC_LAYERS; l++)
        A.stem_target[l] = NH_CLAMP(level[l], 0.0f, 1.0f);
}

void audio_tick(float dt)
{
    if (!A.ready)
        return;
    for (int l = 0; l < NH_MUSIC_LAYERS; l++) {
        if (A.stem_voice[l] < 0)
            continue;
        /* Swell in over ~2 s, fall away over ~4 s. */
        float d = A.stem_target[l] - A.stem_level[l];
        float rate = d > 0.0f ? 0.5f : 0.25f;
        A.stem_level[l] += NH_CLAMP(d, -rate * dt, rate * dt);
        plat_audio_set(A.stem_voice[l], A.stem_level[l] * A.music_gain * 0.8f, 0.0f);
    }
}

void audio_stop_effects(void)
{
    for (int i = 0; i < MAX_LOOPS; i++) {
        int h = i;
        audio_loop_stop(&h);
    }
}
