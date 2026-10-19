/* Night Heist - Wii backend internals shared between wii_*.c files. */
#ifndef NH_WII_INTERNAL_H
#define NH_WII_INTERNAL_H

#include <stdbool.h>

bool wii_gfx_init(void);
void wii_gfx_shutdown(void);

void wii_input_init(void);
void wii_input_shutdown(void);

bool wii_file_init(const char *argv0);
void wii_file_shutdown(void);

void wii_audio_init(void);
void wii_audio_shutdown(void);

#endif
