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
    bool active;
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
                vc->active = false;
                break;
            }
            float s = (float)vc->data[idx] * (1.0f / 32768.0f);
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

int plat_audio_play(const NhSound *s, float volume, float pan)
{
    if (!s_dev)
        return -1;
    volume = NH_CLAMP(volume, 0.0f, 1.0f);
    pan = NH_CLAMP(pan, -1.0f, 1.0f);
    int voice = -1;
    SDL_LockAudioDevice(s_dev);
    for (int v = 0; v < NH_AUDIO_VOICES; v++) {
        if (!s_voices[v].active) {
            /* Same pan law and 8-bit volume steps as the ASND path. */
            float vl = (float)(int)(255.0f * volume * NH_MIN(1.0f, 1.0f - pan)) / 255.0f;
            float vr = (float)(int)(255.0f * volume * NH_MIN(1.0f, 1.0f + pan)) / 255.0f;
            s_voices[v] = (Voice){ s->samples, s->count, 0.0, (double)s->rate / s_rate, vl, vr, true };
            voice = v;
            break;
        }
    }
    SDL_UnlockAudioDevice(s_dev);
    return voice;
}

void plat_audio_stop(int voice)
{
    if (!s_dev || voice < 0 || voice >= NH_AUDIO_VOICES)
        return;
    SDL_LockAudioDevice(s_dev);
    s_voices[voice].active = false;
    SDL_UnlockAudioDevice(s_dev);
}
