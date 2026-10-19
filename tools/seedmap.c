/* Night Heist - top-down seed map renderer.
 *
 *   seedmap TYPE SIZE SEED [SECURITY] out.png     one building, every floor
 *   seedmap --sheet TYPE SIZE COUNT out.png       COUNT seeds of one kind, ground floors
 *
 * TYPE: warehouse office bank mansion museum (or 0-4); SIZE: small medium
 * large (or 0-2). Rooms are tinted by function; doors by lock (green none,
 * yellow pick, blue keycard, purple code, orange maglock, red vault);
 * windows light blue; stairs grey; vents dotted; loot gold, the objective
 * a white-ringed star; keys cyan; cameras red; guards' routes magenta
 * (dog handlers brown); entries green and the extraction van lime.
 *
 * Builds against the core only (make seedmap). Writes an uncompressed PNG. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/nh_font.h"
#include "core/nh_gen.h"
#include "core/nh_rooms.h"

#define PX 10 /* pixels per metre */

static uint8_t *s_img;
static int s_w, s_h;

static void put(int x, int y, uint32_t rgb)
{
    if (x < 0 || y < 0 || x >= s_w || y >= s_h)
        return;
    uint8_t *p = s_img + (y * s_w + x) * 3;
    p[0] = (uint8_t)(rgb >> 16);
    p[1] = (uint8_t)(rgb >> 8);
    p[2] = (uint8_t)rgb;
}

static void rect(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            put(x, y, rgb);
}

