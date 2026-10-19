/* Night Heist - security, keys, loot, entry/extraction and guard rosters.
 *
 * Locks go on the doors into secure rooms. Each lock's key (keycard, keypad
 * code, fuse box for maglocks, card + code for vaults) is placed in a room
 * that is reachable without passing that lock, given the keys placed before
 * it, so every building can be solved by construction. nh_gen_validate then
 * checks the result independently. */
#include <math.h>
#include <string.h>

#include "nh_gen_internal.h"

/* ---- room graph ------------------------------------------------------------------ */

void gen_room_graph_depths(GenCtx *c)
{
    Level *L = c->L;
    static int queue[LV_ROOMS_MAX];
    for (int r = 0; r < L->nrooms; r++) {
        c->gr[r].depth = 255;
        c->gr[r].parent = -1;
        c->gr[r].entry_door = -1;
    }
    int qh = 0, qt = 0;
    c->gr[LV_ROOM_OUTSIDE].depth = 0;
    queue[qt++] = LV_ROOM_OUTSIDE;
    while (qh < qt) {
        int r = queue[qh++];
        const LvRoom *rr = &L->rooms[r];
        int f = rr->floor;
        /* Neighbours through doors and arches on this floor. */
        for (int z = rr->z0; z <= rr->z1; z++)
            for (int x = rr->x0; x <= rr->x1; x++) {
                if (L->cells[f][z][x].room != r)
                    continue;
                for (int s = 0; s < 4; s++) {
                    const LvEdge *e = lv_edge_side_c(L, f, x, z, s);
                    if (!e || (e->type != LV_E_DOOR && e->type != LV_E_ARCH))
                        continue;
                    int nb = lv_neighbor_room(L, f, x, z, s);
                    if (nb < LV_ROOM_FIRST || c->gr[nb].depth != 255)
                        continue;
                    c->gr[nb].depth = (uint8_t)NH_MIN(254, c->gr[r].depth + 1);
                    c->gr[nb].parent = (int16_t)r;
                    c->gr[nb].entry_door = e->type == LV_E_DOOR ? (int16_t)(e->ref - 1) : -1;
                    queue[qt++] = nb;
                }
            }
        /* Up and down the stairs. */
        if (c->gr[r].role == GR_STAIR) {
            for (int df = -1; df <= 1; df += 2) {
                int nf = f + df;
                if (nf < 0 || nf >= c->floors)
                    continue;
                int nb = c->stair_room[nf];
                if (nb && c->gr[nb].depth == 255) {
                    c->gr[nb].depth = (uint8_t)NH_MIN(254, c->gr[r].depth + 1);
                    c->gr[nb].parent = (int16_t)r;
                    queue[qt++] = nb;
                }
            }
        }
    }
}

bool gen_room_spot(GenCtx *c, int room, NhVec3 *out)
{
    const LvRoom *r = &c->L->rooms[room];
    int f = r->floor;
    for (int tries = 0; tries < 40; tries++) {
        int x = gen_ri(c, r->x0, r->x1), z = gen_ri(c, r->z0, r->z1);
        const LvCell *cell = &c->L->cells[f][z][x];
        if (cell->room != room || cell->stair || (cell->flags & LV_C_HOLE))
            continue;
        NhVec3 p = lv_cell_center(f, x, z);
        /* 1.5 m clear square around the centre (furniture = 1, keep-clear = 2 is fine). */
        bool clear = true;
        int ox = x * OCC_RES, oz = z * OCC_RES;
        for (int k = 0; k < OCC_RES * OCC_RES && clear; k++) {
            int dx = k % OCC_RES, dz = k / OCC_RES;
            if (c->occ[f][oz + dz][ox + dx] == 1 && dx >= 1 && dx <= 2 && dz >= 1 && dz <= 2)
                clear = false;
        }
        if (!clear)
            continue;
        *out = p;
        return true;
    }
    return false;
}

/* ---- doors and locks ---------------------------------------------------------------- */

/* A point on the wall beside a door, on the side facing room `from`. */
static void beside_door(const LvDoor *d, int from, float along, float y, NhVec3 *pos, float *yaw)
{
    /* Door axis 0: on the x boundary at x = d->x (between cells x-1 and x). */
    float bx = d->axis == 0 ? (float)d->x * LV_CELL : (float)d->x * LV_CELL + LV_CELL * 0.5f;
    float bz = d->axis == 0 ? (float)d->z * LV_CELL + LV_CELL * 0.5f : (float)d->z * LV_CELL;
    /* Which side is `from` on? room_a is on the - side. */
    float sgn = d->room_a == from ? -1.0f : 1.0f;
    if (d->axis == 0) {
        *pos = nh_v3(bx + sgn * 0.12f, lv_floor_y(d->floor) + y, bz + along);
        *yaw = sgn < 0 ? NH_PI * 0.5f : -NH_PI * 0.5f;
    } else {
        *pos = nh_v3(bx + along, lv_floor_y(d->floor) + y, bz + sgn * 0.12f);
        *yaw = sgn < 0 ? 0.0f : NH_PI;
    }
}

