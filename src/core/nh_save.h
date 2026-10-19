/* Night Heist - save data: the campaign plus settings, written field by
 * field in big-endian order (portable between Wii and desktop), with a
 * magic, a version and a CRC32. The game keeps a main slot and a backup of
 * the previous good save. Older versions are migrated on load. */
#ifndef NH_SAVE_H
#define NH_SAVE_H

#include "nh_campaign.h"
#include "nh_player.h"

#define SAVE_VERSION 1
#define SAVE_MAX_BYTES 4096

typedef struct NhSaveSettings {
    NhControlSettings controls;
    uint8_t rumble, subtitles, music, sfx; /* volumes 0..10 */
    uint8_t difficulty;                    /* 0 easy, 1 normal, 2 hard */
    uint8_t playlog;                       /* opt-in playtest logging */
} NhSaveSettings;

void save_default_settings(NhSaveSettings *s);
uint32_t nh_crc32(const void *data, size_t len);
/* Returns the byte count written, 0 if it doesn't fit. */
size_t save_encode(const NhCampaign *c, const NhSaveSettings *s, uint8_t *buf, size_t cap);
/* why gets a reason on failure (bad magic, newer version, checksum...). */
bool save_decode(const uint8_t *buf, size_t len, NhCampaign *c, NhSaveSettings *s, char *why, int why_len);

#endif
