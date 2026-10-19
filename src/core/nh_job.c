#include "nh_job.h"

#include <stdio.h>
#include <string.h>

#include "nh_proptab.h"
#include "nh_rng.h"

const NhGadgetInfo nh_gadgets[GD_COUNT] = {
    [GD_LOCKPICK] = { "Lockpicks", "Pick locks", 255, 0, 1.5f },
    [GD_COIN] = { "Coins", "Throw: a small noise", 6, 1, 14.0f },
    [GD_NOISEMAKER] = { "Noisemaker", "Throw: loud decoy", 2, 1, 14.0f },
    [GD_SMOKE] = { "Smoke", "Throw: blocks sight", 2, 1, 12.0f },
    [GD_TASER] = { "Taser", "Knock out up close", 2, 0, 2.5f },
    [GD_DART] = { "Dart gun", "Silent, slow knockout", 3, 0, 15.0f },
    [GD_JAMMER] = { "Jammer", "Blind a camera", 3, 0, 18.0f },
    [GD_EMP] = { "EMP", "Throw: electronics off", 1, 1, 12.0f },
    [GD_CUTTER] = { "Glass cutter", "Cut a window", 3, 0, 1.5f },
    [GD_CLONER] = { "Card cloner", "Copy a keycard", 2, 0, 1.6f },
    [GD_THERMAL] = { "Thermal", "See through walls", 255, 0, 0.0f },
    [GD_MIRROR] = { "Laser mirror", "Bend a laser", 2, 0, 2.0f },
    [GD_DRILL] = { "Safe drill", "Drill a safe (loud)", 1, 0, 1.5f },
    [GD_DIALER] = { "Phone dialer", "Ring a desk phone", 3, 0, 40.0f },
};

const char *const nh_obj_names[OBJ_COUNT] = {
    "Steal", "Hack", "Safe", "Photograph", "Bug", "Wipe", "Loot", "Ghost", "No alarm", "No takedowns", "Speed",
};

static NhObjective *add_obj(NhJob *j, int type, bool optional, int ent, uint32_t target, uint32_t bonus)
{
    if (j->nobj >= JOB_OBJ_MAX)
        return NULL;
    NhObjective *o = &j->obj[j->nobj++];
    memset(o, 0, sizeof(*o));
    o->type = (uint8_t)type;
    o->optional = optional;
    o->ent = (int16_t)ent;
    o->target = target;
    o->bonus = bonus;
    return o;
}

const char *job_loot_name(const Level *L, const NhPlan *p, int ent)
{
    if (ent < 0 || ent >= p->nent || p->ent[ent].prop < 0)
        return "the target";
    switch (L->props[p->ent[ent].prop].type) {
    case PROP_GOLD_BARS: return "the gold";
    case PROP_JEWELRY_BOX: return "the jewels";
    case PROP_HARD_DRIVE: return "the hard drive";
    case PROP_DOCUMENTS: return "the documents";
    case PROP_PAINTING: return "the painting";
    case PROP_STATUE: return "the statue";
    case PROP_CRATE: return "the shipment";
    case PROP_CASH_STACK: return "the cash";
    case PROP_WATCH_CASE: return "the watches";
    case PROP_LAPTOP: return "the laptop";
    default: return "the target";
    }
}