/* Turn arches on a room's boundary into doors so a lock can't be walked round. */
static void close_arches(GenCtx *c, int room)
{
    Level *L = c->L;
    const LvRoom *r = &L->rooms[room];
    int f = r->floor;
    for (int z = r->z0; z <= r->z1; z++)
        for (int x = r->x0; x <= r->x1; x++)
            for (int s = 0; s < 4; s++) {
                LvEdge *e = lv_edge_side(L, f, x, z, s);
                if (!e || e->type != LV_E_ARCH)
                    continue;
                int nb = lv_neighbor_room(L, f, x, z, s);
                if (nb == room)
                    continue;
                e->type = LV_E_OPEN;
                e->ref = 0;
                lv_add_door(L, f, x, z, s, LV_DOOR_WOOD, LV_LOCK_NONE, 0, 0);
            }
}

static int room_doors(const Level *L, int room, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < L->ndoors && n < max; i++)
        if (L->doors[i].room_a == room || L->doors[i].room_b == room)
            out[n++] = i;
    return n;
}

/* Where to leave a key: a reachable, ordinary room, on a desk if there is one. */
static int place_item(GenCtx *c, const uint8_t *reach, int kind, int key_id, int avoid_room)
{
    Level *L = c->L;
    static const uint8_t pref[] = { RF_SECURITY, RF_OFFICE, RF_EXEC_OFFICE, RF_LOCKER_ROOM, RF_BREAKROOM, RF_STUDY,
                                    RF_BEDROOM, RF_MEETING, RF_OPEN_OFFICE, RF_LAB, RF_KITCHEN, RF_TELLER };
    int cand[LV_ROOMS_MAX], n = 0, npref = 0;
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        if (!reach[r] || r == avoid_room || L->rooms[r].floor >= c->floors || c->gr[r].role == GR_STAIR)
            continue;
        bool p = false;
        for (unsigned k = 0; k < NH_ARRAY_LEN(pref); k++)
            p |= L->rooms[r].func == pref[k];
        if (p)
            cand[npref++] = r;
    }
    n = npref;
    if (!n)
        for (int r = LV_ROOM_FIRST; r < L->nrooms; r++)
            if (reach[r] && r != avoid_room && L->rooms[r].floor < c->floors && c->gr[r].role != GR_STAIR)
                cand[n++] = r;
    if (!n)
        return -1;
    int room = cand[gen_ri(c, 0, n - 1)];
    int type = kind == ENT_KEYCARD ? PROP_KEYCARD : PROP_CODE_NOTE;
    /* On a desk or table in that room, else on the floor at a free spot. */
    int surf = -1;
    for (int i = 0; i < L->nprops; i++) {
        const LvProp *p = &L->props[i];
        if (p->room == room && (p->type == PROP_DESK || p->type == PROP_CONFERENCE_TABLE ||
                                p->type == PROP_COFFEE_TABLE || p->type == PROP_KITCHEN_COUNTER ||
                                p->type == PROP_RECEPTION_DESK) && gen_chance(c, 60))
            surf = i;
    }
    NhVec3 pos;
    float yaw = (float)gen_ri(c, 0, 7) * NH_PI * 0.25f;
    if (surf >= 0) {
        const LvProp *b = &L->props[surf];
        pos = nh_v3(b->x + (float)gen_ri(c, -2, 2) * 0.1f, b->y + nh_prop_info[b->type].half.y * 2.0f,
                    b->z + (float)gen_ri(c, -1, 1) * 0.08f);
    } else if (!gen_room_spot(c, room, &pos)) {
        pos = lv_cell_center(L->rooms[room].floor, L->rooms[room].x0, L->rooms[room].z0);
    }
    int prop = lv_add_prop(L, type, L->rooms[room].floor, pos.x, pos.y, pos.z, yaw, nh_prop_info[type].half,
                           nh_prop_info[type].flags | LV_P_DYNAMIC);
    if (prop >= 0)
        L->props[prop].room = (uint8_t)room;
    int e = gen_entity(c, kind, room, prop, pos, yaw);
    if (e >= 0)
        c->P->ent[e].key_id = (uint8_t)key_id;
    return e;
}

