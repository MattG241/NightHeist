#include "nh_light.h"

#include <math.h>

void nh_light_init(NhLight *l, NhVec3 pos, NhColor color)
{
    l->pos = pos;
    l->dir = nh_v3(0.0f, 0.0f, -1.0f);
    l->color = color;
    l->a[0] = 1.0f;
    l->a[1] = 0.0f;
    l->a[2] = 0.0f;
    l->k[0] = 1.0f;
    l->k[1] = 0.0f;
    l->k[2] = 0.0f;
}

void nh_light_set_spot(NhLight *l, NhVec3 dir, float cutoff_deg, NhSpotFn fn)
{
    l->dir = nh_v3_norm(dir);
    if (cutoff_deg <= 0.0f || cutoff_deg > 90.0f)
        fn = NH_SPOT_OFF;

    float cr = cosf(NH_DEG2RAD(cutoff_deg));
    float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;
    switch (fn) {
    case NH_SPOT_FLAT:
        a0 = -1000.0f * cr;
        a1 = 1000.0f;
        a2 = 0.0f;
        break;
    case NH_SPOT_COS:
        a0 = -cr / (1.0f - cr);
        a1 = 1.0f / (1.0f - cr);
        a2 = 0.0f;
        break;
    case NH_SPOT_COS2:
        a0 = 0.0f;
        a1 = -cr / (1.0f - cr);
        a2 = 1.0f / (1.0f - cr);
        break;
    case NH_SPOT_SHARP: {
        float d = (1.0f - cr) * (1.0f - cr);
        a0 = (cr * (cr - 2.0f)) / d;
        a1 = 2.0f / d;
        a2 = -1.0f / d;
        break;
    }
    case NH_SPOT_OFF:
    default:
        break;
    }
    l->a[0] = a0;
    l->a[1] = a1;
    l->a[2] = a2;
}

void nh_light_set_dist(NhLight *l, float ref_dist, float ref_bright, NhDistFn fn)
{
    if (ref_dist < 0.0f || ref_bright <= 0.0f || ref_bright >= 1.0f)
        fn = NH_DIST_OFF;

    float k0 = 1.0f, k1 = 0.0f, k2 = 0.0f;
    switch (fn) {
    case NH_DIST_GENTLE:
        k1 = (1.0f - ref_bright) / (ref_bright * ref_dist);
        break;
    case NH_DIST_MEDIUM:
        k1 = 0.5f * (1.0f - ref_bright) / (ref_bright * ref_dist);
        k2 = 0.5f * (1.0f - ref_bright) / (ref_bright * ref_dist * ref_dist);
        break;
    case NH_DIST_STEEP:
        k2 = (1.0f - ref_bright) / (ref_bright * ref_dist * ref_dist);
        break;
    case NH_DIST_OFF:
    default:
        break;
    }
    l->k[0] = k0;
    l->k[1] = k1;
    l->k[2] = k2;
}

float nh_light_factor(const NhLight *l, NhVec3 p, NhVec3 n)
{
    NhVec3 to_light = nh_v3_sub(l->pos, p);
    float d = nh_v3_len(to_light);
    NhVec3 L = d > 1e-6f ? nh_v3_scale(to_light, 1.0f / d) : nh_v3(0.0f, 1.0f, 0.0f);

    float diff = 1.0f;
    if (n.x != 0.0f || n.y != 0.0f || n.z != 0.0f)
        diff = fmaxf(nh_v3_dot(n, L), 0.0f);

    float c = fmaxf(-nh_v3_dot(L, l->dir), 0.0f);
    float ang = fmaxf(l->a[0] + l->a[1] * c + l->a[2] * c * c, 0.0f);
    float dist = l->k[0] + l->k[1] * d + l->k[2] * d * d;
    if (dist <= 1e-6f)
        dist = 1e-6f;
    return diff * ang / dist;
}
