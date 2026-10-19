/* Night Heist - the hand-built greybox test building (Phases 1-3).
 * Built with the same builder API the procedural generator uses. */
#include "testlevel.h"

#include <math.h>

#include "core/nh_proptab.h"
#include "core/nh_rooms.h"

/* Place a prop by name at metres (x, z) on floor f, yaw in quarter turns. */
static int P(Level *L, int type, int f, float x, float z, float yaw, int extra_flags)
{
    const NhPropInfo *pi = &nh_prop_info[type];
    return lv_add_prop(L, type, f, x, lv_floor_y(f), z, yaw, pi->half, pi->flags | extra_flags);
}

static int room(Level *L, int f, int x0, int z0, int x1, int z1, int func, int flags, NhColor tint)
{
    int r = lv_add_room(L, f, x0, z0, x1, z1, func, flags);
    nh_room_apply_style(L, r, func);
    L->rooms[r].wall_tint = tint;
    return r;
}

static void ceiling_light(Level *L, int f, float x, float z, int r, int circuit, NhColor c, float radius, bool on)
{
    int i = lv_add_light(L, nh_v3(x, lv_floor_y(f) + LV_CEIL - 0.05f, z), c, radius, LV_LIGHT_CEILING, r, circuit);
    if (i >= 0)
        L->lights[i].on = on;
    P(L, PROP_CEILING_LAMP, f, x, z, 0, 0);
    L->props[L->nprops - 1].y = lv_floor_y(f) + LV_CEIL;
}

/* Moonlight through a window: a dim, cold spot shining into the room. */
static void moon(Level *L, int f, float x, float z, NhVec3 dir, int r)
{
    int i = lv_add_light(L, nh_v3(x, lv_floor_y(f) + 2.4f, z), nh_rgba(70, 90, 140, 255), 7.0f, LV_LIGHT_WINDOW,
                         r, 15);
    if (i >= 0) {
        L->lights[i].dir = nh_v3_norm(dir);
        L->lights[i].cone = cosf(NH_DEG2RAD(60.0f));
        L->lights[i].emergency = 1;
    }
}