static void line(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        put(x0, y0, rgb);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void text(int x, int y, int scale, uint32_t rgb, const char *s)
{
    for (; *s; s++, x += NH_FONT_ADVANCE * scale) {
        int ch = (unsigned char)*s;
        if (ch < NH_FONT_FIRST || ch >= NH_FONT_FIRST + NH_FONT_GLYPHS)
            ch = '?';
        const uint8_t *g = nh_font5x7[ch - NH_FONT_FIRST];
        for (int r = 0; r < 7; r++)
            for (int col = 0; col < 5; col++)
                if (g[r] & (0x10 >> col))
                    rect(x + col * scale, y + r * scale, x + col * scale + scale - 1, y + r * scale + scale - 1, rgb);
    }
}

static uint32_t func_color(int func)
{
    switch (func) {
    case RF_CORRIDOR: case RF_STAIRWELL: return 0x8a8f99;
    case RF_LOBBY: case RF_BANK_HALL: return 0xd8c38a;
    case RF_VAULT: case RF_PANIC_ROOM: return 0xd06050;
    case RF_SERVER: return 0x6f8fd0;
    case RF_SECURITY: return 0xc07070;
    case RF_EXEC_OFFICE: case RF_STUDY: return 0xb08f60;
    case RF_EXHIBIT: return 0xe0a040;
    case RF_GALLERY: return 0xd8d0b8;
    case RF_WAREHOUSE: case RF_LOADING: return 0x9b9070;
    case RF_STORAGE: case RF_UTILITY: case RF_ARCHIVE: return 0x8f8f80;
    case RF_BATHROOM: case RF_KITCHEN: return 0x9fc0c8;
    case RF_BEDROOM: return 0xc8a0b0;
    default: {
        uint32_t h = (uint32_t)func * 2654435761u;
        return 0x707070 + ((h >> 8) & 0x3f3f3f);
    }
    }
}

static uint32_t lock_color(int lock)
{
    switch (lock) {
    case LV_LOCK_NONE: return 0x30c050;
    case LV_LOCK_PICK: return 0xe0d040;
    case LV_LOCK_KEYCARD: return 0x3070ff;
    case LV_LOCK_CODE: return 0xb050e0;
    case LV_LOCK_POWERED: return 0xff9020;
    default: return 0xff2020;
    }
}

static void draw_floor(const Level *L, const NhPlan *P, int f, int ox, int oy)
{
    const int cs = (int)(LV_CELL * PX);
    for (int z = 0; z < L->d; z++)
        for (int x = 0; x < L->w; x++) {
            const LvCell *c = &L->cells[f][z][x];
            uint32_t col = 0x1a1d24;
            if (c->room == LV_ROOM_OUTSIDE)
                col = 0x2c3038;
            else if (c->room >= LV_ROOM_FIRST)
                col = func_color(L->rooms[c->room].func);
            if (c->flags & LV_C_HOLE)
                col = 0x101010;
            if (c->flags & LV_C_EXTRACT)
                col = 0x40a040;
            rect(ox + x * cs, oy + z * cs, ox + x * cs + cs - 1, oy + z * cs + cs - 1, col);
            if (c->stair) {
                for (int k = 2; k < cs; k += 4)
                    rect(ox + x * cs + 2, oy + z * cs + k, ox + x * cs + cs - 3, oy + z * cs + k, 0x505560);
            }
            if (c->flags & LV_C_VENT)
                for (int k = 3; k < cs; k += 6)
                    rect(ox + x * cs + k, oy + z * cs + cs / 2, ox + x * cs + k + 1, oy + z * cs + cs / 2 + 1, 0x20e0e0);
            if (c->flags & LV_C_GRATE)
                rect(ox + x * cs + 6, oy + z * cs + 6, ox + x * cs + cs - 7, oy + z * cs + cs - 7, 0x20a0a0);
            if (c->flags & LV_C_HATCH)
                rect(ox + x * cs + 4, oy + z * cs + 4, ox + x * cs + cs - 5, oy + z * cs + cs - 5, 0xf0f0f0);
        }
    /* Props (footprints). */
    for (int i = 0; i < L->nprops; i++) {
        const LvProp *p = &L->props[i];
        if (p->floor != f || p->y > lv_floor_y(f) + 1.5f)
            continue;
        float hx = p->half.x, hz = p->half.z;
        if ((int)(p->yaw * 1.2732f + 8.5f) % 2)
            hx = p->half.z, hz = p->half.x;
        rect(ox + (int)((p->x - hx) * PX), oy + (int)((p->z - hz) * PX), ox + (int)((p->x + hx) * PX),
             oy + (int)((p->z + hz) * PX), (p->flags & LV_P_SOLID) ? 0x4a4038 : 0x605848);
    }
    /* Edges */
    for (int z = 0; z < L->d; z++)
        for (int x = 0; x < L->w; x++)
            for (int s = 0; s < 2; s++) {
                const LvEdge *e = lv_edge_side_c(L, f, x, z, s);
                if (!e || e->type == LV_E_OPEN)
                    continue;
                int x0, y0, x1, y1;
                if (s == 0) {
                    x0 = x1 = ox + (x + 1) * cs;
                    y0 = oy + z * cs;
                    y1 = y0 + cs;
                } else {
                    y0 = y1 = oy + (z + 1) * cs;
                    x0 = ox + x * cs;
                    x1 = x0 + cs;
                }
                uint32_t col = 0x000000;
                int th = 1;
                if (e->type == LV_E_WINDOW)
                    col = 0x90c8ff;
                else if (e->type == LV_E_RAIL)
                    col = 0xd0d0d0, th = 0;
                else if (e->type == LV_E_ARCH)
                    continue;
                else if (e->type == LV_E_DOOR && e->ref)
                    col = lock_color(L->doors[e->ref - 1].lock), th = 2;
                rect(NH_MIN(x0, x1) - th, NH_MIN(y0, y1) - th, NH_MAX(x0, x1) + th, NH_MAX(y0, y1) + th, col);
            }
    /* Entities */
    for (int i = 0; i < P->nent; i++) {
        const NhEntity *e = &P->ent[i];
        if (e->floor != f)
            continue;
        int px = ox + (int)(e->pos.x * PX), py = oy + (int)(e->pos.z * PX);
        switch (e->kind) {
        case ENT_LOOT:
            if (i == P->objective) {
                rect(px - 6, py - 6, px + 6, py + 6, 0xffffff);
                rect(px - 4, py - 4, px + 4, py + 4, 0xffd000);
            } else
                rect(px - 2, py - 2, px + 2, py + 2, e->tier >= 3 ? 0xffb000 : 0xd0a020);
            break;
        case ENT_KEYCARD: case ENT_CODE:
            rect(px - 3, py - 3, px + 3, py + 3, 0x000000);
            rect(px - 2, py - 2, px + 2, py + 2, e->kind == ENT_KEYCARD ? 0x30e0ff : 0xe080ff);
            break;
        case ENT_CAMERA:
            rect(px - 3, py - 3, px + 3, py + 3, 0xff2020);
            break;
        case ENT_FUSE_BOX:
            rect(px - 3, py - 3, px + 3, py + 3, 0xff9020);
            break;
        case ENT_SAFE: case ENT_TERMINAL: case ENT_CONSOLE:
            rect(px - 3, py - 3, px + 3, py + 3, 0x20d0a0);
            break;
        case ENT_LASER: {
            float len = (float)e->value / 100.0f;
            NhVec3 d = nh_dir_from_yaw_pitch(e->yaw, 0);
            line(px, py, px + (int)(d.x * len * PX), py + (int)(d.z * len * PX), 0xff3030);
            break;
        }
        default:
            break;
        }
    }
    /* Guards */
    for (int i = 0; i < P->nguards; i++) {
        const NhGuardPlan *g = &P->guards[i];
        if (lv_floor_of(g->pos.y + 0.3f) != f)
            continue;
        uint32_t col = g->arch == ARCH_HANDLER || g->arch == ARCH_DOG ? 0xa06030 : 0xff40ff;
        for (int k = 0; k < g->nroute; k++) {
            NhVec3 a = g->route[k].pos, b = g->route[(k + 1) % g->nroute].pos;
            if (lv_floor_of(a.y + 0.3f) == f && lv_floor_of(b.y + 0.3f) == f)
                line(ox + (int)(a.x * PX), oy + (int)(a.z * PX), ox + (int)(b.x * PX), oy + (int)(b.z * PX), col);
        }
        int px = ox + (int)(g->pos.x * PX), py = oy + (int)(g->pos.z * PX);
        rect(px - 4, py - 4, px + 4, py + 4, 0x000000);
        rect(px - 3, py - 3, px + 3, py + 3, col);
    }
    if (f == 0) {
        for (int i = 0; i < P->nentry; i++) {
            int px = ox + (int)(P->entry[i].x * PX), py = oy + (int)(P->entry[i].z * PX);
            rect(px - 5, py - 5, px + 5, py + 5, 0x00ff60);
        }
    }
}

static void write_png(const char *path)
{
    /* Stored (uncompressed) deflate blocks. */
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        perror(path);
        exit(1);
    }
    static uint32_t crc_t[256];
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_t[n] = c;
    }
    size_t raw_len = (size_t)s_h * (size_t)(s_w * 3 + 1);
    uint8_t *raw = malloc(raw_len);
    for (int y = 0; y < s_h; y++) {
        raw[y * (s_w * 3 + 1)] = 0;
        memcpy(raw + y * (s_w * 3 + 1) + 1, s_img + y * s_w * 3, (size_t)s_w * 3);
    }
    size_t nblocks = (raw_len + 65534) / 65535;
    size_t zlen = 2 + nblocks * 5 + raw_len + 4;
    uint8_t *z = malloc(zlen);
    size_t o = 0;
    z[o++] = 0x78;
    z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i += 65535) {
        size_t n = raw_len - i < 65535 ? raw_len - i : 65535;
        z[o++] = i + n >= raw_len ? 1 : 0;
        z[o++] = (uint8_t)n;
        z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n;
        z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + i, n);
        o += n;
    }
    for (size_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    uint32_t adler = (b << 16) | a;
    z[o++] = (uint8_t)(adler >> 24);
    z[o++] = (uint8_t)(adler >> 16);
    z[o++] = (uint8_t)(adler >> 8);
    z[o++] = (uint8_t)adler;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, fp);
    uint8_t ihdr[13] = { 0 };
    ihdr[0] = (uint8_t)(s_w >> 24), ihdr[1] = (uint8_t)(s_w >> 16), ihdr[2] = (uint8_t)(s_w >> 8), ihdr[3] = (uint8_t)s_w;
    ihdr[4] = (uint8_t)(s_h >> 24), ihdr[5] = (uint8_t)(s_h >> 16), ihdr[6] = (uint8_t)(s_h >> 8), ihdr[7] = (uint8_t)s_h;
    ihdr[8] = 8;
    ihdr[9] = 2;
    const struct {
        const char *type;
        const uint8_t *data;
        size_t len;
    } chunks[3] = { { "IHDR", ihdr, 13 }, { "IDAT", z, o }, { "IEND", NULL, 0 } };
    for (int k = 0; k < 3; k++) {
        uint8_t hdr[8] = { (uint8_t)(chunks[k].len >> 24), (uint8_t)(chunks[k].len >> 16), (uint8_t)(chunks[k].len >> 8),
                           (uint8_t)chunks[k].len, 0, 0, 0, 0 };
        memcpy(hdr + 4, chunks[k].type, 4);
        fwrite(hdr, 1, 8, fp);
        if (chunks[k].len)
            fwrite(chunks[k].data, 1, chunks[k].len, fp);
        uint32_t c = 0xffffffffu;
        for (int i = 4; i < 8; i++)
            c = crc_t[(c ^ hdr[i]) & 0xff] ^ (c >> 8);
        for (size_t i = 0; i < chunks[k].len; i++)
            c = crc_t[(c ^ chunks[k].data[i]) & 0xff] ^ (c >> 8);
        c ^= 0xffffffffu;
        uint8_t cb[4] = { (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(cb, 1, 4, fp);
    }
    fclose(fp);
    free(raw);
    free(z);
}

static int parse(const char *s, const char *const *names, int n)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(s, names[i]))
            return i;
    return atoi(s) % n;
}

