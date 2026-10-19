/* Night Heist - runtime asset formats (written by tools/nhconv.py).
 *
 * All files are big-endian so the Wii reads them natively; the parsers
 * assemble bytes explicitly and work on any host.
 *
 * .ntx  texture:   'NTX1' u16 w, u16 h, u8 fmt, u8 levels, u8 wrap, u8 filter,
 *                  u32 size, pad to 32, GX tiled data (all mips)
 * .nmd  model:     'NMD1' u16 flags, u16 nbones, u16 nparts, u16 pad,
 *                  f32 bmin[3], f32 bmax[3], pad to 48
 *                  bones[nbones] (64 B): name[32], s16 parent, u16 pad,
 *                                        f32 pos[3], f32 rot[4]   (bind-pose local)
 *                  parts[nparts] (64 B): material[32], u32 vcount, u32 icount,
 *                                        u8 nslots, u8 slots[10], pad
 *                  then per part: vertices (14 B each, NhVertex layout),
 *                  pad 4, colours (4 B each, if flags & 1), indices (u16), pad 4
 *                  A part is one draw: one material, at most 10 bones.
 * .nan  animation: 'NAN1' u16 nbones, u16 nframes, f32 fps, u8 loop, pad 3,
 *                  name[32], then nframes x nbones x (s16 quat[4] / 32767,
 *                  s16 pos[3] / 256) */
#ifndef NH_ASSETS_H
#define NH_ASSETS_H

#include "nh_mesh.h"
#include "nh_texconv.h"

#define NH_SKEL_MAX 24
#define NH_MODEL_PARTS_MAX 12
#define NH_NAME_LEN 32

typedef struct NhTexInfo {
    int width, height, levels;
    NhTexFormat format;
    bool repeat, filter;
    const uint8_t *data; /* points into the file buffer (32-byte aligned if the buffer is) */
    size_t size;
} NhTexInfo;

typedef struct NhBone {
    char name[NH_NAME_LEN];
    int parent;           /* -1 for the root */
    NhVec3 pos;           /* bind-pose local transform */
    NhQuat rot;
    NhMat34 inv_bind;     /* inverse of the bind-pose global transform */
} NhBone;

typedef struct NhModelPart {
    char material[NH_NAME_LEN];
    NhMesh mesh;          /* vertex bone fields are slot indices into slots[] */
    uint8_t nslots;
    uint8_t slots[NH_MAX_BONES]; /* skeleton bone for each matrix slot */
} NhModelPart;

typedef struct NhModel {
    NhBone bones[NH_SKEL_MAX];
    int nbones;
    NhModelPart parts[NH_MODEL_PARTS_MAX];
    int nparts;
    NhVec3 bmin, bmax;
    bool skinned;
} NhModel;

typedef struct NhAnimClip {
    char name[NH_NAME_LEN];
    int nbones, nframes;
    float fps;
    bool loop;
    const uint8_t *frames; /* raw big-endian frame data inside the file buffer */
} NhAnimClip;

bool nh_ntx_parse(const void *data, size_t len, NhTexInfo *out);
/* Copies geometry into arena memory; the file buffer can be freed afterwards. */
bool nh_nmd_parse(const void *data, size_t len, NhArena *arena, NhModel *out);
int nh_model_find_bone(const NhModel *m, const char *name);
/* Keeps pointers into the file buffer: keep it alive while the clip is used. */
bool nh_nan_parse(const void *data, size_t len, NhAnimClip *out);

/* ---- animation ------------------------------------------------------------ */

typedef struct NhPose {
    NhQuat rot[NH_SKEL_MAX];
    NhVec3 pos[NH_SKEL_MAX];
} NhPose;

void nh_pose_bind(const NhModel *m, NhPose *out);
/* Sample a clip at time t (seconds; wraps if looping, clamps otherwise). */
void nh_anim_sample(const NhAnimClip *clip, float t, NhPose *out, int nbones);
float nh_anim_duration(const NhAnimClip *clip);
/* out = a blended toward b by w (per bone nlerp / lerp). */
void nh_pose_blend(const NhPose *a, const NhPose *b, float w, int nbones, NhPose *out);
/* Skinning matrices for every skeleton bone: world * global(pose) * inv_bind. */
void nh_pose_skin(const NhModel *m, const NhPose *pose, const NhMat34 *world, NhMat34 *out_bones);
/* Global (model-space) transform of one bone for attaching things (torch, radio). */
void nh_pose_bone_global(const NhModel *m, const NhPose *pose, int bone, NhMat34 *out);

#endif
