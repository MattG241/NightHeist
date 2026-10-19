/* Night Heist - Wii entry point, lifecycle, timing and memory stats. */
#include <gccore.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>
#include <stdlib.h>
#include <wiiuse/wpad.h>

#include "game/app.h"
#include "platform/platform.h"
#include "wii_internal.h"

static volatile bool s_quit;
static volatile bool s_poweroff;

static void on_reset(u32 irq, void *ctx)
{
    (void)irq;
    (void)ctx;
    s_quit = true;
}

static void on_power(void)
{
    s_quit = true;
    s_poweroff = true;
}

static void on_remote_power(s32 chan)
{
    (void)chan;
    s_quit = true;
    s_poweroff = true;
}

bool plat_init(const NhPlatformArgs *args)
{
    if (!wii_gfx_init())
        return false;
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);
    wii_input_init();
    WPAD_SetPowerButtonCallback(on_remote_power);
    wii_file_init(args->argc > 0 ? args->argv[0] : NULL); /* missing SD is not fatal */
    wii_audio_init();
    return true;
}

void plat_shutdown(void)
{
    wii_audio_shutdown();
    wii_input_shutdown();
    wii_file_shutdown();
    wii_gfx_shutdown();
}

bool plat_should_quit(void)
{
    return s_quit;
}

void plat_request_quit(void)
{
    s_quit = true;
}

const char *plat_name(void)
{
    return "Wii";
}

uint64_t plat_time_us(void)
{
    return ticks_to_microsecs(gettime());
}

void plat_mem_stats(NhMemStats *out)
{
    struct mallinfo mi = mallinfo();
    out->heap_used = (uint32_t)mi.uordblks;
    out->mem1_free = (uint32_t)((u8 *)SYS_GetArena1Hi() - (u8 *)SYS_GetArena1Lo());
    out->mem2_free = (uint32_t)((u8 *)SYS_GetArena2Hi() - (u8 *)SYS_GetArena2Lo());
}

int main(int argc, char **argv)
{
    NhPlatformArgs args = { argc, argv };
    int rc = nh_app_run(&args);
    if (s_poweroff)
        SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    return rc; /* returning goes back to the Homebrew Channel */
}
