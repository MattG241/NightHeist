/* Night Heist - desktop backend internals. */
#ifndef NH_DESKTOP_INTERNAL_H
#define NH_DESKTOP_INTERNAL_H

#include <SDL.h>
#include <stdbool.h>

typedef struct DesktopOptions {
    int frames;              /* > 0: quit after this many frames (automated runs) */
    const char *screenshot;  /* save the final frame as BMP here */
    bool autotest;           /* drive input from a scripted sequence */
    bool wide;               /* emulate a 16:9 Wii (anamorphic) */
    bool vsync;
    const char *data_dir;
} DesktopOptions;

extern DesktopOptions g_desktop_opts;
extern SDL_Window *g_window;

bool desktop_gfx_init(void);
void desktop_gfx_shutdown(void);
/* Rectangle (window pixels) where the 640x480 image is shown. */
void desktop_gfx_viewport(int *x, int *y, int *w, int *h);
uint64_t desktop_frame_count(void);

bool desktop_audio_init(void);
void desktop_audio_shutdown(void);

#endif