void job_init(NhJob *j, const Level *L, const NhPlan *plan, uint32_t seed)
{
    memset(j, 0, sizeof(*j));
    NhRng rng;
    nh_rng_seed(&rng, seed, 0x70Bu);
    for (int g = 0; g < GD_COUNT; g++)
        j->charges[g] = nh_gadgets[g].charges;
    j->bag_cap = 12;
    j->holding = j->carrying = -1;
    j->police_eta = -1.0f;
    j->police_delay = 75.0f;
    /* Main objective. */
    int main_ent = plan->objective;
    NhObjective *o = add_obj(j, OBJ_STEAL, false, main_ent, 0, 0);
    if (o)
        snprintf(o->text, sizeof(o->text), "Steal %s", job_loot_name(L, plan, main_ent));
    /* Optional ones that fit the building. */
    int cand[GEN_ENT_MAX], nc = 0;
    for (int i = 0; i < plan->nent && nc < GEN_ENT_MAX; i++) {
        const NhEntity *e = &plan->ent[i];
        if (i == main_ent)
            continue;
        if (e->kind == ENT_TERMINAL || e->kind == ENT_SAFE || e->kind == ENT_PHONE ||
            (e->kind == ENT_LOOT && e->value > 0 && plan->ent[i].tier >= 2))
            cand[nc++] = i;
    }
    int nopt = 1 + (int)nh_rng_range(&rng, 2);
    for (int k = 0; k < nopt && nc > 0; k++) {
        int pick = (int)nh_rng_range(&rng, (uint32_t)nc);
        int ent = cand[pick];
        cand[pick] = cand[--nc];
        const NhEntity *e = &plan->ent[ent];
        NhObjective *q = NULL;
        switch (e->kind) {
        case ENT_TERMINAL:
            if (nh_rng_range(&rng, 2)) {
                q = add_obj(j, OBJ_HACK, true, ent, 0, 400);
                if (q)
                    snprintf(q->text, sizeof(q->text), "Download the files (floor %d)", e->floor);
            } else {
                q = add_obj(j, OBJ_WIPE, true, ent, 0, 350);
                if (q)
                    snprintf(q->text, sizeof(q->text), "Wipe the server logs (floor %d)", e->floor);
            }
            break;
        case ENT_SAFE:
            q = add_obj(j, OBJ_SAFE, true, ent, 0, 300);
            if (q)
                snprintf(q->text, sizeof(q->text), "Open the safe (floor %d)", e->floor);
            break;
        case ENT_PHONE:
            q = add_obj(j, OBJ_BUG, true, ent, 0, 250);
            if (q)
                snprintf(q->text, sizeof(q->text), "Bug the phone (floor %d)", e->floor);
            break;
        default:
            q = add_obj(j, OBJ_PHOTO, true, ent, 0, 200);
            if (q)
                snprintf(q->text, sizeof(q->text), "Photograph the piece (floor %d)", e->floor);
            break;
        }
    }
    /* A style objective. */
    int style = (int)nh_rng_range(&rng, 5);
    NhObjective *s = NULL;
    switch (style) {
    case 0:
        s = add_obj(j, OBJ_GHOST, true, -1, 0, 600);
        if (s)
            snprintf(s->text, sizeof(s->text), "Ghost: never be spotted");
        break;
    case 1:
        s = add_obj(j, OBJ_NO_ALARM, true, -1, 0, 300);
        if (s)
            snprintf(s->text, sizeof(s->text), "Keep the alarm quiet");
        break;
    case 2:
        s = add_obj(j, OBJ_NO_KO, true, -1, 0, 300);
        if (s)
            snprintf(s->text, sizeof(s->text), "No takedowns");
        break;
    case 3: {
        uint32_t want = (uint32_t)(plan->total_loot / 3);
        s = add_obj(j, OBJ_LOOT, true, -1, want, 250);
        if (s)
            snprintf(s->text, sizeof(s->text), "Bag at least $%u", (unsigned)want * 10u);
        break;
    }
    default:
        s = add_obj(j, OBJ_TIME, true, -1, 480, 300);
        if (s)
            snprintf(s->text, sizeof(s->text), "Out within 8 minutes");
        break;
    }
}

bool job_taken(const NhJob *j, int ent)
{
    return ent >= 0 && ent < GEN_ENT_MAX && ((j->taken[ent >> 3] >> (ent & 7)) & 1u);
}

void job_mark_taken(NhJob *j, int ent)
{
    if (ent >= 0 && ent < GEN_ENT_MAX)
        j->taken[ent >> 3] |= (uint8_t)(1u << (ent & 7));
}

void job_tick(NhJob *j, float dt)
{
    if (j->outcome != JOB_RUNNING)
        return;
    j->time += dt;
    if (j->police_eta > 0.0f) {
        j->police_eta -= dt;
        if (j->police_eta <= 0.0f) {
            j->police_eta = 0.0f;
            j->police_here = true;
        }
    }
    for (int i = 0; i < j->nobj; i++) {
        NhObjective *o = &j->obj[i];
        if (o->type == OBJ_TIME && !o->done && j->time > (float)o->target)
            o->failed = 1;
    }
}

static void complete(NhJob *j, int type, int ent)
{
    for (int i = 0; i < j->nobj; i++) {
        NhObjective *o = &j->obj[i];
        if (o->type == type && !o->failed && (o->ent < 0 || o->ent == ent))
            o->done = 1;
    }
}

static void fail(NhJob *j, int type)
{
    for (int i = 0; i < j->nobj; i++)
        if (j->obj[i].type == type && !j->obj[i].done)
            j->obj[i].failed = 1;
}

