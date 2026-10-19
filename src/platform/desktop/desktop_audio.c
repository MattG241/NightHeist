/* Night Heist - desktop audio: a tiny software mixer that behaves like ASND
 * (16 voices, mono 16-bit sources at any rate, per-voice L/R volume). */
#include <string.h>

#include "core/nh_types.h"
#include "desktop_internal.h"
#include "platform/platform.h"

typedef struct Voice {
    const int16_t *data;
    uint32_t count;
    double pos, step;
    float vl, vr;
    bool active, loop;
} Voice;

static SDL_AudioDeviceID s_dev;
static int s_rate;
static Voice s_voices[NH_AUDIO_VOICES];

static void mix(void *user, Uint8 *stream, int len)
{
    (void)user;
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    memset(stream, 0, (size_t)len);
    for (int v = 0; v < NH_AUDIO_VOICES; v++) {
        Voice *vc = &s_voices[v];
        if (!vc->active)
            continue;
        for (int i = 0; i < frames; i++) {
            uint32_t idx = (uint32_t)vc->pos;
            if (idx >= vc->count) {
                if (!vc->loop) {
                    vc->active = false;
                    break;
                }
                vc->pos -= (double)vc->count;
                idx = (uint32_t)vc->pos;
            }
            /* Linear interpolation, like the DSP's resampler. */
            uint32_t nxt = idx + 1 < vc->count ? idx + 1 : (vc->loop ? 0 : idx);
            float fr = (float)(vc->pos - (double)idx);
            float s = ((float)vc->data[idx] + ((float)vc->data[nxt] - (float)vc->data[idx]) * fr) * (1.0f / 32768.0f);
            out[i * 2] += s * vc->vl;
            out[i * 2 + 1] += s * vc->vr;
            vc->pos += vc->step;
        }
    }
}

bool desktop_audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return false;
    SDL_AudioSpec want = { 0 }, have;
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = mix;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!s_dev)
        return false;
    s_rate = have.freq;
    SDL_PauseAudioDevice(s_dev, 0);
    return true;
}

void desktop_audio_shutdown(void)
{
    if (s_dev)
        SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
}

/* Same pan law and 8-bit volume steps as the ASND path. */
static void levels(float volume, float pan, float *vl, float *vr)
{
    volume = NH_CLAMP(volume, 0.0f, 1.0f);
    pan = NH_CLAMP(pan, -1.0f, 1.0f);
    *vl = (float)(int)(255.0f * volume * NH_MIN(1.0f, 1.0f - pan)) / 255.0f;
    *vr = (float)(int)(255.0f * volume * NH_MIN(1.0f, 1.0f + pan)) / 255.0f;
}

static int start(const NhSound *s, float volume, float pan, bool loop)
{
    if (!s_dev || !s || !s->count)
        return -1;
    int voice = -1;
    SDL_LockAudioDevice(s_dev);
    for (int v = 0; v < NH_AUDIO_VOICES; v++) {
        if (!s_voices[v].active) {
            Voice *vc = &s_voices[v];
            *vc = (Voice){ s->samples, s->count, 0.0, (double)s->rate / s_rate, 0, 0, true, loop };
            levels(volume, pan, &vc->vl, &vc->vr);
            voice = v;
            break;
        }
    }
    SDL_UnlockAudioDevice(s_dev);
    return voice;
}

int plat_audio_play(const NhSound *s, float volume, float pan) { return start(s, volume, pan, false); }

int plat_audio_loop(const NhSound *s, float volume, float pan) { return start(s, volume, pan, true); }

void plat_audio_set(int voice, float volume, float pan)
{
    if (!s_dev || voice < 0 || voice >= NH_AUDIO_VOICES)
        return;
    SDL_LockAudioDevice(s_dev);
    levels(volume, pan, &s_voices[voice].vl, &s_voices[voice].vr);
    SDL_UnlockAudioDevice(s_dev);
}

/* SDL's audio lock is recursive, so a group of calls can hold it throughout. */
void plat_audio_begin(void)
{
    if (s_dev)
        SDL_LockAudioDevice(s_dev);
}

void plat_audio_end(void)
{
    if (s_dev)
        SDL_UnlockAudioDevice(s_dev);
}

void plat_audio_stop(int voice)
{
    if (!s_dev || voice < 0 || voice >= NH_AUDIO_VOICES)
        return;
    SDL_LockAudioDevice(s_dev);
    s_voices[voice].active = false;
    SDL_UnlockAudioDevice(s_dev);
}
