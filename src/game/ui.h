/* Night Heist - shared menu pieces: a vertical menu driven by the pointer
 * or the D-pad (with its clicks), and the controls card used by the title
 * screen and the pause menu. */
#ifndef NH_UI_H
#define NH_UI_H

#include "platform/platform.h"

typedef struct UiMenu {
    float x, y, w, row_h, scale;
    int n;
    const char *const *items;
    const bool *enabled; /* NULL: all enabled */
} UiMenu;

/* Moves *sel and returns the row activated with A, or -1. */
int ui_menu_tick(const UiMenu *m, int *sel, const NhInput *in);
void ui_menu_draw(const UiMenu *m, int sel);
/* The controls card, centred, starting at y. */
void ui_draw_controls(float y);
void ui_centered(float y, float scale, NhColor c, const char *s);

#endif