void job_event(NhJob *j, const NhPlan *plan, NhJobEvent ev, int ent, int value)
{
    (void)plan;
    if (j->outcome != JOB_RUNNING)
        return;
    switch (ev) {
    case JE_LOOT:
        job_mark_taken(j, ent);
        j->bag_value += value;
        j->bag_items++;
        complete(j, OBJ_STEAL, ent);
        for (int i = 0; i < j->nobj; i++)
            if (j->obj[i].type == OBJ_LOOT && (uint32_t)j->bag_value >= j->obj[i].target)
                j->obj[i].done = 1;
        break;
    case JE_HACKED: j->hacks++; complete(j, OBJ_HACK, ent); break;
    case JE_WIPED: j->hacks++; complete(j, OBJ_WIPE, ent); break;
    case JE_SAFE_OPEN: j->safes++; complete(j, OBJ_SAFE, ent); break;
    case JE_PHOTO: complete(j, OBJ_PHOTO, ent); break;
    case JE_BUG: complete(j, OBJ_BUG, ent); break;
    case JE_SPOTTED: j->spotted++; fail(j, OBJ_GHOST); break;
    case JE_CAMERA_SPOTTED: j->camera_spots++; fail(j, OBJ_GHOST); break;
    case JE_TAKEDOWN: j->takedowns++; fail(j, OBJ_NO_KO); break;
    case JE_BODY_FOUND: j->bodies_found++; break;
    case JE_ALARM:
        if (!j->alarm) {
            j->alarm = true;
            j->alarms++;
            j->police_eta = j->police_delay;
        }
        fail(j, OBJ_NO_ALARM);
        fail(j, OBJ_GHOST);
        break;
    case JE_CAUGHT:
        j->outcome = JOB_BUSTED;
        break;
    case JE_EXTRACT:
        /* Conduct objectives only count when the job itself was done. */
        for (int i = 0; i < j->nobj && job_main_done(j); i++) {
            NhObjective *o = &j->obj[i];
            /* Conduct objectives are met by finishing with them intact. */
            if (!o->failed && (o->type == OBJ_GHOST || o->type == OBJ_NO_ALARM || o->type == OBJ_NO_KO ||
                               o->type == OBJ_TIME))
                o->done = 1;
        }
        j->outcome = job_main_done(j) ? JOB_SUCCESS : JOB_PARTIAL;
        break;
    case JE_ABORT:
        j->outcome = JOB_ABORTED;
        break;
    }
}

bool job_main_done(const NhJob *j)
{
    for (int i = 0; i < j->nobj; i++)
        if (!j->obj[i].optional && !j->obj[i].done)
            return false;
    return true;
}

void job_result(const NhJob *j, NhJobResult *r)
{
    memset(r, 0, sizeof(*r));
    r->outcome = j->outcome;
    r->objectives_total = j->nobj;
    for (int i = 0; i < j->nobj; i++)
        if (j->obj[i].done) {
            r->objectives_done++;
            r->bonus += (int)j->obj[i].bonus;
        }
    r->loot = j->bag_value;
    /* Stealth: start clean, lose points for every slip. */
    int s = 100 - 12 * j->spotted - 8 * j->camera_spots - 25 * j->alarms - 10 * j->bodies_found - 4 * j->takedowns;
    r->ghost = j->spotted == 0 && j->camera_spots == 0 && j->alarms == 0;
    if (r->ghost && j->takedowns == 0)
        s += 10;
    r->stealth = NH_CLAMP(s, 0, 100);
    if (j->outcome == JOB_BUSTED) {
        r->grade = 'F';
        r->loot = 0;
        r->bonus = 0;
        r->payout = 0;
        r->heat = 6 + 2 * j->alarms;
        return;
    }
    if (j->outcome == JOB_ABORTED) {
        r->bonus = 0;
        r->loot = j->bag_value / 2; /* left in a hurry */
    }
    int pts = r->stealth;
    if (j->outcome != JOB_SUCCESS)
        pts -= 25;
    pts += r->objectives_done * 3;
    r->grade = pts >= 100 ? 'S' : pts >= 82 ? 'A' : pts >= 62 ? 'B' : pts >= 40 ? 'C' : 'D';
    /* Better grades pay a little more (the fence trusts clean work). */
    int mult = r->grade == 'S' ? 120 : r->grade == 'A' ? 110 : r->grade == 'B' ? 100 : r->grade == 'C' ? 90 : 80;
    r->payout = (r->loot + r->bonus) * mult / 100;
    r->heat = 1 + 3 * j->alarms + NH_MIN(3, j->spotted + j->camera_spots) + 2 * j->bodies_found;
    if (r->ghost)
        r->heat = 0;
}
