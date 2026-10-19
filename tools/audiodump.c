/* Night Heist - writes every synthesised effect and music stem as a WAV
 * file, for listening outside the game: build/audiodump OUTDIR */
#include <stdio.h>
#include <stdlib.h>

#include "core/nh_synth.h"

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f), fputc(v >> 8 & 255, f), fputc(v >> 16 & 255, f), fputc(v >> 24, f); }
static void put16(FILE *f, uint32_t v) { fputc(v & 255, f), fputc(v >> 8 & 255, f); }

static void wav(const char *dir, const char *name, const int16_t *s, int n)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fwrite("RIFF", 1, 4, f), put32(f, 36 + (uint32_t)n * 2), fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16), put16(f, 1), put16(f, 1), put32(f, NH_SYNTH_RATE), put32(f, NH_SYNTH_RATE * 2), put16(f, 2), put16(f, 16);
    fwrite("data", 1, 4, f), put32(f, (uint32_t)n * 2);
    for (int i = 0; i < n; i++)
        put16(f, (uint16_t)s[i]);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int16_t *s = malloc(sizeof(int16_t) * NH_SYNTH_RATE * 16);
    float *scratch = malloc(sizeof(float) * NH_SYNTH_RATE * 16);
    for (int i = 0; i < SFX_COUNT; i++) {
        nh_sfx_render(i, s, scratch);
        wav(dir, nh_sfx_name(i), s, nh_sfx_length(i));
    }
    static const char *const themes[THEME_COUNT] = { "heist", "safehouse" };
    for (int t = 0; t < THEME_COUNT; t++)
        for (int l = 0; l < nh_music_layers(t); l++) {
            char name[64];
            snprintf(name, sizeof(name), "music_%s_%d", themes[t], l);
            nh_music_render(t, l, s, scratch);
            wav(dir, name, s, nh_music_length(t));
        }
    printf("wrote %d effects and the music stems to %s\n", SFX_COUNT, dir);
    return 0;
}