static int place_fuse(GenCtx *c, const uint8_t *reach, int circuit, int avoid_room)
{
    Level *L = c->L;
    int cand[LV_ROOMS_MAX], n = 0;
    for (int pass = 0; pass < 2 && !n; pass++)
        for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
            if (!reach[r] || r == avoid_room || L->rooms[r].floor >= c->floors || c->gr[r].role == GR_STAIR)
                continue;
            int fn = L->rooms[r].func;
            if (pass == 0 && fn != RF_UTILITY && fn != RF_STORAGE && fn != RF_SECURITY && fn != RF_GARAGE)
                continue;
            cand[n++] = r;
        }
    for (int tries = 0; tries < 8 && n; tries++) {
        int room = cand[gen_ri(c, 0, n - 1)];
        const LvRoom *r = &L->rooms[room];
        for (int k = 0; k < 24; k++) {
            int x = gen_ri(c, r->x0, r->x1), z = gen_ri(c, r->z0, r->z1), s = gen_ri(c, 0, 3);
            int p = gen_prop_on_wall(c, PROP_FUSE_BOX, r->floor, x, z, s, 0.4f, 1.3f);
            if (p < 0)
                continue;
            L->props[p].flags |= LV_P_INTERACT | LV_P_DYNAMIC;
            const LvProp *pp = &L->props[p];
            int e = gen_entity(c, ENT_FUSE_BOX, room, p, nh_v3(pp->x, pp->y, pp->z), pp->yaw);
            if (e >= 0)
                c->P->ent[e].key_id = (uint8_t)circuit;
            return e;
        }
    }
    return -1;
}

typedef struct LockJob {
    int16_t room;
    uint8_t lock, level;
} LockJob;