void testlevel_build(Level *L)
{
    lv_init(L, 20, 16, 2, 0x7E57u);
    NhColor white = nh_rgba(235, 235, 230, 255), blue = nh_rgba(200, 212, 230, 255),
            warm = nh_rgba(240, 225, 200, 255), green = nh_rgba(205, 225, 205, 255);
    NhColor lamp = nh_rgba(215, 196, 160, 255), cold = nh_rgba(165, 180, 205, 255);
    L->rooms[LV_ROOM_OUTSIDE].floor_mat = MAT_ASPHALT;
    L->rooms[LV_ROOM_OUTSIDE].wall_mat = MAT_BRICK;
    L->rooms[LV_ROOM_OUTSIDE].func = RF_OUTSIDE;

    /* ---- ground floor ---- */
    int lobby = room(L, 0, 3, 3, 8, 7, RF_LOBBY, LV_R_PUBLIC, warm);
    int corr = room(L, 0, 9, 3, 10, 12, RF_CORRIDOR, LV_R_CORRIDOR, white);
    int offA = room(L, 0, 11, 3, 16, 6, RF_OFFICE, 0, blue);
    int offB = room(L, 0, 11, 7, 16, 9, RF_OPEN_OFFICE, 0, green);
    int serv = room(L, 0, 11, 10, 16, 12, RF_SERVER, LV_R_SECURE, white);
    int stair = room(L, 0, 3, 8, 5, 12, RF_STAIRWELL, LV_R_STAIRWELL, white);
    int stor = room(L, 0, 6, 8, 8, 12, RF_STORAGE, 0, white);
    lv_add_stair(L, 0, 4, 9, 1);

    /* ---- first floor ---- */
    room(L, 1, 3, 8, 5, 12, RF_STAIRWELL, LV_R_STAIRWELL, white); /* landing */
    int hall = room(L, 1, 6, 8, 10, 12, RF_LOUNGE, 0, warm);
    int exec = room(L, 1, 3, 3, 8, 7, RF_EXEC_OFFICE, LV_R_SECURE, warm);
    int corr1 = room(L, 1, 9, 3, 10, 7, RF_CORRIDOR, LV_R_CORRIDOR, white);
    int meet = room(L, 1, 11, 3, 16, 7, RF_MEETING, 0, warm);
    int arch = room(L, 1, 11, 8, 16, 12, RF_ARCHIVE, 0, white);
    /* re-flag the stair holes now that the landing room exists */
    for (int k = 0; k < LV_STAIR_LEN; k++)
        L->cells[1][9 + k][4].flags |= LV_C_HOLE;

    /* ---- roof ---- */
    int roof = room(L, 2, 3, 3, 16, 12, RF_ROOF, LV_R_OUTDOOR | LV_R_ROOF, white);
    L->rooms[roof].floor_mat = MAT_ROOF;

    /* Doors (cell, side: 0 +X, 1 +Z, 2 -X, 3 -Z) */
    lv_add_door(L, 0, 5, 3, 3, LV_DOOR_GLASS, LV_LOCK_PICK, 1, 0);       /* front entrance */
    lv_add_door(L, 0, 8, 5, 0, LV_DOOR_WOOD, LV_LOCK_NONE, 0, 0);        /* lobby - corridor */
    lv_add_door(L, 0, 10, 4, 0, LV_DOOR_WOOD, LV_LOCK_PICK, 1, 0);       /* office A */
    lv_add_door(L, 0, 10, 8, 0, LV_DOOR_WOOD, LV_LOCK_NONE, 0, 0);       /* office B */
    lv_add_door(L, 0, 10, 11, 0, LV_DOOR_METAL, LV_LOCK_KEYCARD, 1, 1);  /* server room */
    lv_add_door(L, 0, 9, 10, 2, LV_DOOR_WOOD, LV_LOCK_NONE, 0, 0);       /* storage */
    lv_set_edge(L, 0, 4, 8, 3, LV_E_ARCH);                               /* stairwell - lobby */
    lv_add_door(L, 0, 10, 12, 1, LV_DOOR_METAL, LV_LOCK_PICK, 2, 0);     /* rear exit */
    lv_set_edge(L, 1, 5, 10, 0, LV_E_ARCH);                              /* landing - hall */
    lv_add_door(L, 1, 9, 8, 3, LV_DOOR_WOOD, LV_LOCK_NONE, 0, 0);        /* hall - corridor */
    lv_add_door(L, 1, 9, 5, 2, LV_DOOR_WOOD, LV_LOCK_PICK, 2, 0);        /* executive office */
    lv_add_door(L, 1, 10, 5, 0, LV_DOOR_GLASS, LV_LOCK_NONE, 0, 0);      /* meeting room */
    lv_add_door(L, 1, 10, 10, 0, LV_DOOR_METAL, LV_LOCK_CODE, 1, 2);     /* archive */

    /* Windows */
    int win0[][3] = { { 3, 3, 3 }, { 4, 3, 3 }, { 7, 3, 3 }, { 8, 3, 3 }, { 12, 3, 3 }, { 14, 3, 3 },
                      { 16, 4, 0 }, { 16, 5, 0 }, { 16, 8, 0 }, { 3, 5, 2 }, { 16, 11, 0 } };
    for (unsigned i = 0; i < NH_ARRAY_LEN(win0); i++)
        lv_add_window(L, 0, win0[i][0], win0[i][1], win0[i][2]);
    int win1[][3] = { { 4, 3, 3 }, { 6, 3, 3 }, { 12, 3, 3 }, { 15, 3, 3 }, { 16, 5, 0 }, { 16, 9, 0 },
                      { 16, 11, 0 }, { 3, 4, 2 }, { 13, 12, 1 }, { 7, 12, 1 } };
    for (unsigned i = 0; i < NH_ARRAY_LEN(win1); i++)
        lv_add_window(L, 1, win1[i][0], win1[i][1], win1[i][2]);

    /* Vent from the storage room to the open office, and a roof hatch. */
    for (int x = 7; x <= 12; x++)
        L->cells[0][9][x].flags |= LV_C_VENT;
    L->cells[0][9][7].flags |= LV_C_GRATE;
    L->cells[0][9][12].flags |= LV_C_GRATE;
    L->cells[1][10][7].flags |= LV_C_HATCH;

    lv_finalize(L);

    /* ---- lights ---- */
    ceiling_light(L, 0, 9, 9, lobby, 1, lamp, 9, true);
    ceiling_light(L, 0, 15, 11, lobby, 1, lamp, 9, true);
    ceiling_light(L, 0, 20, 9, corr, 1, cold, 8, true);
    ceiling_light(L, 0, 20, 19, corr, 1, cold, 8, false);
    ceiling_light(L, 0, 28, 10, offA, 1, cold, 9, false);
    ceiling_light(L, 0, 28, 17, offB, 1, cold, 9, true);
    ceiling_light(L, 0, 28, 23, serv, 2, nh_rgba(110, 150, 220, 255), 9, true);
    ceiling_light(L, 0, 8, 20, stair, 3, lamp, 8, true);
    L->lights[L->nlights - 1].emergency = 1;
    ceiling_light(L, 0, 15, 21, stor, 1, lamp, 7, false);
    ceiling_light(L, 1, 16, 20, hall, 1, lamp, 9, true);
    ceiling_light(L, 1, 11, 10, exec, 1, lamp, 8, false);
    ceiling_light(L, 1, 28, 10, meet, 1, lamp, 9, false);
    ceiling_light(L, 1, 28, 20, arch, 1, cold, 9, false);
    ceiling_light(L, 1, 20, 10, corr1, 1, cold, 7, true);
    int lamp_i = lv_add_light(L, nh_v3(10.0f, lv_floor_y(1) + 1.6f, 13.0f), nh_rgba(255, 200, 140, 255), 5.0f,
                              LV_LIGHT_LAMP, exec, 4);
    (void)lamp_i;
    /* Exit sign over the rear door, always on. */
    int sign = lv_add_light(L, nh_v3(21.0f, 2.5f, 25.6f), nh_rgba(40, 200, 80, 255), 3.0f, LV_LIGHT_SIGN, corr, 0);
    L->lights[sign].emergency = 1;
    /* Street lights and moonlight. */
    for (int i = 0; i < 3; i++) {
        float x = 6.0f + i * 13.0f;
        int sl = lv_add_light(L, nh_v3(x, 4.8f, 3.4f), nh_rgba(230, 170, 100, 255), 16.0f, LV_LIGHT_STREET,
                              LV_ROOM_OUTSIDE, 15);
        L->lights[sl].emergency = 1;
        P(L, PROP_STREETLIGHT, 0, x, 2.6f, NH_PI, 0);
    }
    moon(L, 0, 9, 7, nh_v3(0, -0.6f, 1), lobby);
    moon(L, 0, 31, 9, nh_v3(-1, -0.6f, 0.2f), offA);
    moon(L, 0, 26, 7, nh_v3(0, -0.6f, 1), offA);
    moon(L, 1, 9, 7, nh_v3(0.3f, -0.6f, 1), exec);
    moon(L, 1, 28, 7, nh_v3(0, -0.6f, 1), meet);

    /* ---- props (metres) ---- */
    P(L, PROP_RECEPTION_DESK, 0, 12, 12, 0, 0);
    P(L, PROP_OFFICE_CHAIR, 0, 12, 13.2f, NH_PI, 0);
    P(L, PROP_PLANT, 0, 7, 7, 0, 0);
    P(L, PROP_PLANT, 0, 17, 7, 0, 0);
    P(L, PROP_SOFA, 0, 7.2f, 11, -NH_PI * 0.5f, 0);
    P(L, PROP_COFFEE_TABLE, 0, 8.6f, 11, NH_PI * 0.5f, 0);
    P(L, PROP_WATER_COOLER, 0, 17, 15, 0, 0);
    P(L, PROP_PAINTING, 0, 12, 15.85f, 0, 0);
    L->props[L->nprops - 1].y = 1.3f;
    L->props[L->nprops - 1].state = 2;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            float x = 25.5f + i * 5.0f, z = 8.5f + j * 3.5f;
            P(L, PROP_DESK, 0, x, z, 0, 0);
            P(L, PROP_OFFICE_CHAIR, 0, x, z + 0.8f, NH_PI, 0);
            int c = P(L, PROP_COMPUTER, 0, x - 0.3f, z - 0.1f, 0, 0);
            L->props[c].y = 0.76f;
        }
    P(L, PROP_FILING_CABINET, 0, 33.4f, 7.2f, NH_PI * 0.5f, 0);
    P(L, PROP_FILING_CABINET, 0, 33.4f, 8.0f, NH_PI * 0.5f, 0);
    P(L, PROP_BOOKSHELF, 0, 22.4f, 12.4f, -NH_PI * 0.5f, 0);
    for (int i = 0; i < 4; i++) {
        float x = 24.5f + i * 2.6f;
        P(L, PROP_DESK, 0, x, 16.0f, 0, 0);
        P(L, PROP_OFFICE_CHAIR, 0, x, 16.8f, NH_PI, 0);
        int c = P(L, PROP_COMPUTER, 0, x, 15.9f, 0, 0);
        L->props[c].y = 0.76f;
    }
    P(L, PROP_COFFEE_MACHINE, 0, 33.2f, 19.0f, 0, 0);
    P(L, PROP_PLANT, 0, 23.0f, 19.0f, 0, 0);
    for (int i = 0; i < 4; i++) {
        P(L, PROP_SERVER_RACK, 0, 25.0f + i * 2.0f, 22.0f, NH_PI, 0);
        P(L, PROP_SERVER_RACK, 0, 25.0f + i * 2.0f, 24.6f, 0, 0);
    }
    P(L, PROP_TERMINAL, 0, 33.4f, 21.0f, NH_PI * 0.5f, 0);
    P(L, PROP_PALLET_RACK, 0, 16.8f, 18.8f, NH_PI * 0.5f, 0);
    P(L, PROP_CRATE, 0, 13.0f, 24.3f, 0.3f, 0);
    P(L, PROP_CRATE_STACK, 0, 15.6f, 23.6f, 0, 0);
    P(L, PROP_BARREL, 0, 12.7f, 21.5f, 0, 0);
    P(L, PROP_FUSE_BOX, 0, 12.2f, 17.5f, -NH_PI * 0.5f, 0);
    L->props[L->nprops - 1].y = 1.2f;
    P(L, PROP_VENT_GRATE, 0, 15.0f, 19.0f, 0, 0);
    L->props[L->nprops - 1].y = LV_CEIL - 0.02f;
    P(L, PROP_VENT_GRATE, 0, 25.0f, 19.0f, 0, 0);
    L->props[L->nprops - 1].y = LV_CEIL - 0.02f;
    /* Upstairs */
    P(L, PROP_DESK, 1, 10.0f, 11.0f, 0, 0);
    P(L, PROP_OFFICE_CHAIR, 1, 10.0f, 11.8f, NH_PI, 0);
    P(L, PROP_LAMP_FLOOR, 1, 7.0f, 14.6f, 0, 0);
    P(L, PROP_BOOKSHELF, 1, 6.4f, 9.0f, -NH_PI * 0.5f, 0);
    P(L, PROP_BOOKSHELF, 1, 6.4f, 11.0f, -NH_PI * 0.5f, 0);
    P(L, PROP_SAFE, 1, 17.4f, 14.5f, NH_PI * 0.5f, 0);
    P(L, PROP_ARMCHAIR, 1, 14.0f, 8.0f, 0.6f, 0);
    P(L, PROP_CONFERENCE_TABLE, 1, 28.0f, 10.0f, 0, 0);
    for (int i = 0; i < 4; i++) {
        P(L, PROP_OFFICE_CHAIR, 1, 26.5f + i * 1.0f, 8.7f, 0, 0);
        P(L, PROP_OFFICE_CHAIR, 1, 26.5f + i * 1.0f, 11.3f, NH_PI, 0);
    }
    for (int i = 0; i < 4; i++)
        P(L, PROP_BOOKSHELF, 1, 24.5f + i * 2.5f, 22.5f, 0, 0);
    P(L, PROP_FILING_CABINET, 1, 33.4f, 18.0f, NH_PI * 0.5f, 0);
    P(L, PROP_SOFA, 1, 15.0f, 25.3f, 0, 0);
    P(L, PROP_PLANT, 1, 19.5f, 17.0f, 0, 0);
    P(L, PROP_ARMCHAIR, 1, 13.5f, 20.0f, 2.2f, 0);
    /* Outside */
    P(L, PROP_CAR, 0, 30.0f, 1.8f, NH_PI * 0.5f, 0);
    P(L, PROP_DUMPSTER, 0, 24.0f, 27.0f, 0, 0);
    P(L, PROP_BENCH, 0, 13.0f, 4.6f, 0, 0);
    P(L, PROP_LADDER, 0, 33.0f, 26.1f, NH_PI, 0);
    lv_index_props(L);
}

NhVec3 testlevel_start(float *yaw)
{
    *yaw = NH_PI; /* facing +Z, toward the front door */
    return nh_v3(11.0f, 0.0f, 2.0f);
}
