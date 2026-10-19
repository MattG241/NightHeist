/* Night Heist - resources: materials, textures, models and animations loaded
 * by name from the data directory (data/tex/NAME.ntx, data/mdl/NAME.nmd,
 * data/anim/NAME.nan). Everything here lives for the whole session. */
#ifndef NH_RES_H
#define NH_RES_H

#include "core/nh_assets.h"
#include "platform/gfx.h"

#define RES_MAT_MAX 96
#define RES_MODEL_MAX 96
#define RES_ANIM_MAX 48

typedef struct ResModel {
    char name[NH_NAME_LEN];
    NhModel model;
    GfxMesh parts[NH_MODEL_PARTS_MAX];
    int part_mat[NH_MODEL_PARTS_MAX];
    bool loaded;
} ResModel;

bool res_init(NhArena *persist, NhArena *scratch);
/* Read a whole data file into arena memory (32-byte aligned). */
void *res_read(const char *rel_path, size_t *len, NhArena *arena);

GfxTexture res_texture(const char *name);
/* Material id for a texture name (created on first use); 0 = missing. */
int res_material(const char *name);
const GfxMaterial *res_mat(int id);
GfxMaterial *res_mat_mut(int id);
const char *res_mat_name(int id);

const ResModel *res_model(const char *name);
const NhAnimClip *res_anim(const char *name);

/* Memory used by resources (persistent arena), for the debug overlay. */
size_t res_bytes(void);

#endif
