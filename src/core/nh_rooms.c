#include "nh_rooms.h"

const char *const nh_mat_names[MAT_COUNT] = {
    "", "concrete", "garage", "plaster", "plaster_beige", "stucco", "ceiling", "brick",
    "brick_white", "parquet", "planks", "tiles", "marble", "marble_tiles", "granite",
    "linoleum", "carpet", "wallpaper", "metal", "corrugated", "rust", "slats", "wood",
    "veneer", "panel", "asphalt", "pavement", "grass", "facade", "roof",
};

#define S(n, fl, wa, ce, su, lit) { n, fl, wa, ce, su, lit }

const NhRoomStyle nh_room_styles[RF_COUNT] = {
    [RF_NONE] = S("room", MAT_CONCRETE, MAT_PLASTER, MAT_CEILING, LV_SURF_CONCRETE, 20),
    [RF_OUTSIDE] = S("street", MAT_ASPHALT, MAT_FACADE, MAT_NONE, LV_SURF_ASPHALT, 100),
    [RF_CORRIDOR] = S("corridor", MAT_LINOLEUM, MAT_PLASTER, MAT_CEILING, LV_SURF_TILE, 55),
    [RF_LOBBY] = S("lobby", MAT_MARBLE, MAT_PLASTER_BEIGE, MAT_CEILING, LV_SURF_MARBLE, 70),
    [RF_OFFICE] = S("office", MAT_CARPET, MAT_PLASTER, MAT_CEILING, LV_SURF_CARPET, 20),
    [RF_OPEN_OFFICE] = S("open office", MAT_CARPET, MAT_PLASTER, MAT_CEILING, LV_SURF_CARPET, 30),
    [RF_MEETING] = S("meeting room", MAT_CARPET, MAT_PANEL, MAT_CEILING, LV_SURF_CARPET, 15),
    [RF_EXEC_OFFICE] = S("executive office", MAT_PARQUET, MAT_PANEL, MAT_STUCCO, LV_SURF_WOOD, 25),
    [RF_BATHROOM] = S("bathroom", MAT_TILES, MAT_TILES, MAT_CEILING, LV_SURF_TILE, 30),
    [RF_KITCHEN] = S("kitchen", MAT_TILES, MAT_PLASTER_BEIGE, MAT_CEILING, LV_SURF_TILE, 40),
    [RF_BREAKROOM] = S("break room", MAT_LINOLEUM, MAT_PLASTER_BEIGE, MAT_CEILING, LV_SURF_TILE, 50),
    [RF_STORAGE] = S("storage", MAT_CONCRETE, MAT_BRICK_WHITE, MAT_CONCRETE, LV_SURF_CONCRETE, 10),
    [RF_WAREHOUSE] = S("warehouse floor", MAT_GARAGE, MAT_CORRUGATED, MAT_METAL, LV_SURF_CONCRETE, 45),
    [RF_LOADING] = S("loading bay", MAT_GARAGE, MAT_BRICK, MAT_METAL, LV_SURF_CONCRETE, 60),
    [RF_SECURITY] = S("security room", MAT_LINOLEUM, MAT_PLASTER, MAT_CEILING, LV_SURF_TILE, 100),
    [RF_SERVER] = S("server room", MAT_METAL, MAT_PLASTER, MAT_CEILING, LV_SURF_METAL, 60),
    [RF_VAULT] = S("vault", MAT_METAL, MAT_METAL, MAT_METAL, LV_SURF_METAL, 80),
    [RF_BANK_HALL] = S("banking hall", MAT_MARBLE_TILES, MAT_PLASTER_BEIGE, MAT_STUCCO, LV_SURF_MARBLE, 50),
    [RF_TELLER] = S("teller area", MAT_GRANITE, MAT_PANEL, MAT_CEILING, LV_SURF_MARBLE, 30),
    [RF_LIVING] = S("living room", MAT_PARQUET, MAT_WALLPAPER, MAT_STUCCO, LV_SURF_WOOD, 35),
    [RF_DINING] = S("dining room", MAT_PARQUET, MAT_WALLPAPER, MAT_STUCCO, LV_SURF_WOOD, 25),
    [RF_BEDROOM] = S("bedroom", MAT_CARPET, MAT_WALLPAPER, MAT_STUCCO, LV_SURF_CARPET, 15),
    [RF_LIBRARY] = S("library", MAT_PARQUET, MAT_PANEL, MAT_STUCCO, LV_SURF_WOOD, 25),
    [RF_STUDY] = S("study", MAT_PARQUET, MAT_PANEL, MAT_STUCCO, LV_SURF_WOOD, 20),
    [RF_GALLERY] = S("gallery", MAT_MARBLE, MAT_STUCCO, MAT_STUCCO, LV_SURF_MARBLE, 65),
    [RF_EXHIBIT] = S("exhibit hall", MAT_MARBLE_TILES, MAT_PLASTER, MAT_STUCCO, LV_SURF_MARBLE, 60),
    [RF_WINE_CELLAR] = S("wine cellar", MAT_PLANKS, MAT_BRICK, MAT_BRICK, LV_SURF_WOOD, 20),
    [RF_GARAGE] = S("garage", MAT_GARAGE, MAT_BRICK_WHITE, MAT_CONCRETE, LV_SURF_CONCRETE, 40),
    [RF_STAIRWELL] = S("stairwell", MAT_CONCRETE, MAT_PLASTER, MAT_CEILING, LV_SURF_CONCRETE, 80),
    [RF_LOCKER_ROOM] = S("locker room", MAT_TILES, MAT_PLASTER, MAT_CEILING, LV_SURF_TILE, 40),
    [RF_PENTHOUSE] = S("penthouse", MAT_MARBLE, MAT_STUCCO, MAT_STUCCO, LV_SURF_MARBLE, 40),
    [RF_PANIC_ROOM] = S("panic room", MAT_METAL, MAT_METAL, MAT_METAL, LV_SURF_METAL, 100),
    [RF_ROOF] = S("roof", MAT_ROOF, MAT_CONCRETE, MAT_NONE, LV_SURF_CONCRETE, 100),
    [RF_BALCONY] = S("balcony", MAT_PAVEMENT, MAT_STUCCO, MAT_NONE, LV_SURF_CONCRETE, 100),
    [RF_UTILITY] = S("utility room", MAT_CONCRETE, MAT_BRICK_WHITE, MAT_CONCRETE, LV_SURF_CONCRETE, 30),
    [RF_ARCHIVE] = S("archive", MAT_LINOLEUM, MAT_PLASTER, MAT_CEILING, LV_SURF_TILE, 10),
    [RF_LOUNGE] = S("lounge", MAT_CARPET, MAT_PANEL, MAT_STUCCO, LV_SURF_CARPET, 40),
    [RF_SAFEHOUSE] = S("safehouse", MAT_PLANKS, MAT_BRICK, MAT_CONCRETE, LV_SURF_WOOD, 100),
};

void nh_room_apply_style(Level *L, int room, int func)
{
    if (room <= 0 || room >= L->nrooms || func < 0 || func >= RF_COUNT)
        return;
    LvRoom *r = &L->rooms[room];
    const NhRoomStyle *st = &nh_room_styles[func];
    r->func = (uint8_t)func;
    r->floor_mat = st->floor;
    r->wall_mat = st->wall;
    r->ceil_mat = st->ceil;
    for (int z = r->z0; z <= r->z1; z++)
        for (int x = r->x0; x <= r->x1; x++)
            if (L->cells[r->floor][z][x].room == room)
                L->cells[r->floor][z][x].surface = st->surface;
}