static Level s_L;
static NhPlan s_P;

int main(int argc, char **argv)
{
    if (argc >= 6 && !strcmp(argv[1], "--sheet")) {
        int type = parse(argv[2], nh_building_names, BT_COUNT), size = parse(argv[3], nh_size_names, BS_COUNT);
        int count = atoi(argv[4]);
        int cols = count < 4 ? count : 4, rows = (count + cols - 1) / cols;
        int tw = 0, th = 0;
        NhGenParams gp = { 0 };
        gp.type = (uint8_t)type;
        gp.size = (uint8_t)size;
        gp.security = 2;
        gp.seed = 1;
        nh_gen_building(&s_L, &gp, &s_P);
        tw = (int)(LV_W_MAX * LV_CELL * PX) / 2 + 10;
        th = (int)(LV_D_MAX * LV_CELL * PX) / 2 + 30;
        (void)tw;
        (void)th;
        int cw = 0, ch = 0;
        /* Measure the biggest building of the set. */
        for (int i = 0; i < count; i++) {
            gp.seed = (uint32_t)(i + 1);
            nh_gen_building(&s_L, &gp, &s_P);
            cw = NH_MAX(cw, (int)(s_L.w * LV_CELL * PX));
            ch = NH_MAX(ch, (int)(s_L.d * LV_CELL * PX));
        }
        s_w = cols * (cw + 10) + 10;
        s_h = rows * (ch + 30) + 10;
        s_img = calloc((size_t)s_w * (size_t)s_h, 3);
        for (int i = 0; i < count; i++) {
            gp.seed = (uint32_t)(i + 1);
            nh_gen_building(&s_L, &gp, &s_P);
            int ox = 10 + (i % cols) * (cw + 10), oy = 30 + (i / cols) * (ch + 30);
            draw_floor(&s_L, &s_P, 0, ox, oy);
            char buf[64];
            snprintf(buf, sizeof(buf), "seed %d  %08X", i + 1, (unsigned)s_P.fingerprint);
            text(ox, oy - 20, 2, 0xffffff, buf);
        }
        write_png(argv[5]);
        return 0;
    }
    if (argc < 5) {
        fprintf(stderr, "usage: seedmap TYPE SIZE SEED [SECURITY] out.png | --sheet TYPE SIZE COUNT out.png\n");
        return 1;
    }
    NhGenParams gp = { 0 };
    gp.type = (uint8_t)parse(argv[1], nh_building_names, BT_COUNT);
    gp.size = (uint8_t)parse(argv[2], nh_size_names, BS_COUNT);
    gp.seed = (uint32_t)strtoul(argv[3], NULL, 0);
    gp.security = argc >= 6 ? (uint8_t)atoi(argv[4]) : 2;
    const char *out = argv[argc - 1];
    if (!nh_gen_building(&s_L, &gp, &s_P)) {
        fprintf(stderr, "generation failed\n");
        return 1;
    }
    int fw = (int)(s_L.w * LV_CELL * PX), fh = (int)(s_L.d * LV_CELL * PX);
    int nf = s_L.floors + 1;
    s_w = nf * (fw + 10) + 10;
    s_h = fh + 60;
    s_img = calloc((size_t)s_w * (size_t)s_h, 3);
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s  seed %u  sec %d  fp %08X  rooms %d  guards %d  loot $%d  set pieces %d",
             nh_building_names[gp.type], nh_size_names[gp.size], (unsigned)gp.seed, gp.security,
             (unsigned)s_P.fingerprint, s_L.nrooms - 2, s_P.nguards, s_P.total_loot * 10, s_P.nsetpieces);
    text(10, 8, 2, 0xffffff, buf);
    for (int f = 0; f < nf; f++) {
        int ox = 10 + f * (fw + 10), oy = 50;
        draw_floor(&s_L, &s_P, f, ox, oy);
        snprintf(buf, sizeof(buf), f == s_L.floors ? "roof" : "floor %d", f);
        text(ox, oy - 16, 2, 0xc0c0c0, buf);
    }
    write_png(out);
    printf("%s\n", buf);
    for (int i = 0; i < 64; i++)
        if ((s_P.setpieces >> i) & 1u)
            printf("  set piece: %s\n", nh_gen_setpiece_name(i));
    return 0;
}