void gen_security(GenCtx *c)
{
    Level *L = c->L;
    NhPlan *P = c->P;
    int sec = c->security;
    /* Exterior doors: picks, harder with security; alarmed at 2+. */
    for (int i = 0; i < L->ndoors; i++) {
        LvDoor *d = &L->doors[i];
        if (!(d->flags & LV_DOOR_F_EXTERIOR))
            continue;
        d->lock = LV_LOCK_PICK;
        d->lock_level = (uint8_t)NH_CLAMP(d->lock_level + (sec >= 3) + (c->type == BT_BANK), 1, 3);
        d->locked = 1;
        if (sec >= 2)
            d->flags |= LV_DOOR_F_ALARMED;
    }
    /* Interior locks by room security. */
    LockJob jobs[64];
    int njobs = 0;
    for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
        if (L->rooms[r].floor >= c->floors)
            continue;
        int s = c->gr[r].secure;
        int fn = L->rooms[r].func;
        uint8_t lock = LV_LOCK_NONE, level = 0;
        if (fn == RF_VAULT) {
            lock = LV_LOCK_VAULT;
            level = 3;
        } else if (fn == RF_PANIC_ROOM) {
            lock = LV_LOCK_POWERED;
            level = 3;
        } else if (s >= 2) {
            lock = gen_chance(c, 55) ? LV_LOCK_KEYCARD : LV_LOCK_CODE;
            level = (uint8_t)(s >= 3 ? 3 : 2);
            if (fn == RF_EXHIBIT && sec >= 3 && gen_chance(c, 40))
                lock = LV_LOCK_POWERED;
        } else if (s == 1) {
            lock = sec >= 2 && gen_chance(c, 50) ? LV_LOCK_KEYCARD : LV_LOCK_PICK;
            level = (uint8_t)(1 + (sec >= 3));
        } else if ((fn == RF_OFFICE || fn == RF_STUDY || fn == RF_STORAGE) && gen_chance(c, 15 + sec * 8)) {
            lock = LV_LOCK_PICK;
            level = 1;
        }
        if (lock == LV_LOCK_NONE)
            continue;
        if (lock != LV_LOCK_PICK)
            close_arches(c, r);
        if (njobs < (int)NH_ARRAY_LEN(jobs))
            jobs[njobs++] = (LockJob){ (int16_t)r, lock, level };
    }
    /* Shallow rooms first so their keys can open the way to deeper ones. */
    for (int i = 0; i < njobs; i++)
        for (int j = i + 1; j < njobs; j++)
            if (c->gr[jobs[j].room].depth < c->gr[jobs[i].room].depth ||
                (c->gr[jobs[j].room].depth == c->gr[jobs[i].room].depth && jobs[j].room < jobs[i].room)) {
                LockJob t = jobs[i];
                jobs[i] = jobs[j];
                jobs[j] = t;
            }
    int next_card = 1, next_code = 1;
    static uint8_t reach[LV_ROOMS_MAX];
    for (int k = 0; k < njobs; k++) {
        LockJob *j = &jobs[k];
        int ds[16];
        int nd = room_doors(L, j->room, ds, 16);
        uint8_t key_id = 0;
        if (j->lock == LV_LOCK_KEYCARD || j->lock == LV_LOCK_VAULT)
            key_id = (uint8_t)NH_MIN(next_card++, 30);
        if (j->lock == LV_LOCK_CODE) {
            key_id = (uint8_t)NH_MIN(next_code++, GEN_CODES - 1);
        }
        if (j->lock == LV_LOCK_VAULT) {
            /* Card and code share the vault's id. */
            if (key_id >= GEN_CODES)
                key_id = GEN_CODES - 1;
        }
        if (j->lock == LV_LOCK_POWERED)
            key_id = P->security_circuit;
        for (int i = 0; i < nd; i++) {
            LvDoor *d = &L->doors[ds[i]];
            if (d->flags & LV_DOOR_F_EXTERIOR)
                continue;
            /* Doors between two locked rooms keep the stronger lock. */
            if (d->lock != LV_LOCK_NONE && d->lock != LV_LOCK_PICK && d->lock_level >= j->level)
                continue;
            d->lock = j->lock;
            d->lock_level = j->level;
            d->key_id = key_id;
            d->locked = 1;
            int fn = L->rooms[j->room].func;
            d->kind = fn == RF_VAULT ? LV_DOOR_VAULT : (j->level >= 2 || fn == RF_SERVER) ? LV_DOOR_METAL : d->kind;
            if (j->lock == LV_LOCK_CODE || j->lock == LV_LOCK_VAULT || j->lock == LV_LOCK_KEYCARD) {
                int other = d->room_a == j->room ? d->room_b : d->room_a;
                NhVec3 pos;
                float yaw;
                beside_door(d, other, 0.78f, 1.3f, &pos, &yaw);
                int p = lv_add_prop(L, PROP_KEYPAD, d->floor, pos.x, pos.y, pos.z, yaw, nh_prop_info[PROP_KEYPAD].half,
                                    nh_prop_info[PROP_KEYPAD].flags | LV_P_DYNAMIC);
                if (p >= 0)
                    L->props[p].room = (uint8_t)other;
                int e = gen_entity(c, ENT_KEYPAD, other, p, pos, yaw);
                if (e >= 0) {
                    P->ent[e].door = (int16_t)ds[i];
                    P->ent[e].key_id = key_id;
                    P->ent[e].tier = j->lock;
                }
            }
        }
        if (j->lock == LV_LOCK_PICK)
            continue;
        /* Place the key somewhere reachable with this lock closed. */
        gen_reach(L, P, reach);
        if (j->lock == LV_LOCK_KEYCARD || j->lock == LV_LOCK_VAULT)
            place_item(c, reach, ENT_KEYCARD, key_id, j->room);
        if (j->lock == LV_LOCK_CODE || j->lock == LV_LOCK_VAULT) {
            if (j->lock == LV_LOCK_VAULT)
                gen_reach(L, P, reach); /* the card may have opened more */
            place_item(c, reach, ENT_CODE, key_id, j->room);
            P->codes[key_id % GEN_CODES] = (uint16_t)gen_ri(c, 1000, 9999);
        }
        if (j->lock == LV_LOCK_POWERED) {
            bool have = false;
            for (int i = 0; i < P->nent; i++)
                have |= P->ent[i].kind == ENT_FUSE_BOX && P->ent[i].key_id == key_id;
            if (!have)
                place_fuse(c, reach, key_id, j->room);
        }
    }
    /* Fuse boxes for the security circuit (if not already) and each floor's lights. */
    gen_reach(L, P, reach);
    bool have_sec = false;
    for (int i = 0; i < P->nent; i++)
        have_sec |= P->ent[i].kind == ENT_FUSE_BOX && P->ent[i].key_id == P->security_circuit;
    if (!have_sec)
        place_fuse(c, reach, P->security_circuit, -1);
    for (int f = 0; f < c->floors; f++) {
        uint8_t fr[LV_ROOMS_MAX];
        memset(fr, 0, sizeof(fr));
        for (int r = LV_ROOM_FIRST; r < L->nrooms; r++)
            fr[r] = reach[r] && L->rooms[r].floor == f;
        place_fuse(c, fr, 1 + f * 2, -1);
    }
    /* Alarm panels: by the front door and in the security room. */
    if (c->front_door >= 0) {
        const LvDoor *d = &L->doors[c->front_door];
        NhVec3 pos;
        float yaw;
        beside_door(d, c->entrance, -0.8f, 1.4f, &pos, &yaw);
        int p = lv_add_prop(L, PROP_ALARM_PANEL, 0, pos.x, pos.y, pos.z, yaw, nh_prop_info[PROP_ALARM_PANEL].half,
                            nh_prop_info[PROP_ALARM_PANEL].flags | LV_P_DYNAMIC);
        if (p >= 0)
            L->props[p].room = (uint8_t)c->entrance;
        int e = gen_entity(c, ENT_ALARM_PANEL, c->entrance, p, pos, yaw);
        if (e >= 0)
            P->ent[e].circuit = P->security_circuit;
    }
    /* Cameras: corners of corridors, the lobby and secure rooms. */
    int ncam = NH_MIN(2 + sec * 2 + c->size * 2, 14);
    int placed = 0;
    for (int tries = 0; tries < 200 && placed < ncam; tries++) {
        int r = gen_ri(c, LV_ROOM_FIRST, L->nrooms - 1);
        const LvRoom *rr = &L->rooms[r];
        if (rr->floor >= c->floors || c->gr[r].role == GR_STAIR)
            continue;
        bool want = c->gr[r].role == GR_CORRIDOR || c->gr[r].role == GR_HALL || r == c->entrance ||
                    c->gr[r].secure >= 1 || rr->func == RF_GALLERY || rr->func == RF_EXHIBIT;
        if (!want)
            continue;
        bool dup = false;
        for (int i = 0; i < P->nent; i++)
            dup |= P->ent[i].kind == ENT_CAMERA && P->ent[i].room == r && gen_area(rr) < 20;
        if (dup)
            continue;
        int corner = gen_ri(c, 0, 3);
        float cx = corner & 1 ? (float)(rr->x1 + 1) * LV_CELL - 0.2f : (float)rr->x0 * LV_CELL + 0.2f;
        float cz = corner & 2 ? (float)(rr->z1 + 1) * LV_CELL - 0.2f : (float)rr->z0 * LV_CELL + 0.2f;
        NhVec3 ctr = nh_v3(((float)rr->x0 + (float)rr->x1 + 1.0f) * LV_CELL * 0.5f, 0,
                           ((float)rr->z0 + (float)rr->z1 + 1.0f) * LV_CELL * 0.5f);
        float yaw = atan2f(-(ctr.x - cx), -(ctr.z - cz));
        NhVec3 pos = nh_v3(cx, lv_floor_y(rr->floor) + LV_CEIL - 0.25f, cz);
        int p = lv_add_prop(L, PROP_SECURITY_CAMERA, rr->floor, pos.x, pos.y, pos.z, yaw,
                            nh_prop_info[PROP_SECURITY_CAMERA].half,
                            nh_prop_info[PROP_SECURITY_CAMERA].flags | LV_P_DYNAMIC);
        if (p >= 0)
            L->props[p].room = (uint8_t)r;
        int e = gen_entity(c, ENT_CAMERA, r, p, pos, yaw);
        if (e >= 0)
            P->ent[e].circuit = P->security_circuit;
        placed++;
    }
    /* The objective: the flagged loot in the most secure room. */
    int best = -1, bs = -1;
    for (int i = 0; i < P->nent; i++) {
        NhEntity *e = &P->ent[i];
        if (e->kind != ENT_LOOT || !(e->flags & ENTF_OBJECTIVE))
            continue;
        int s = c->gr[e->room].secure * 100 + c->gr[e->room].depth * 4 + e->tier;
        if (s > bs) {
            bs = s;
            best = i;
        }
    }
    if (best < 0) {
        /* No set piece supplied one: the most secure room gets a target. */
        int room = -1, rs = -1;
        for (int r = LV_ROOM_FIRST; r < L->nrooms; r++) {
            if (L->rooms[r].floor >= c->floors || c->gr[r].role == GR_STAIR || c->gr[r].role == GR_CORRIDOR)
                continue;
            int s = c->gr[r].secure * 100 + c->gr[r].depth * 4 + L->rooms[r].floor;
            if (s > rs) {
                rs = s;
                room = r;
            }
        }
        static const uint8_t k_target[BT_COUNT] = { PROP_CRATE, PROP_HARD_DRIVE, PROP_GOLD_BARS, PROP_JEWELRY_BOX,
                                                    PROP_JEWELRY_BOX };
        NhVec3 pos;
        if (room >= 0 && gen_room_spot(c, room, &pos))
            best = gen_loot(c, room, k_target[c->type], pos.x, pos.y, pos.z, 4, ENTF_OBJECTIVE);
        if (best < 0 && room >= 0) {
            NhVec3 cpos = lv_cell_center(L->rooms[room].floor, L->rooms[room].x0, L->rooms[room].z0);
            int p = lv_add_prop(L, k_target[c->type], L->rooms[room].floor, cpos.x, cpos.y, cpos.z, 0,
                                nh_prop_info[k_target[c->type]].half, LV_P_DYNAMIC | LV_P_INTERACT);
            if (p >= 0)
                L->props[p].room = (uint8_t)room;
            best = gen_entity(c, ENT_LOOT, room, p, cpos, 0);
            if (best >= 0) {
                P->ent[best].tier = 4;
                P->ent[best].flags = ENTF_OBJECTIVE;
                P->ent[best].value = (uint16_t)gen_ri(c, 2000, 6000);
            }
        }
    }
    for (int i = 0; i < P->nent; i++)
        if (i != best)
            P->ent[i].flags &= (uint8_t)~ENTF_OBJECTIVE;
    P->objective = best;
    /* Top up small loot so every building is worth exploring. */
    int nloot = 0;
    for (int i = 0; i < P->nent; i++)
        nloot += P->ent[i].kind == ENT_LOOT;
    int want = 8 + c->size * 6 + sec;
    static const uint8_t small[] = { PROP_CASH_STACK, PROP_LAPTOP, PROP_WATCH_CASE, PROP_JEWELRY_BOX, PROP_DOCUMENTS };
    for (int tries = 0; tries < 120 && nloot < want; tries++) {
        int r = gen_ri(c, LV_ROOM_FIRST, L->nrooms - 1);
        if (L->rooms[r].floor >= c->floors || c->gr[r].role == GR_STAIR || c->gr[r].role == GR_CORRIDOR)
            continue;
        /* On a surface in that room if one exists. */
        int surf = -1;
        for (int i = 0; i < L->nprops; i++) {
            const LvProp *p = &L->props[i];
            if (p->room == r && (p->type == PROP_DESK || p->type == PROP_COFFEE_TABLE || p->type == PROP_KITCHEN_COUNTER ||
                                 p->type == PROP_BANK_COUNTER || p->type == PROP_CONFERENCE_TABLE))
                surf = i;
        }
        if (surf < 0)
            continue;
        const LvProp *b = &L->props[surf];
        int type = small[gen_ri(c, 0, (int)NH_ARRAY_LEN(small) - 1)];
        int tier = gen_ri(c, 0, 1) + (c->gr[r].secure >= 1);
        if (gen_loot(c, r, type, b->x + (float)gen_ri(c, -2, 2) * 0.12f, b->y + nh_prop_info[b->type].half.y * 2.0f,
                     b->z, tier, 0) >= 0)
            nloot++;
    }
    P->total_loot = 0;
    for (int i = 0; i < P->nent; i++)
        if (P->ent[i].kind == ENT_LOOT)
            P->total_loot += P->ent[i].value;
    /* Entry points: the street in front, the back yard; the van out front. */
    float side = gen_chance(c, 50) ? -1.0f : 1.0f;
    float ex = side < 0 ? (float)c->bx0 * LV_CELL - 3.0f : (float)(c->bx1 + 1) * LV_CELL + 3.0f;
    P->entry[0] = nh_v3(ex, 0.0f, (float)(c->bz0 - 2) * LV_CELL + 1.0f);
    P->entry_yaw[0] = NH_PI;
    P->entry[1] = nh_v3(ex, 0.0f, (float)(c->bz1 + 2) * LV_CELL + 1.0f);
    P->entry_yaw[1] = 0.0f;
    P->nentry = 2;
    NhVec3 van = nh_v3(ex - side * 1.0f, 0.0f, (float)(c->bz0 - 3) * LV_CELL + 1.0f);
    int vp = lv_add_prop(L, PROP_CAR, 0, van.x, 0.0f, van.z, NH_PI * 0.5f, nh_prop_info[PROP_CAR].half,
                         nh_prop_info[PROP_CAR].flags);
    if (vp >= 0) {
        L->props[vp].room = LV_ROOM_OUTSIDE;
        L->props[vp].state = 1; /* the crew's van */
    }
    P->extract[0] = nh_v3(van.x, 0.0f, van.z + 2.2f);
    P->nextract = 1;
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            const LvCell *cl = lv_cell(L, 0, lv_cell_of(P->extract[0].x) + dx, lv_cell_of(P->extract[0].z) + dz);
            if (cl && cl->room == LV_ROOM_OUTSIDE)
                L->cells[0][lv_cell_of(P->extract[0].z) + dz][lv_cell_of(P->extract[0].x) + dx].flags |= LV_C_EXTRACT;
        }
    for (int i = 0; i < P->nentry; i++) {
        const LvCell *cl = lv_cell(L, 0, lv_cell_of(P->entry[i].x), lv_cell_of(P->entry[i].z));
        if (cl)
            L->cells[0][lv_cell_of(P->entry[i].z)][lv_cell_of(P->entry[i].x)].flags |= LV_C_ENTRY;
    }
}

