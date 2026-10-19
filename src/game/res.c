#include "res.h"

#include <stdio.h>
#include <string.h>

#include "platform/platform.h"

typedef struct ResTex {
    char name[NH_NAME_LEN];
    GfxTexture tex;
} ResTex;

typedef struct ResMat {
    char name[NH_NAME_LEN];
    GfxMaterial gfx;
} ResMat;

typedef struct ResAnim {
    NhAnimClip clip;
} ResAnim;

static NhArena *s_persist, *s_scratch;
static ResTex s_tex[GFX_MAX_TEXTURES];
static int s_ntex;
static ResMat s_mat[RES_MAT_MAX];
static int s_nmat = 1; /* 0 = invalid */
static ResModel s_models[RES_MODEL_MAX];
static int s_nmodels;
static ResAnim s_anims[RES_ANIM_MAX];
static int s_nanims;
static bool s_anims_scanned;

bool res_init(NhArena *persist, NhArena *scratch)
{
    s_persist = persist;
    s_scratch = scratch;
    return true;
}

size_t res_bytes(void)
{
    return s_persist ? s_persist->used : 0;
}

#define FILE_CAP (2u * 1024u * 1024u)

void *res_read(const char *rel, size_t *len, NhArena *arena)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/data/%s", plat_data_dir(), rel);
    size_t mark = nh_arena_mark(arena);
    uint8_t *buf = nh_arena_alloc(arena, FILE_CAP);
    if (!buf)
        return NULL;
    long n = plat_file_read(path, buf, FILE_CAP);
    if (n <= 0 || (size_t)n >= FILE_CAP) {
        nh_arena_reset_to(arena, mark);
        return NULL;
    }
    /* Give back the unused tail of the read buffer. */
    nh_arena_reset_to(arena, mark);
    buf = nh_arena_alloc(arena, (size_t)n);
    *len = (size_t)n;
    return buf;
}

GfxTexture res_texture(const char *name)
{
    for (int i = 0; i < s_ntex; i++)
        if (strcmp(s_tex[i].name, name) == 0)
            return s_tex[i].tex;
    if (s_ntex >= GFX_MAX_TEXTURES)
        return 0;
    char rel[96];
    snprintf(rel, sizeof(rel), "tex/%s.ntx", name);
    size_t mark = nh_arena_mark(s_scratch), len = 0;
    void *data = res_read(rel, &len, s_scratch);
    GfxTexture t = 0;
    NhTexInfo ti;
    if (data && nh_ntx_parse(data, len, &ti)) {
        GfxTextureDesc d = { ti.format, (uint16_t)ti.width, (uint16_t)ti.height, (uint8_t)ti.levels,
                             ti.repeat, ti.filter, ti.data };
        t = gfx_texture_create(&d);
    } else {
        fprintf(stderr, "res: texture '%s' missing or invalid\n", name);
    }
    nh_arena_reset_to(s_scratch, mark);
    ResTex *rt = &s_tex[s_ntex++];
    snprintf(rt->name, sizeof(rt->name), "%s", name);
    rt->tex = t;
    return t;
}

static uint16_t default_flags(const char *name)
{
    if (strcmp(name, "glass") == 0)
        return GFX_MAT_BLEND | GFX_MAT_NOCULL | GFX_MAT_BAKED;
    if (strcmp(name, "glow_panel") == 0 || strcmp(name, "screen") == 0 || strcmp(name, "sign_exit") == 0)
        return GFX_MAT_UNLIT;
    return GFX_MAT_BAKED;
}

int res_material(const char *name)
{
    for (int i = 1; i < s_nmat; i++)
        if (strcmp(s_mat[i].name, name) == 0)
            return i;
    if (s_nmat >= RES_MAT_MAX)
        return 0;
    int id = s_nmat++;
    ResMat *m = &s_mat[id];
    snprintf(m->name, sizeof(m->name), "%s", name);
    m->gfx.tex = res_texture(name);
    m->gfx.tint = nh_rgba(255, 255, 255, 255);
    m->gfx.ambient = nh_rgba(40, 40, 48, 255);
    m->gfx.flags = default_flags(name);
    if (strcmp(name, "glass") == 0)
        m->gfx.tint = nh_rgba(120, 140, 160, 150);
    return id;
}

const GfxMaterial *res_mat(int id)
{
    return &s_mat[(id > 0 && id < s_nmat) ? id : 0].gfx;
}

GfxMaterial *res_mat_mut(int id)
{
    return &s_mat[(id > 0 && id < s_nmat) ? id : 0].gfx;
}

const char *res_mat_name(int id)
{
    return (id > 0 && id < s_nmat) ? s_mat[id].name : "";
}

const ResModel *res_model(const char *name)
{
    for (int i = 0; i < s_nmodels; i++)
        if (strcmp(s_models[i].name, name) == 0)
            return s_models[i].loaded ? &s_models[i] : NULL;
    if (s_nmodels >= RES_MODEL_MAX)
        return NULL;
    ResModel *rm = &s_models[s_nmodels++];
    memset(rm, 0, sizeof(*rm));
    snprintf(rm->name, sizeof(rm->name), "%s", name);
    char rel[96];
    snprintf(rel, sizeof(rel), "mdl/%s.nmd", name);
    size_t mark = nh_arena_mark(s_scratch), len = 0;
    void *data = res_read(rel, &len, s_scratch);
    if (data && nh_nmd_parse(data, len, s_persist, &rm->model)) {
        for (int p = 0; p < rm->model.nparts; p++) {
            rm->parts[p] = gfx_mesh_create(&rm->model.parts[p].mesh, false);
            rm->part_mat[p] = res_material(rm->model.parts[p].material);
        }
        rm->loaded = true;
    } else {
        fprintf(stderr, "res: model '%s' missing or invalid\n", name);
    }
    nh_arena_reset_to(s_scratch, mark);
    return rm->loaded ? rm : NULL;
}

static const char *k_clip_names[] = { "walk", "walk_torch", "run", "search", "idle", "look_around", "suspicious",
                                      "alert", "radio", "smoke", "chat", "grab", "knockout", "dragged", "nap",
                                      "dog_walk", "dog_run", "dog_idle", "dog_sniff", "dog_bark", "dog_sleep" };

const NhAnimClip *res_anim(const char *name)
{
    if (!s_anims_scanned) {
        s_anims_scanned = true;
        for (size_t i = 0; i < NH_ARRAY_LEN(k_clip_names) && s_nanims < RES_ANIM_MAX; i++) {
            char rel[96];
            snprintf(rel, sizeof(rel), "anim/%s.nan", k_clip_names[i]);
            size_t len = 0;
            size_t mark = nh_arena_mark(s_persist);
            void *data = res_read(rel, &len, s_persist);
            if (data && nh_nan_parse(data, len, &s_anims[s_nanims].clip))
                s_nanims++;
            else
                nh_arena_reset_to(s_persist, mark);
        }
    }
    for (int i = 0; i < s_nanims; i++)
        if (strcmp(s_anims[i].clip.name, name) == 0)
            return &s_anims[i].clip;
    return NULL;
}
