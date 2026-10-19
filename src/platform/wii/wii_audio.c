/* Night Heist - Wii audio through ASND (16 hardware-mixed voices on the DSP). */
#include <asndlib.h>
#include <gccore.h>
#include <ogc/irq.h>

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

static u32 s_isr;

static void levels(float volume, float pan, s32 *vl, s32 *vr)
{
    volume = NH_CLAMP(volume, 0.0f, 1.0f);
    pan = NH_CLAMP(pan, -1.0f, 1.0f);
    *vl = (s32)(255.0f * volume * NH_MIN(1.0f, 1.0f - pan));
    *vr = (s32)(255.0f * volume * NH_MIN(1.0f, 1.0f + pan));
}

static int start(const NhSound *s, float volume, float pan, bool loop)
{
    if (!s || !s->count)
        return -1;
    s32 voice = ASND_GetFirstUnusedVoice();
    if (voice < 0)
        return -1;
    s32 vl, vr;
    levels(volume, pan, &vl, &vr);
    u32 bytes = s->count * sizeof(int16_t);
    /* ASND DMAs straight from RAM, so the CPU cache must be written back. */
    DCFlushRange((void *)s->samples, bytes);
    s32 r = loop ? ASND_SetInfiniteVoice(voice, VOICE_MONO_16BIT, (s32)s->rate, 0, (void *)s->samples, (s32)bytes, vl, vr)
                 : ASND_SetVoice(voice, VOICE_MONO_16BIT, (s32)s->rate, 0, (void *)s->samples, (s32)bytes, vl, vr, NULL);
    return r == SND_OK ? voice : -1;
}

int plat_audio_play(const NhSound *s, float volume, float pan) { return start(s, volume, pan, false); }

int plat_audio_loop(const NhSound *s, float volume, float pan) { return start(s, volume, pan, true); }

void plat_audio_set(int voice, float volume, float pan)
{
    if (voice < 0)
        return;
    s32 vl, vr;
    levels(volume, pan, &vl, &vr);
    ASND_ChangeVolumeVoice(voice, vl, vr);
}

void plat_audio_stop(int voice)
{
    if (voice >= 0)
        ASND_StopVoice(voice);
}

/* With interrupts off the DSP update can't run between the calls, so voices
 * started in the group begin on the same audio frame. */
void plat_audio_begin(void) { s_isr = IRQ_Disable(); }

void plat_audio_end(void) { IRQ_Restore(s_isr); }