/* ---- guards ------------------------------------------------------------------------ */

static const char *const k_roster[BT_COUNT] = {
    /* warehouse */ "HDLgrSLgv",
    /* office */ "CgSJgrvgrg",
    /* bank */ "CVgSgEgvgr",
    /* mansion */ "HDgLvgrLg",
    /* museum */ "CgvJgEgrvg",
};

static int arch_of(char ch)
{
    switch (ch) {
    case 'g': return ARCH_GUARD;
    case 'r': return ARCH_ROOKIE;
    case 'v': return ARCH_VETERAN;
    case 'L': return ARCH_LAZY;
    case 'H': return ARCH_HANDLER;
    case 'D': return ARCH_DOG;
    case 'C': return ARCH_CAMOP;
    case 'S': return ARCH_SUPERVISOR;
    case 'E': return ARCH_ELITE;
    case 'J': return ARCH_JANITOR;
    default: return ARCH_GUARD;
    }
}

static int action_for(GenCtx *c, int func, float *wait)
{
    switch (func) {
    case RF_BREAKROOM: case RF_KITCHEN: case RF_LOUNGE:
        *wait = (float)gen_ri(c, 6, 12);
        return gen_chance(c, 50) ? ACT_COFFEE : ACT_CHAT;
    case RF_OUTSIDE: case RF_LOADING:
        *wait = (float)gen_ri(c, 8, 14);
        return ACT_SMOKE;
    case RF_CORRIDOR: case RF_STAIRWELL:
        *wait = (float)gen_ri(c, 1, 2);
        return gen_chance(c, 25) ? ACT_RADIO : ACT_NONE;
    default:
        *wait = (float)gen_ri(c, 3, 6);
        return ACT_LOOK;
    }
}

