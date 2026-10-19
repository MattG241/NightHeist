/* Tests for the procedural generator: solvability over 10,000 seeds (set
 * NH_GEN_SEEDS to change), determinism with golden fingerprints, layout
 * distinctness, anti-staleness, set-piece coverage, and navigation reach
 * (furniture never cuts a room off) on a sample of buildings. */
#include <stdlib.h>
#include <string.h>

#include "core/nh_gen.h"
#include "core/nh_nav.h"
#include "core/nh_rooms.h"
#include "nh_test.h"

static Level s_L, s_L2;
static NhPlan s_P, s_P2;
static NhNav s_nav;

static NhGenParams params(int i)
{
    NhGenParams gp;
    memset(&gp, 0, sizeof(gp));
    gp.seed = (uint32_t)i * 7919u + 13u;
    gp.type = (uint8_t)(i % BT_COUNT);
    gp.size = (uint8_t)((i / BT_COUNT) % BS_COUNT);
    gp.security = (uint8_t)((i / (BT_COUNT * BS_COUNT)) % 5);
    return gp;
}

static bool sane(const Level *L, const NhPlan *P)
{
    bool ok = true;
    for (int i = 0; i < L->nprops; i++) {
        const LvProp *p = &L->props[i];
        ok &= p->x >= 0.0f && p->z >= 0.0f && p->x <= (float)L->w * LV_CELL && p->z <= (float)L->d * LV_CELL;
        ok &= p->floor <= L->floors;
    }
    ok &= L->nstairs == L->floors - 1;
    for (int i = 0; i < P->nguards; i++) {
        const NhGuardPlan *g = &P->guards[i];
        int f = lv_floor_of(g->pos.y + 0.3f);
        const LvCell *c = lv_cell(L, f, lv_cell_of(g->pos.x), lv_cell_of(g->pos.z));
        ok &= c && c->room != LV_ROOM_VOID && !(c->flags & LV_C_HOLE);
    }
    for (int i = 0; i < P->nent; i++)
        ok &= P->ent[i].room < L->nrooms;
    return ok;
}

void test_gen_solvable(void)
{
    int n = 10000;
    const char *env = getenv("NH_GEN_SEEDS");
    if (env)
        n = atoi(env);
    int failed = 0, retried = 0, insane = 0;
    uint64_t used = 0;
    long rooms = 0;
    for (int i = 0; i < n; i++) {
        NhGenParams gp = params(i);
        if (!nh_gen_building(&s_L, &gp, &s_P)) {
            if (failed++ < 5) {
                char why[96] = "";
                nh_gen_validate(&s_L, &s_P, why, sizeof(why));
                fprintf(stderr, "  seed %u %s %s: %s\n", (unsigned)gp.seed, nh_building_names[gp.type],
                        nh_size_names[gp.size], why);
            }
            continue;
        }
        retried += s_P.attempts > 1;
        if (!sane(&s_L, &s_P))
            insane++;
        used |= s_P.setpieces;
        rooms += s_L.nrooms - LV_ROOM_FIRST;
    }
    CHECK(failed == 0);
    CHECK(insane == 0);
    CHECK(retried * 20 < n); /* constructive placement: retries are rare */
    /* At least 40 set pieces, and every one of them actually appears. */
    int nsp = nh_gen_setpiece_count();
    CHECK(nsp >= 40);
    if (n >= 1000)
        for (int i = 0; i < nsp; i++) {
            if (!((used >> i) & 1u))
                fprintf(stderr, "  set piece never used: %s\n", nh_gen_setpiece_name(i));
            CHECK((used >> i) & 1u);
        }
    printf("  %d buildings, %d needed a retry, %.1f rooms on average\n", n, retried, (double)rooms / n);
}

void test_gen_determinism(void)
{
    for (int i = 0; i < 30; i++) {
        NhGenParams gp = params(i * 37);
        CHECK(nh_gen_building(&s_L, &gp, &s_P));
        CHECK(nh_gen_building(&s_L2, &gp, &s_P2));
        CHECK(s_P.fingerprint == s_P2.fingerprint);
        CHECK(memcmp(s_L.cells, s_L2.cells, sizeof(s_L.cells)) == 0);
        CHECK(memcmp(s_L.ex, s_L2.ex, sizeof(s_L.ex)) == 0);
        CHECK(memcmp(s_L.ez, s_L2.ez, sizeof(s_L.ez)) == 0);
        CHECK(s_L.nprops == s_L2.nprops && memcmp(s_L.props, s_L2.props, sizeof(LvProp) * (size_t)s_L.nprops) == 0);
        CHECK(s_P.nent == s_P2.nent && memcmp(s_P.ent, s_P2.ent, sizeof(NhEntity) * (size_t)s_P.nent) == 0);
        CHECK(s_P.nguards == s_P2.nguards);
    }
    /* Golden layouts: the same seed must give the same building on every
     * platform (the Wii debug overlay shows the fingerprint to compare). */
    static const struct {
        uint32_t seed;
        uint8_t type, size;
        uint32_t fp;
    } golden[] = {
#include "gen_golden.inc"
    };
    for (unsigned i = 0; i < NH_ARRAY_LEN(golden); i++) {
        NhGenParams gp;
        memset(&gp, 0, sizeof(gp));
        gp.seed = golden[i].seed;
        gp.type = golden[i].type;
        gp.size = golden[i].size;
        gp.security = 2;
        CHECK(nh_gen_building(&s_L, &gp, &s_P));
        if (s_P.fingerprint != golden[i].fp)
            fprintf(stderr, "  golden %u: got %08X want %08X\n", i, (unsigned)s_P.fingerprint, (unsigned)golden[i].fp);
        CHECK(s_P.fingerprint == golden[i].fp);
    }
}

