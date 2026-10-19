#include "nh_save.h"

#include <stdio.h>
#include <string.h>

uint32_t nh_crc32(const void *data, size_t len)
{
    static uint32_t table[256];
    static bool ready;
    if (!ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void save_default_settings(NhSaveSettings *s)
{
    memset(s, 0, sizeof(*s));
    nh_controls_default(&s->controls);
    s->rumble = 1;
    s->subtitles = 1;
    s->music = 7;
    s->sfx = 8;
    s->difficulty = 1;
    s->brightness = 3;
}

/* ---- writer / reader ------------------------------------------------------- */

typedef struct W {
    uint8_t *p;
    size_t n, cap;
    bool ok;
} W;

static void w8(W *w, uint32_t v)
{
    if (w->n >= w->cap) {
        w->ok = false;
        return;
    }
    w->p[w->n++] = (uint8_t)v;
}
static void w16(W *w, uint32_t v) { w8(w, v >> 8), w8(w, v); }
static void w32(W *w, uint32_t v) { w16(w, v >> 16), w16(w, v); }
static void wf(W *w, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    w32(w, u);
}

typedef struct R {
    const uint8_t *p;
    size_t n, len;
    bool ok;
} R;

static uint32_t r8(R *r)
{
    if (r->n >= r->len) {
        r->ok = false;
        return 0;
    }
    return r->p[r->n++];
}
static uint32_t r16(R *r)
{
    uint32_t a = r8(r);
    return (a << 8) | r8(r);
}
static uint32_t r32(R *r)
{
    uint32_t a = r16(r);
    return (a << 16) | r16(r);
}
static float rf(R *r)
{
    uint32_t u = r32(r);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* Clamped reads (the NH_ macros evaluate their arguments more than once). */
static float rf_clamp(R *r, float lo, float hi)
{
    float v = rf(r);
    return v != v ? lo : NH_CLAMP(v, lo, hi);
}

static uint32_t r8_max(R *r, uint32_t hi)
{
    uint32_t v = r8(r);
    return NH_MIN(v, hi);
}

static void write_settings(W *w, const NhSaveSettings *s)
{
    wf(w, s->controls.ir_smoothing);
    wf(w, s->controls.box_w);
    wf(w, s->controls.box_h);
    wf(w, s->controls.turn_speed);
    w8(w, s->controls.crouch_toggle);
    w8(w, s->controls.invert_y);
    w8(w, s->rumble);
    w8(w, s->subtitles);
    w8(w, s->music);
    w8(w, s->sfx);
    w8(w, s->difficulty);
    w8(w, s->playlog);
    w8(w, (uint32_t)s->controls.scheme);
    w8(w, s->brightness);
}

static void read_settings(R *r, NhSaveSettings *s, int version)
{
    s->controls.ir_smoothing = rf_clamp(r, 0.0f, 0.95f);
    s->controls.box_w = rf_clamp(r, 0.1f, 0.9f);
    s->controls.box_h = rf_clamp(r, 0.1f, 0.9f);
    s->controls.turn_speed = rf_clamp(r, 0.5f, 8.0f);
    s->controls.crouch_toggle = r8(r) != 0;
    s->controls.invert_y = r8(r) != 0;
    s->rumble = (uint8_t)r8(r);
    s->subtitles = (uint8_t)r8(r);
    s->music = (uint8_t)r8_max(r, 10);
    s->sfx = (uint8_t)r8_max(r, 10);
    s->difficulty = (uint8_t)r8_max(r, 2);
    s->playlog = (uint8_t)r8(r);
    s->controls.scheme = version >= 2 ? (int)r8_max(r, NH_CONTROLS_COUNT - 1) : NH_CONTROLS_SIMPLE;
    if (version >= 3) {
        uint32_t b = r8_max(r, 5);
        s->brightness = (uint8_t)(b ? b : 3);
    } else {
        s->brightness = 3;
    }
}

static void write_offer(W *w, const NhJobOffer *o)
{
    w32(w, o->seed);
    w8(w, o->district);
    w8(w, o->type);
    w8(w, o->size);
    w8(w, o->security);
    w16(w, o->mods);
    w8(w, o->loyalty_crew);
    w8(w, o->finale);
    w32(w, (uint32_t)o->est_payout);
    for (int i = 0; i < (int)sizeof(o->name); i++)
        w8(w, (uint8_t)o->name[i]);
}

static void read_offer(R *r, NhJobOffer *o)
{
    o->seed = r32(r);
    o->district = (uint8_t)r8_max(r, DI_COUNT - 1);
    o->type = (uint8_t)r8_max(r, BT_COUNT - 1);
    o->size = (uint8_t)r8_max(r, BS_COUNT - 1);
    o->security = (uint8_t)r8_max(r, 4);
    o->mods = (uint16_t)r16(r);
    o->loyalty_crew = (uint8_t)r8_max(r, CR_COUNT);
    o->finale = (uint8_t)r8(r);
    o->est_payout = (int32_t)r32(r);
    for (int i = 0; i < (int)sizeof(o->name); i++)
        o->name[i] = (char)r8(r);
    o->name[sizeof(o->name) - 1] = 0;
}

static void write_campaign(W *w, const NhCampaign *c)
{
    w32(w, c->seed);
    w32(w, (uint32_t)c->money);
    w32(w, (uint32_t)c->reputation);
    for (int d = 0; d < DI_COUNT; d++) {
        w8(w, c->heat[d]);
        w8(w, c->district_done[d]);
        w8(w, c->district_jobs[d]);
    }
    w8(w, c->postgame);
    w8(w, c->crew_hired);
    w8(w, c->crew_selected);
    w8(w, c->crew_perk2);
    for (int m = 0; m < CR_COUNT; m++)
        w8(w, c->crew_loyalty[m]);
    for (int u = 0; u < UP_COUNT; u++)
        w8(w, c->upgrade[u]);
    for (int s = 0; s < LS_COUNT; s++) {
        w8(w, c->set_count[s]);
        w8(w, c->sets_done[s]);
    }
    w8(w, c->history_n);
    for (int i = 0; i < HISTORY_MAX; i++)
        w32(w, c->history_fp[i]);
    for (int i = 0; i < 3; i++) {
        w32(w, (uint32_t)(c->sp_hist[i] >> 32));
        w32(w, (uint32_t)c->sp_hist[i]);
    }
    w16(w, c->jobs_done);
    w16(w, c->jobs_failed);
    w16(w, c->ghosts);
    w32(w, c->total_earned);
    wf(w, c->play_time);
    w32(w, c->board_gen);
    w32(w, c->tips_seen);
    w8(w, c->board_n);
    for (int i = 0; i < c->board_n; i++)
        write_offer(w, &c->board[i]);
}

static void read_campaign(R *r, NhCampaign *c, int version)
{
    memset(c, 0, sizeof(*c));
    c->seed = r32(r);
    c->money = (int32_t)r32(r);
    c->reputation = (int32_t)r32(r);
    for (int d = 0; d < DI_COUNT; d++) {
        c->heat[d] = (uint8_t)r8_max(r, 100);
        c->district_done[d] = (uint8_t)r8(r);
        c->district_jobs[d] = (uint8_t)r8(r);
    }
    c->postgame = (uint8_t)r8(r);
    c->crew_hired = (uint8_t)r8(r);
    c->crew_selected = (uint8_t)r8(r);
    c->crew_perk2 = (uint8_t)r8(r);
    for (int m = 0; m < CR_COUNT; m++)
        c->crew_loyalty[m] = (uint8_t)r8_max(r, 100);
    for (int u = 0; u < UP_COUNT; u++)
        c->upgrade[u] = (uint8_t)r8_max(r, nh_upgrades[u].max_level);
    for (int s = 0; s < LS_COUNT; s++) {
        c->set_count[s] = (uint8_t)r8(r);
        c->sets_done[s] = (uint8_t)r8(r);
    }
    c->history_n = (uint8_t)r8_max(r, HISTORY_MAX);
    for (int i = 0; i < HISTORY_MAX; i++)
        c->history_fp[i] = r32(r);
    for (int i = 0; i < 3; i++) {
        uint64_t hi = r32(r);
        c->sp_hist[i] = (hi << 32) | r32(r);
    }
    c->jobs_done = (uint16_t)r16(r);
    c->jobs_failed = (uint16_t)r16(r);
    c->ghosts = (uint16_t)r16(r);
    c->total_earned = r32(r);
    c->play_time = rf(r);
    c->board_gen = r32(r);
    c->tips_seen = version >= 2 ? r32(r) : 0;
    c->board_n = (uint8_t)r8_max(r, JOB_BOARD_MAX);
    for (int i = 0; i < c->board_n; i++)
        read_offer(r, &c->board[i]);
}

/* Layout: 'NHSV' u16 version u16 0 u32 payload_len u32 crc32(payload) payload */
size_t save_encode(const NhCampaign *c, const NhSaveSettings *s, uint8_t *buf, size_t cap)
{
    if (cap < 16)
        return 0;
    W w = { buf + 16, 0, cap - 16, true };
    write_settings(&w, s);
    write_campaign(&w, c);
    if (!w.ok)
        return 0;
    W h = { buf, 0, 16, true };
    w8(&h, 'N'), w8(&h, 'H'), w8(&h, 'S'), w8(&h, 'V');
    w16(&h, SAVE_VERSION);
    w16(&h, 0);
    w32(&h, (uint32_t)w.n);
    w32(&h, nh_crc32(buf + 16, w.n));
    return 16 + w.n;
}

bool save_decode(const uint8_t *buf, size_t len, NhCampaign *c, NhSaveSettings *s, char *why, int why_len)
{
#define FAIL(msg)                                                                                                      \
    do {                                                                                                               \
        if (why)                                                                                                       \
            snprintf(why, (size_t)why_len, "%s", msg);                                                                 \
        return false;                                                                                                  \
    } while (0)
    if (len < 16)
        FAIL("too short");
    if (memcmp(buf, "NHSV", 4) != 0)
        FAIL("not a Night Heist save");
    R h = { buf, 4, 16, true };
    int version = (int)r16(&h);
    uint32_t reserved = r16(&h);
    uint32_t plen = r32(&h), crc = r32(&h);
    if (version > SAVE_VERSION)
        FAIL("saved by a newer version");
    if (version < 1 || reserved != 0)
        FAIL("bad header");
    if (plen > len - 16)
        FAIL("truncated");
    if (nh_crc32(buf + 16, plen) != crc)
        FAIL("checksum mismatch");
    R r = { buf + 16, 0, plen, true };
    NhSaveSettings st;
    NhCampaign cp;
    read_settings(&r, &st, version);
    read_campaign(&r, &cp, version);
    if (!r.ok)
        FAIL("corrupt data");
    if (!cp.board_n)
        camp_make_board(&cp);
    *c = cp;
    *s = st;
    return true;
#undef FAIL
}
