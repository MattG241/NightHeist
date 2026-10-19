/* Night Heist - application entry and main loop (portable).
 * Fixed 60 Hz simulation, rendering decoupled and interpolated. */
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "core/nh_arena.h"
#include "core/nh_fixedstep.h"
#include "core/nh_save.h"
#include "heist.h"
#include "hideout.h"
#include "platform/gfx.h"
#include "res.h"
#include "text.h"
#include "title.h"

#define PERSIST_SIZE (4u << 20)
#define LEVEL_SIZE (12u << 20)
#define SCRATCH_SIZE (10u << 20)

static NhArena s_persist, s_level, s_scratch;

void app_arena_stats(size_t out[6])
{
    out[0] = s_persist.peak;
    out[1] = s_persist.cap;
    out[2] = s_level.peak;
    out[3] = s_level.cap;
    out[4] = s_scratch.peak;
    out[5] = s_scratch.cap;
}
static Heist s_heist;
static GameSettings s_settings;
static NhCampaign s_camp;
static NhSaveSettings s_save_settings;
static Hideout s_hideout;
static NhJobOffer s_offer;
static Title s_title;
static enum { MODE_TITLE, MODE_HIDEOUT, MODE_HEIST } s_mode;

static void path_of(char *out, size_t n, const char *file)
{
    snprintf(out, n, "%s/%s", plat_data_dir(), file);
}

/* Main slot, falling back to the backup of the last good save. Returns
 * whether a career was loaded. */
static bool load_game(void)
{
    static uint8_t buf[SAVE_MAX_BYTES];
    const char *slots[2] = { "save.dat", "save.bak" };
    for (int k = 0; k < 2; k++) {
        char path[192], why[64];
        path_of(path, sizeof(path), slots[k]);
        long n = plat_file_read(path, buf, sizeof(buf));
        if (n > 0 && save_decode(buf, (size_t)n, &s_camp, &s_save_settings, why, sizeof(why))) {
            printf("loaded %s\n", slots[k]);
            hideout_init(&s_hideout);
            return true;
        }
        if (n > 0)
            fprintf(stderr, "%s: %s\n", slots[k], why);
    }
    camp_new(&s_camp, (uint32_t)(plat_time_us() & 0xFFFFFFFFu) | 1u);
    save_default_settings(&s_save_settings);
    hideout_init(&s_hideout);
    return false;
}

/* Keep the previous good save as the backup, then write the new one. */
static void save_game(void)
{
    if (!plat_storage_ready())
        return;
    static uint8_t buf[SAVE_MAX_BYTES], old[SAVE_MAX_BYTES];
    char path[192], bak[192];
    path_of(path, sizeof(path), "save.dat");
    path_of(bak, sizeof(bak), "save.bak");
    long n = plat_file_read(path, old, sizeof(old));
    NhCampaign tmpc;
    NhSaveSettings tmps;
    if (n > 0 && save_decode(old, (size_t)n, &tmpc, &tmps, NULL, 0))
        plat_file_write(bak, old, (size_t)n);
    size_t len = save_encode(&s_camp, &s_save_settings, buf, sizeof(buf));
    if (len)
        plat_file_write(path, buf, len);
}

static void apply_settings(void)
{
    s_settings.controls = s_save_settings.controls;
    s_settings.rumble = s_save_settings.rumble;
    s_settings.subtitles = s_save_settings.subtitles;
    s_settings.music_volume = (float)s_save_settings.music / 10.0f;
    s_settings.sfx_volume = (float)s_save_settings.sfx / 10.0f;
    audio_volumes(s_save_settings.music, s_save_settings.sfx);
    world_set_brightness(s_save_settings.brightness);
}

/* Opt-in playtest log: one CSV line per job on the SD card. */
static void playlog(const Heist *h, const NhPayday *pay)
{
    if (!s_save_settings.playlog)
        return;
    static char buf[32768];
    char path[192];
    path_of(path, sizeof(path), "playlog.csv");
    long n = plat_file_read(path, buf, sizeof(buf) - 256);
    if (n < 0)
        n = 0;
    if (n == 0)
        n = snprintf(buf, sizeof(buf), "job,district,type,size,security,mods,outcome,grade,seconds,spotted,alarms,"
                                       "takedowns,payout,net,money,reputation\n");
    const NhJob *j = &h->state;
    n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%u,%d,%d,%d,%d,%u,%d,%c,%d,%d,%d,%d,%d,%d,%d,%d\n",
                  (unsigned)s_camp.jobs_done + s_camp.jobs_failed, s_offer.district, s_offer.type, s_offer.size,
                  s_offer.security, (unsigned)s_offer.mods, h->result.outcome, h->result.grade, (int)j->time,
                  j->spotted + j->camera_spots, j->alarms, j->takedowns, h->result.payout * 10, pay->net * 10,
                  s_camp.money * 10, s_camp.reputation);
    plat_file_write(path, buf, (size_t)n);
}

