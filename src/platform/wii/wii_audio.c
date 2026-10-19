/* Night Heist - Wii audio through ASND (16 hardware-mixed voices on the DSP). */
#include <asndlib.h>
#include <gccore.h>

#include "core/nh_types.h"
#include "platform/platform.h"
#include "wii_internal.h"

void wii_audio_init(void)
{
    ASND_Init();
    ASND_Pause(0);
}

void wii_audio_shutdown(void)
{
    ASND_Pause(1);
    ASND_End();
}

int plat_audio_play(const NhSound *s, float volume, float pan)
{
    s32 voice = ASND_GetFirstUnusedVoice();
    if (voice < 0)
        return -1;
    volume = NH_CLAMP(volume, 0.0f, 1.0f);
    pan = NH_CLAMP(pan, -1.0f, 1.0f);
    s32 vl = (s32)(255.0f * volume * NH_MIN(1.0f, 1.0f - pan));
    s32 vr = (s32)(255.0f * volume * NH_MIN(1.0f, 1.0f + pan));
    u32 bytes = s->count * sizeof(int16_t);
    /* ASND DMAs straight from RAM, so the CPU cache must be written back. */
    DCFlushRange((void *)s->samples, bytes);
    if (ASND_SetVoice(voice, VOICE_MONO_16BIT, (s32)s->rate, 0, (void *)s->samples, (s32)bytes, vl, vr, NULL) !=
        SND_OK)
        return -1;
    return voice;
}

void plat_audio_stop(int voice)
{
    if (voice >= 0)
        ASND_StopVoice(voice);
}
