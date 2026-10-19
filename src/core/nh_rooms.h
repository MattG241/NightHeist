/* Night Heist - surface materials and room functions shared by the level
 * generator (core) and the renderer (game). */
#ifndef NH_ROOMS_H
#define NH_ROOMS_H

#include "nh_level.h"

typedef enum NhMat {
    MAT_NONE,
    MAT_CONCRETE, MAT_GARAGE, MAT_PLASTER, MAT_PLASTER_BEIGE, MAT_STUCCO, MAT_CEILING, MAT_BRICK,
    MAT_BRICK_WHITE, MAT_PARQUET, MAT_PLANKS, MAT_TILES, MAT_MARBLE, MAT_MARBLE_TILES, MAT_GRANITE,
    MAT_LINOLEUM, MAT_CARPET, MAT_WALLPAPER, MAT_METAL, MAT_CORRUGATED, MAT_RUST, MAT_SLATS, MAT_WOOD,
    MAT_VENEER, MAT_PANEL, MAT_ASPHALT, MAT_PAVEMENT, MAT_GRASS, MAT_FACADE, MAT_ROOF,
    MAT_COUNT
} NhMat;

extern const char *const nh_mat_names[MAT_COUNT];

typedef enum NhRoomFunc {
    RF_NONE, RF_OUTSIDE, RF_CORRIDOR, RF_LOBBY, RF_OFFICE, RF_OPEN_OFFICE, RF_MEETING, RF_EXEC_OFFICE,
    RF_BATHROOM, RF_KITCHEN, RF_BREAKROOM, RF_STORAGE, RF_WAREHOUSE, RF_LOADING, RF_SECURITY, RF_SERVER,
    RF_VAULT, RF_BANK_HALL, RF_TELLER, RF_LIVING, RF_DINING, RF_BEDROOM, RF_LIBRARY, RF_STUDY, RF_GALLERY,
    RF_EXHIBIT, RF_WINE_CELLAR, RF_GARAGE, RF_STAIRWELL, RF_LOCKER_ROOM, RF_PENTHOUSE, RF_PANIC_ROOM,
    RF_ROOF, RF_BALCONY, RF_UTILITY, RF_ARCHIVE, RF_LOUNGE, RF_SAFEHOUSE, RF_SHOP, RF_LAB,
    RF_COUNT
} NhRoomFunc;

typedef struct NhRoomStyle {
    const char *name;
    uint8_t floor, wall, ceil; /* NhMat */
    uint8_t surface;           /* LvSurface for footsteps */
    uint8_t lit_chance;        /* % chance the lights are on at night */
} NhRoomStyle;

extern const NhRoomStyle nh_room_styles[RF_COUNT];

/* Apply a function's default materials and footstep surface to a room. */
void nh_room_apply_style(Level *L, int room, int func);

#endif