static bool make_arena(NhArena *a, size_t size)
{
    uint8_t *raw = malloc(size + 32);
    if (!raw)
        return false;
    uint8_t *aligned = (uint8_t *)NH_ALIGN_UP((uintptr_t)raw, 32);
    nh_arena_init(a, aligned, size);
    return true;
}

static void loading_screen(const char *msg)
{
    gfx_begin_frame(nh_rgba(0, 0, 0, 255));
    text_draw(320 - text_width("NIGHT HEIST", 4) * 0.5f, 200, 4, nh_rgba(255, 210, 120, 255), "NIGHT HEIST");
    text_draw(320 - text_width(msg, 2) * 0.5f, 260, 2, nh_rgba(170, 170, 180, 255), msg);
    gfx_end_frame();
}

int nh_app_run(const NhPlatformArgs *args)
{
    if (!plat_init(args))
        return 1;
    if (!make_arena(&s_persist, PERSIST_SIZE) || !make_arena(&s_level, LEVEL_SIZE) ||
        !make_arena(&s_scratch, SCRATCH_SIZE)) {
        plat_shutdown();
        return 2;
    }
    res_init(&s_persist, &s_scratch);
    if (!text_init(&s_scratch)) {
        plat_shutdown();
        return 3;
    }
    loading_screen("tuning up...");
    if (!audio_init())
        fprintf(stderr, "audio: out of memory, continuing silently\n");
    title_init(&s_title, load_game());
    apply_settings();
    s_settings.show_debug = plat_debug_overlay();
    static const float k_safe_mix[NH_MUSIC_LAYERS] = { 1.0f, 0.8f, 0.0f, 0.0f };
    static const float k_title_mix[NH_MUSIC_LAYERS] = { 0.9f, 0.0f, 0.0f, 0.0f };
    audio_music(THEME_SAFEHOUSE);
    audio_music_mix(k_title_mix);
    s_mode = MODE_TITLE;

    /* Debug jobs (--job / debug_start.txt) skip the safehouse. */
    int jt, js, jsec;
    unsigned jseed;
    if (plat_debug_job(&jt, &js, &jseed, &jsec)) {
        NhGenParams job;
        memset(&job, 0, sizeof(job));
        bool use_test = jt < 0;
        job.type = (uint8_t)NH_CLAMP(jt, 0, BT_COUNT - 1);
        job.size = (uint8_t)NH_CLAMP(js, 0, BS_COUNT - 1);
        job.seed = jseed;
        job.security = (uint8_t)NH_CLAMP(jsec, 0, 4);
        loading_screen(use_test ? "building the test building..." : "casing the job...");
        if (!heist_start(&s_heist, use_test ? NULL : &job, NULL, NULL, s_save_settings.difficulty,
                         plat_debug_coop() ? 2 : 1, &s_level, &s_scratch)) {
            fprintf(stderr, "failed to build the level\n");
            plat_shutdown();
            return 4;
        }
        s_mode = MODE_HEIST;
        audio_music(THEME_HEIST);
    }

    NhFixedStep step;
    nh_fixedstep_init(&step);
    uint64_t last = plat_time_us();
    uint64_t fps_start = last;
    uint32_t fps_frames = 0;
    float fps = 0.0f;
    uint32_t pending_pressed[NH_MAX_PLAYERS] = { 0 }, pending_released[NH_MAX_PLAYERS] = { 0 };

    while (!plat_should_quit()) {
        uint64_t now = plat_time_us();
        int ticks = nh_fixedstep_advance(&step, now - last);
        last = now;

        NhInput ins[NH_MAX_PLAYERS];
        for (int p = 0; p < NH_MAX_PLAYERS; p++)
            plat_input_poll(p, &ins[p]);
        s_hideout.p2_connected = ins[1].connected && ins[1].nunchuk;

        /* Edges must reach exactly one tick even on frames that run zero ticks. */
        for (int p = 0; p < NH_MAX_PLAYERS; p++) {
            pending_pressed[p] |= ins[p].pressed;
            pending_released[p] |= ins[p].released;
        }
        int start_job = -1;
        for (int i = 0; i < ticks; i++) {
            NhInput tick_ins[NH_MAX_PLAYERS];
            for (int p = 0; p < NH_MAX_PLAYERS; p++) {
                tick_ins[p] = ins[p];
                tick_ins[p].pressed = pending_pressed[p];
                tick_ins[p].released = pending_released[p];
                pending_pressed[p] = pending_released[p] = 0;
            }
            NhInput tick_in = tick_ins[0];
            audio_tick(NH_TICK_DT);
            if (s_mode == MODE_TITLE) {
                TitleAction ta = title_tick(&s_title, &tick_in);
                if (ta == TA_QUIT) {
                    plat_request_quit();
                } else if (ta == TA_NEW || ta == TA_CONTINUE) {
                    if (ta == TA_NEW) {
                        camp_new(&s_camp, (uint32_t)(plat_time_us() & 0xFFFFFFFFu) | 1u);
                        hideout_init(&s_hideout);
                        save_game();
                    }
                    s_mode = MODE_HIDEOUT;
                    audio_music_mix(k_safe_mix);
                }
            } else if (s_mode == MODE_HEIST) {
                heist_tick(&s_heist, tick_ins, &s_settings);
            } else if (tick_in.pressed & NH_BTN_HOME) {
                /* Back to the title (the career is saved after every change). */
                save_game();
                title_init(&s_title, true);
                s_mode = MODE_TITLE;
                audio_music_mix(k_title_mix);
            } else {
                s_camp.play_time += NH_TICK_DT;
                int job = -1;
                HideAction ha = hideout_tick(&s_hideout, &s_camp, &s_save_settings, &tick_in, &job);
                if (ha == HA_SAVE) {
                    apply_settings();
                    save_game();
                } else if (ha == HA_START_JOB) {
                    start_job = job;
                }
            }
        }

        fps_frames++;
        if (now - fps_start >= 1000000u) {
            fps = (float)fps_frames * 1000000.0f / (float)(now - fps_start);
            fps_frames = 0;
            fps_start = now;
        }
        if (s_mode == MODE_TITLE) {
            title_render(&s_title, &s_camp);
        } else if (s_mode == MODE_HEIST) {
            heist_render(&s_heist, nh_fixedstep_alpha(&step), &s_settings, fps);
        } else {
            hideout_render(&s_hideout, &s_camp, &s_save_settings);
        }

        if (start_job >= 0) {
            /* Off to work. */
            s_offer = s_camp.board[start_job];
            NhGenParams gp;
            camp_offer_params(&s_camp, &s_offer, &gp);
            nh_arena_reset(&s_level);
            nh_arena_reset(&s_scratch);
            loading_screen("casing the job...");
            if (!heist_start(&s_heist, &gp, &s_camp, &s_offer, s_save_settings.difficulty, s_hideout.players, &s_level,
                             &s_scratch))
                break;
            s_mode = MODE_HEIST;
            audio_music(THEME_HEIST);
            last = plat_time_us();
        } else if (s_mode == MODE_HEIST && s_heist.done) {
            /* Back home: book the job, save, show the payday. */
            NhPayday pay;
            camp_finish_job(&s_camp, &s_offer, &s_heist.result, &s_heist.state, &s_heist.world.L, &s_heist.plan,
                            s_heist.plan.fingerprint, s_heist.plan.setpieces, &pay);
            s_camp.play_time += s_heist.state.time;
            s_camp.tips_seen |= s_heist.tips_seen;
            playlog(&s_heist, &pay);
            static const char *const k_title[] = { "", "PAYDAY", "PARTIAL TAKE", "WALKED AWAY", "BUSTED" };
            hideout_payday(&s_hideout, &pay, k_title[s_heist.result.outcome]);
            heist_end(&s_heist);
            save_game();
            s_mode = MODE_HIDEOUT;
            audio_music(THEME_SAFEHOUSE);
            audio_music_mix(k_safe_mix);
            plat_rumble(0, false);
            plat_rumble(1, false);
        } else if (s_mode == MODE_HEIST && s_heist.want_rebuild) {
            NhGenParams next = s_heist.job;
            heist_end(&s_heist);
            nh_arena_reset(&s_level);
            nh_arena_reset(&s_scratch);
            loading_screen("casing the job...");
            if (!heist_start(&s_heist, &next, NULL, NULL, s_save_settings.difficulty, s_heist.nplayers, &s_level,
                             &s_scratch))
                break;
            last = plat_time_us();
        }
    }

    if (s_mode == MODE_HEIST)
        heist_end(&s_heist);
    save_game();
    plat_rumble(0, false);
    plat_rumble(1, false);
    plat_shutdown();
    return 0;
}
