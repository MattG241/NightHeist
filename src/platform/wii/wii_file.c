/* Night Heist - SD card and USB storage through libfat.
 *
 * The game lives wherever it was launched from: the Homebrew Channel passes
 * the full path of boot.dol as argv[0] ("sd:/apps/NightHeist/boot.dol" or
 * "usb:/apps/NightHeist/boot.dol"). Both devices are mounted; a USB drive
 * that is still spinning up gets a few seconds to appear. Without a usable
 * argv[0] (some loaders pass none) the game looks for its data folder on
 * the SD card, then on USB. Drives must be FAT32 (libfat). */
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/platform.h"
#include "wii_internal.h"

#define APP_DIR "/apps/NightHeist"

static bool s_sd, s_usb;
static char s_data_dir[256] = "sd:" APP_DIR;

static bool dir_has_data(const char *dir)
{
    char path[300];
    struct stat st;
    snprintf(path, sizeof(path), "%s/data", dir);
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool mount_usb(int wait_ms)
{
    uint64_t start = gettime();
    do {
        if (__io_usbstorage.startup() && __io_usbstorage.isInserted() && fatMountSimple("usb", &__io_usbstorage))
            return true;
        usleep(100 * 1000);
    } while (ticks_to_millisecs(diff_ticks(start, gettime())) < (u64)wait_ms);
    return false;
}

bool wii_file_init(const char *argv0)
{
    s_sd = fatMountSimple("sd", &__io_wiisd);
    /* "dev:/path/boot.dol" -> "dev:/path"; any device name the loader uses. */
    const char *colon = argv0 ? strstr(argv0, ":/") : NULL;
    const char *slash = argv0 ? strrchr(argv0, '/') : NULL;
    bool from_usb = colon && !strncmp(argv0, "usb", 3);
    /* Launched from USB: give a slow drive up to 5 s to spin up. Otherwise a
     * quick look, so a USB copy can still be found without argv[0]. */
    s_usb = mount_usb(from_usb ? 5000 : (s_sd ? 300 : 3000));
    if (colon && slash && slash > colon) {
        size_t len = (size_t)(slash - argv0);
        if (len < sizeof(s_data_dir)) {
            memcpy(s_data_dir, argv0, len);
            s_data_dir[len] = '\0';
            if (dir_has_data(s_data_dir))
                return true;
        }
    }
    static const char *const candidates[] = { "sd:" APP_DIR, "usb:" APP_DIR };
    for (int i = 0; i < 2; i++)
        if (dir_has_data(candidates[i])) {
            snprintf(s_data_dir, sizeof(s_data_dir), "%s", candidates[i]);
            return true;
        }
    snprintf(s_data_dir, sizeof(s_data_dir), "%s", s_sd || !s_usb ? candidates[0] : candidates[1]);
    return s_sd || s_usb;
}

void wii_file_shutdown(void)
{
    /* Unmounting flushes libfat's cache to the card or drive. */
    if (s_sd)
        fatUnmount("sd:");
    if (s_usb)
        fatUnmount("usb:");
    s_sd = s_usb = false;
}

const char *plat_data_dir(void)
{
    return s_data_dir;
}

bool plat_storage_ready(void)
{
    if (!strncmp(s_data_dir, "usb", 3))
        return s_usb;
    if (!strncmp(s_data_dir, "sd", 2))
        return s_sd;
    return s_sd || s_usb;
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
    ok = fflush(f) == 0 && ok;
    /* Push it through libfat's cache to the device now, so a save survives
     * the console being switched off at the wall. */
    ok = fsync(fileno(f)) == 0 && ok;
    ok = (fclose(f) == 0) && ok;
    return ok;
}