/* Patrol through `n` rooms of one floor (plus its corridor), as a loop. */
static int make_route(GenCtx *c, NhGuardPlan *g, int floor, int n, bool lazy)
{
    Level *L = c->L;
    NhVec3 pts[AI_ROUTE_MAX];
    int rooms[AI_ROUTE_MAX];
    int np = 0;
    /* Always pass through the corridor / hall. */
    int corr = c->corridor[floor];
    NhVec3 p;
    if (corr && gen_room_spot(c, corr, &p)) {
        pts[np] = p;
        rooms[np++] = corr;
    }
    for (int tries = 0; tries < 80 && np < n; tries++) {
        int r = gen_ri(c, LV_ROOM_FIRST, L->nrooms - 1);
        if (L->rooms[r].floor != floor || c->gr[r].role == GR_STAIR || c->gr[r].role == GR_ROOF)
            continue;
        bool dup = false;
        for (int k = 0; k < np; k++)
            dup |= rooms[k] == r;
        if (dup || !gen_room_spot(c, r, &p))
            continue;
        pts[np] = p;
        rooms[np++] = r;
    }
    if (np < 2)
        return 0;
    /* Nearest-neighbour loop. */
    bool used[AI_ROUTE_MAX] = { false };
    int order[AI_ROUTE_MAX], cur = 0;
    used[0] = true;
    order[0] = 0;
    for (int k = 1; k < np; k++) {
        int bi = -1;
        float bd = 1e9f;
        for (int i = 0; i < np; i++) {
            if (used[i])
                continue;
            float d = nh_v3_dist(pts[cur], pts[i]);
            if (d < bd) {
                bd = d;
                bi = i;
            }
        }
        used[bi] = true;
        order[k] = bi;
        cur = bi;
    }
    g->nroute = 0;
    for (int k = 0; k < np; k++) {
        int i = order[k];
        AiPatrolPt *rp = &g->route[g->nroute++];
        rp->pos = pts[i];
        float wait;
        rp->action = (uint8_t)action_for(c, L->rooms[rooms[i]].func, &wait);
        rp->wait = wait;
        rp->yaw = (float)gen_ri(c, 0, 7) * NH_PI * 0.25f;
    }
    if (lazy && g->nroute >= 2) {
        int k = gen_ri(c, 1, g->nroute - 1);
        g->route[k].action = ACT_NAP;
        g->route[k].wait = (float)gen_ri(c, 20, 40);
    }
    g->pos = g->route[0].pos;
    g->yaw = g->route[0].yaw;
    return g->nroute;
}

