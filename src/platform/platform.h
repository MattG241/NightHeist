/* Night Heist - platform layer: input, timing, file I/O and audio.
 * Graphics lives in gfx.h. Game code talks to the machine only through these
 * two headers; each backend (wii/, desktop/) implements all of them. */
#ifndef NH_PLATFORM_H
#define NH_PLATFORM_H

#include "core/nh_types.h"

/* ---- lifecycle ---------------------------------------------------------- */

typedef struct NhPlatformArgs {
    int argc;
    char **argv;
} NhPlatformArgs;

bool plat_init(const NhPlatformArgs *args);
void plat_shutdown(void);
/* True once the user asked to leave (Home button, window close, power/reset). */
bool plat_should_quit(void);
void plat_request_quit(void);
const char *plat_name(void); /* "Wii" or "Desktop" */

/* ---- time --------------------------------------------------------------- */

uint64_t plat_time_us(void); /* monotonic */

/* ---- input -------------------------------------------------------------- */

/* Logical buttons, named after the Wii Remote + Nunchuk. */
enum {
    NH_BTN_A = 1u << 0,
    NH_BTN_B = 1u << 1,
    NH_BTN_1 = 1u << 2,
    NH_BTN_2 = 1u << 3,
    NH_BTN_PLUS = 1u << 4,
    NH_BTN_MINUS = 1u << 5,
    NH_BTN_HOME = 1u << 6,
    NH_BTN_UP = 1u << 7,
    NH_BTN_DOWN = 1u << 8,
    NH_BTN_LEFT = 1u << 9,
    NH_BTN_RIGHT = 1u << 10,
    NH_BTN_Z = 1u << 11,
    NH_BTN_C = 1u << 12,
};

typedef struct NhInput {
    bool connected;       /* Wii Remote present */
    bool ir_valid;        /* pointer is on screen */
    float ir_x, ir_y;     /* pointer in 640x480 virtual screen pixels */
    float remote_roll;    /* radians, + = clockwise twist (lockpicking) */

    bool nunchuk;         /* Nunchuk attached */
    int expansion;        /* raw backend expansion id, for diagnostics */
    float stick_x;        /* -1..1, + = right, dead zone applied */
    float stick_y;        /* -1..1, + = forward */
    float nunchuk_roll;   /* radians, + = tilted right (lean) */

    uint32_t held;        /* NH_BTN_* currently down */
    uint32_t pressed;     /* went down since the last poll */
    uint32_t released;    /* went up since the last poll */
} NhInput;

#define NH_MAX_PLAYERS 2
/* Player 0 is Wii Remote 1, player 1 is Wii Remote 2. Poll player 0 first
 * each frame (it reads the hardware for both). */
void plat_input_poll(int player, NhInput *out);
/* The Wii Remote motor is on/off only; callers build patterns over time. */
void plat_rumble(int player, bool on);

/* ---- files -------------------------------------------------------------- */

/* Directory holding the game's data and saves, without trailing slash:
 * "sd:/apps/NightHeist" on Wii, "apps/NightHeist" on desktop. */
const char *plat_data_dir(void);
bool plat_storage_ready(void);
/* Read up to cap bytes; returns bytes read or -1 on error. */
long plat_file_read(const char *path, void *buf, size_t cap);
bool plat_file_write(const char *path, const void *data, size_t len);

/* ---- audio -------------------------------------------------------------- */

/* Mono 16-bit PCM in native byte order. The buffer must be 32-byte aligned and
 * stay alive while playing (ASND reads it by DMA). */
typedef struct NhSound {
    const int16_t *samples;
    uint32_t count;
    uint32_t rate;
} NhSound;

#define NH_AUDIO_VOICES 16

/* volume 0..1, pan -1 (left) .. +1 (right). Returns voice id or -1. */
int plat_audio_play(const NhSound *s, float volume, float pan);
/* A looping voice; it plays until stopped. */
int plat_audio_loop(const NhSound *s, float volume, float pan);
void plat_audio_set(int voice, float volume, float pan);
void plat_audio_stop(int voice);
/* Calls between begin and end take effect together (music stems start in sync). */
void plat_audio_begin(void);
void plat_audio_end(void);

/* ---- debug -------------------------------------------------------------- */

/* Desktop-only override of the player's start (for screenshots/tests). */
bool plat_debug_start(float *x, float *y, float *z, float *yaw, float *pitch);
/* Testing aid: which job to generate (type -1 = the hand-built test level). */
bool plat_debug_job(int *type, int *size, unsigned *seed, int *security);
/* Testing aid: start with the debug overlay showing. */
bool plat_debug_overlay(void);
/* Testing aid: a debug job (--job / debug_start.txt) with two players. */
bool plat_debug_coop(void);

/* ---- memory stats (debug overlay) --------------------------------------- */

typedef struct NhMemStats {
    uint32_t heap_used;  /* bytes in use by malloc */
    uint32_t mem1_free;  /* Wii only, 0 elsewhere */
    uint32_t mem2_free;  /* Wii only, 0 elsewhere */
} NhMemStats;

void plat_mem_stats(NhMemStats *out);

#endif