void test_gen_distinct(void)
{
    enum { N = 30 };
    static uint8_t sig[N][GEN_SIG_BYTES];
    uint32_t fps[N];
    for (int t = 0; t < BT_COUNT; t++)
        for (int z = 0; z < BS_COUNT; z++) {
            for (int i = 0; i < N; i++) {
                NhGenParams gp;
                memset(&gp, 0, sizeof(gp));
                gp.seed = (uint32_t)(5000 + i);
                gp.type = (uint8_t)t;
                gp.size = (uint8_t)z;
                gp.security = 2;
                CHECK(nh_gen_building(&s_L, &gp, &s_P));
                nh_gen_signature(&s_L, sig[i]);
                fps[i] = s_P.fingerprint;
            }
            int close = 0, dupes = 0;
            float sum = 0.0f, minimum = 1.0f;
            for (int i = 0; i < N; i++)
                for (int j = i + 1; j < N; j++) {
                    float d = nh_gen_signature_distance(sig[i], sig[j]);
                    sum += d;
                    minimum = d < minimum ? d : minimum;
                    close += d < 0.1f;
                    dupes += fps[i] == fps[j];
                }
            float mean = sum / (float)(N * (N - 1) / 2);
            CHECK(dupes == 0);
            CHECK(close == 0);
            CHECK(mean > 0.3f);
            if (close || mean <= 0.3f)
                fprintf(stderr, "  %s %s: mean %.2f min %.2f close %d\n", nh_building_names[t], nh_size_names[z],
                        (double)mean, (double)minimum, close);
        }
    /* Anti-staleness: asking to avoid a layout never returns it. */
    NhGenParams gp = params(3);
    CHECK(nh_gen_building(&s_L, &gp, &s_P));
    uint32_t fp = s_P.fingerprint;
    gp.avoid_layouts = &fp;
    gp.navoid = 1;
    CHECK(nh_gen_building(&s_L, &gp, &s_P));
    CHECK(s_P.fingerprint != fp);
    /* Recently used set pieces are avoided when there's a choice. */
    int same = 0, total = 0;
    for (int i = 0; i < 15; i++) {
        gp = params(10 + i);
        gp.navoid = 0;
        gp.avoid_layouts = NULL;
        CHECK(nh_gen_building(&s_L, &gp, &s_P));
        uint64_t first = s_P.setpieces;
        gp.avoid_setpieces = first;
        CHECK(nh_gen_building(&s_L, &gp, &s_P));
        same += __builtin_popcountll(first & s_P.setpieces);
        total += __builtin_popcountll(first);
    }
    CHECK(same * 2 < total);
}

/* Every room's floor can be walked to from the street (doors open), so
 * furniture never seals a room or a doorway. */
void test_gen_nav(void)
{
    int bad = 0;
    for (int i = 0; i < 45; i++) {
        NhGenParams gp = params(i * 11 + 1);
        CHECK(nh_gen_building(&s_L, &gp, &s_P));
        for (int d = 0; d < s_L.ndoors; d++) {
            s_L.doors[d].open = 1.0f;
            s_L.doors[d].locked = 0;
            s_L.doors[d].lock = LV_LOCK_NONE;
        }
        nh_nav_build(&s_nav, &s_L, 0.3f);
        NhVec3 start = s_P.entry[0];
        for (int r = LV_ROOM_FIRST; r < s_L.nrooms; r++) {
            const LvRoom *rr = &s_L.rooms[r];
            if (rr->floor >= s_L.floors)
                continue;
            /* The walkable nodes nearest the room centre (up to four tries:
             * a stray node behind furniture may be cut off). */
            NhVec3 ctr = nh_v3(((float)rr->x0 + (float)rr->x1 + 1.0f), lv_floor_y(rr->floor),
                               ((float)rr->z0 + (float)rr->z1 + 1.0f));
            int tried = 0;
            bool found = false, ok = false;
            float last = -1.0f;
            NhPath path;
            for (int attempt = 0; attempt < 4 && !ok; attempt++) {
                float best = 1e9f;
                NhVec3 goal = ctr;
                for (int z = rr->z0 * NAV_RES; z <= rr->z1 * NAV_RES + 1; z++)
                    for (int x = rr->x0 * NAV_RES; x <= rr->x1 * NAV_RES + 1; x++) {
                        NhVec3 q = nh_v3((float)x + 0.5f, lv_floor_y(rr->floor), (float)z + 0.5f);
                        const LvCell *cl = &s_L.cells[rr->floor][lv_cell_of(q.z)][lv_cell_of(q.x)];
                        if (cl->room != r || cl->stair || !nh_nav_walkable(&s_nav, q))
                            continue;
                        float d = nh_v3_dist_xz(q, ctr) + (float)x * 1e-4f + (float)z * 1e-6f;
                        if (d > last && d < best) {
                            best = d;
                            goal = q;
                        }
                    }
                if (best >= 1e9f)
                    break;
                last = best;
                found = true;
                tried++;
                ok = nh_nav_path(&s_nav, start, goal, true, &path);
            }
            (void)tried;
            if (!ok && bad++ < 6)
                fprintf(stderr, "  %s %s seed %u: room %d (%s, floor %d) %s\n", nh_building_names[gp.type],
                        nh_size_names[gp.size], (unsigned)gp.seed, r, nh_room_styles[rr->func].name, rr->floor,
                        found ? "unreachable" : "has no free floor");
            CHECK(ok);
        }
        /* Guards start somewhere walkable. */
        for (int g = 0; g < s_P.nguards; g++) {
            NhVec3 s;
            CHECK(nh_nav_snap(&s_nav, s_P.guards[g].pos, &s));
        }
    }
}