static void perimeter_route(GenCtx *c, NhGuardPlan *g)
{
    float x0 = (float)c->bx0 * LV_CELL - 2.2f, x1 = (float)(c->bx1 + 1) * LV_CELL + 2.2f;
    float z0 = (float)c->bz0 * LV_CELL - 2.2f, z1 = (float)(c->bz1 + 1) * LV_CELL + 2.2f;
    NhVec3 corners[4] = { nh_v3(x0, 0, z1), nh_v3(x1, 0, z1), nh_v3(x1, 0, z0), nh_v3(x0, 0, z0) };
    int start = gen_ri(c, 0, 3);
    g->nroute = 4;
    for (int k = 0; k < 4; k++) {
        AiPatrolPt *rp = &g->route[k];
        rp->pos = corners[(start + k) & 3];
        rp->action = (uint8_t)(k == 1 ? ACT_SMOKE : ACT_LOOK);
        rp->wait = k == 1 ? 8.0f : 2.5f;
        rp->yaw = (float)k * NH_PI * 0.5f;
    }
    g->pos = g->route[0].pos;
    g->yaw = 0.0f;
}

void gen_guards(GenCtx *c)
{
    NhPlan *P = c->P;
    Level *L = c->L;
    static const int base[BS_COUNT] = { 3, 5, 7 };
    int count = base[c->size] + c->security / 2 + c->gp->extra_guards;
    count = NH_MIN(count, GEN_GUARD_MAX);
    const char *roster = k_roster[c->type];
    int rl = (int)strlen(roster);
    int floor_rr = 0;
    P->nguards = 0;
    for (int i = 0; P->nguards < count && i < 32; i++) {
        char ch = i < rl ? roster[i] : (gen_chance(c, 60) ? 'g' : 'r');
        int arch = arch_of(ch);
        if (arch == ARCH_ELITE && c->security < 3 && c->size < BS_LARGE)
            arch = ARCH_VETERAN;
        if (arch == ARCH_DOG)
            continue; /* added with its handler */
        NhGuardPlan *g = &P->guards[P->nguards];
        memset(g, 0, sizeof(*g));
        g->arch = (uint8_t)arch;
        g->partner = -1;
        bool ok = false;
        if (arch == ARCH_HANDLER) {
            if (P->nguards + 2 > count)
                continue;
            perimeter_route(c, g);
            ok = true;
            P->nguards++;
            NhGuardPlan *d = &P->guards[P->nguards];
            memset(d, 0, sizeof(*d));
            d->arch = ARCH_DOG;
            d->partner = (int8_t)(P->nguards - 1);
            g->partner = (int8_t)P->nguards;
            d->pos = nh_v3(g->pos.x + 1.0f, 0.0f, g->pos.z);
            d->yaw = g->yaw;
            P->nguards++;
            continue;
        }
        if (arch == ARCH_CAMOP || arch == ARCH_SUPERVISOR) {
            /* At the security console (camera op) or standing in the room. */
            for (int e = 0; e < P->nent && !ok; e++) {
                const NhEntity *en = &P->ent[e];
                if (en->kind != ENT_CONSOLE)
                    continue;
                NhVec3 back = nh_dir_from_yaw_pitch(en->yaw, 0.0f);
                NhVec3 pos = nh_v3(en->pos.x + back.x * 0.85f, lv_floor_y(en->floor), en->pos.z + back.z * 0.85f);
                if (arch == ARCH_SUPERVISOR)
                    pos = nh_v3(en->pos.x + back.x * 1.8f, lv_floor_y(en->floor), en->pos.z + back.z * 1.8f);
                g->pos = pos;
                g->yaw = en->yaw + NH_PI;
                ok = true;
            }
            if (!ok)
                arch = g->arch = ARCH_GUARD;
            else {
                P->nguards++;
                continue;
            }
        }
        if (arch == ARCH_VETERAN && P->objective >= 0 && gen_chance(c, 45)) {
            /* Posted outside the objective room's door. */
            int room = P->ent[P->objective].room;
            int door = c->gr[room].entry_door;
            if (door >= 0) {
                const LvDoor *d = &L->doors[door];
                int other = d->room_a == room ? d->room_b : d->room_a;
                NhVec3 pos;
                float yaw;
                float sgn = d->room_a == other ? -1.0f : 1.0f;
                float bx = d->axis == 0 ? (float)d->x * LV_CELL : (float)d->x * LV_CELL + 1.0f;
                float bz = d->axis == 0 ? (float)d->z * LV_CELL + 1.0f : (float)d->z * LV_CELL;
                pos = d->axis == 0 ? nh_v3(bx + sgn * 1.3f, lv_floor_y(d->floor), bz + 0.9f)
                                   : nh_v3(bx + 0.9f, lv_floor_y(d->floor), bz + sgn * 1.3f);
                yaw = d->axis == 0 ? (sgn < 0 ? -NH_PI * 0.5f : NH_PI * 0.5f) : (sgn < 0 ? NH_PI : 0.0f);
                (void)beside_door;
                if (lv_room_at(L, d->floor, lv_cell_of(pos.x), lv_cell_of(pos.z)) == other) {
                    g->pos = pos;
                    g->yaw = yaw;
                    P->nguards++;
                    continue;
                }
            }
        }
        /* Patrols spread over the floors, ground first. */
        int floor = floor_rr % c->floors;
        floor_rr++;
        int n = gen_ri(c, 3, NH_MIN(6, AI_ROUTE_MAX));
        if (make_route(c, g, floor, n, arch == ARCH_LAZY))
            P->nguards++;
    }
}
