/* Night Heist - SD card access through libfat. */
#include <fat.h>
#include <stdio.h>
#include <string.h>

#include "platform/platform.h"
#include "wii_internal.h"

#define DEFAULT_DATA_DIR "sd:/apps/NightHeist"

static bool s_fat_ok;
static char s_data_dir[256] = DEFAULT_DATA_DIR;

bool wii_file_init(const char *argv0)
{
    s_fat_ok = fatInitDefault();
    /* The Homebrew Channel passes the full path of boot.dol as argv[0]
     * (e.g. "usb:/apps/NightHeist/boot.dol"); use its folder so the game
     * also works when launched from USB. */
    if (argv0 && (strncmp(argv0, "sd:/", 4) == 0 || strncmp(argv0, "usb:/", 5) == 0)) {
        const char *slash = strrchr(argv0, '/');
        size_t len = (size_t)(slash - argv0);
        if (len > 0 && len < sizeof(s_data_dir)) {
            memcpy(s_data_dir, argv0, len);
            s_data_dir[len] = '\0';
        }
    }
    return s_fat_ok;
}

void wii_file_shutdown(void)
{
    if (s_fat_ok) {
        fatUnmount("sd:");
        fatUnmount("usb:");
    }
    s_fat_ok = false;
}

const char *plat_data_dir(void)
{
    return s_data_dir;
}

bool plat_storage_ready(void)
{
    return s_fat_ok;
}

long plat_file_read(const char *path, void *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t n = fread(buf, 1, cap, f);
    bool err = ferror(f) != 0;
    fclose(f);
    return err ? -1 : (long)n;
}

bool plat_file_write(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    return ok;
}
