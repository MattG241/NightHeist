#include "nh_math.h"

#include <math.h>
#include <string.h>

float nh_v3_len(NhVec3 a)
{
    return sqrtf(nh_v3_dot(a, a));
}

NhVec3 nh_v3_norm(NhVec3 a)
{
    float l = nh_v3_len(a);
    if (l <= 1e-12f)
        return nh_v3(0.0f, 0.0f, 0.0f);
    return nh_v3_scale(a, 1.0f / l);
}

float nh_wrap_angle(float a)
{
    const float two_pi = 2.0f * NH_PI;
    a = fmodf(a + NH_PI, two_pi);
    if (a < 0.0f)
        a += two_pi;
    return a - NH_PI;
}

void nh_m34_identity(NhMat34 *out)
{
    memset(out, 0, sizeof(*out));
    out->m[0][0] = out->m[1][1] = out->m[2][2] = 1.0f;
}

void nh_m34_mul(NhMat34 *out, const NhMat34 *a, const NhMat34 *b)
{
    NhMat34 r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            float v = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
            if (j == 3)
                v += a->m[i][3];
            r.m[i][j] = v;
        }
    }
    *out = r;
}

void nh_m34_translate(NhMat34 *out, float x, float y, float z)
{
    nh_m34_identity(out);
    out->m[0][3] = x;
    out->m[1][3] = y;
    out->m[2][3] = z;
}

void nh_m34_scale(NhMat34 *out, float x, float y, float z)
{
    nh_m34_identity(out);
    out->m[0][0] = x;
    out->m[1][1] = y;
    out->m[2][2] = z;
}

void nh_m34_rot_x(NhMat34 *out, float rad)
{
    float s = sinf(rad), c = cosf(rad);
    nh_m34_identity(out);
    out->m[1][1] = c;
    out->m[1][2] = -s;
    out->m[2][1] = s;
    out->m[2][2] = c;
}

void nh_m34_rot_y(NhMat34 *out, float rad)
{
    float s = sinf(rad), c = cosf(rad);
    nh_m34_identity(out);
    out->m[0][0] = c;
    out->m[0][2] = s;
    out->m[2][0] = -s;
    out->m[2][2] = c;
}

void nh_m34_rot_z(NhMat34 *out, float rad)
{
    float s = sinf(rad), c = cosf(rad);
    nh_m34_identity(out);
    out->m[0][0] = c;
    out->m[0][1] = -s;
    out->m[1][0] = s;
    out->m[1][1] = c;
}

void nh_m34_look_at(NhMat34 *out, NhVec3 eye, NhVec3 up, NhVec3 target)
{
    NhVec3 look = nh_v3_norm(nh_v3_sub(eye, target)); /* camera +Z points back at the eye */
    NhVec3 right = nh_v3_norm(nh_v3_cross(up, look));
    NhVec3 vup = nh_v3_cross(look, right);

    out->m[0][0] = right.x;
    out->m[0][1] = right.y;
    out->m[0][2] = right.z;
    out->m[0][3] = -nh_v3_dot(eye, right);
    out->m[1][0] = vup.x;
    out->m[1][1] = vup.y;
    out->m[1][2] = vup.z;
    out->m[1][3] = -nh_v3_dot(eye, vup);
    out->m[2][0] = look.x;
    out->m[2][1] = look.y;
    out->m[2][2] = look.z;
    out->m[2][3] = -nh_v3_dot(eye, look);
}

bool nh_m34_inverse(NhMat34 *out, const NhMat34 *in)
{
    const float(*m)[4] = in->m;
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabsf(det) < 1e-12f)
        return false;
    float inv = 1.0f / det;
    NhMat34 r;
    r.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * inv;
    r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv;
    r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv;
    r.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * inv;
    r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv;
    r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv;
    r.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * inv;
    r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv;
    r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv;
    for (int i = 0; i < 3; i++)
        r.m[i][3] = -(r.m[i][0] * m[0][3] + r.m[i][1] * m[1][3] + r.m[i][2] * m[2][3]);
    *out = r;
    return true;
}

bool nh_m34_normal_matrix(NhMat34 *out, const NhMat34 *in)
{
    NhMat34 inv;
    if (!nh_m34_inverse(&inv, in))
        return false;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            out->m[i][j] = inv.m[j][i];
        out->m[i][3] = 0.0f;
    }
    return true;
}

NhVec3 nh_m34_mul_point(const NhMat34 *m, NhVec3 p)
{
    return nh_v3(m->m[0][0] * p.x + m->m[0][1] * p.y + m->m[0][2] * p.z + m->m[0][3],
                 m->m[1][0] * p.x + m->m[1][1] * p.y + m->m[1][2] * p.z + m->m[1][3],
                 m->m[2][0] * p.x + m->m[2][1] * p.y + m->m[2][2] * p.z + m->m[2][3]);
}

NhVec3 nh_m34_mul_dir(const NhMat34 *m, NhVec3 d)
{
    return nh_v3(m->m[0][0] * d.x + m->m[0][1] * d.y + m->m[0][2] * d.z,
                 m->m[1][0] * d.x + m->m[1][1] * d.y + m->m[1][2] * d.z,
                 m->m[2][0] * d.x + m->m[2][1] * d.y + m->m[2][2] * d.z);
}

NhVec3 nh_m34_get_pos(const NhMat34 *m)
{
    return nh_v3(m->m[0][3], m->m[1][3], m->m[2][3]);
}

void nh_m34_from_yaw_pitch(NhMat34 *out, NhVec3 pos, float yaw, float pitch)
{
    NhMat34 ry, rx, t;
    nh_m34_rot_y(&ry, yaw);
    nh_m34_rot_x(&rx, pitch);
    nh_m34_translate(&t, pos.x, pos.y, pos.z);
    nh_m34_mul(out, &ry, &rx);
    nh_m34_mul(out, &t, out);
}

NhVec3 nh_dir_from_yaw_pitch(float yaw, float pitch)
{
    float cp = cosf(pitch);
    return nh_v3(-sinf(yaw) * cp, sinf(pitch), -cosf(yaw) * cp);
}

float nh_v2_len(NhVec2 a)
{
    return sqrtf(a.x * a.x + a.y * a.y);
}

float nh_v3_dist(NhVec3 a, NhVec3 b)
{
    return nh_v3_len(nh_v3_sub(a, b));
}

float nh_v3_dist_xz(NhVec3 a, NhVec3 b)
{
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

float nh_approach(float cur, float target, float max_step)
{
    if (cur < target)
        return cur + max_step < target ? cur + max_step : target;
    return cur - max_step > target ? cur - max_step : target;
}

float nh_angle_approach(float cur, float target, float max_step)
{
    float d = nh_wrap_angle(target - cur);
    if (d > max_step)
        d = max_step;
    else if (d < -max_step)
        d = -max_step;
    return nh_wrap_angle(cur + d);
}

float nh_smoothstep(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

NhQuat nh_quat_axis_angle(NhVec3 axis, float rad)
{
    NhVec3 a = nh_v3_norm(axis);
    float s = sinf(rad * 0.5f);
    NhQuat q = { a.x * s, a.y * s, a.z * s, cosf(rad * 0.5f) };
    return q;
}

NhQuat nh_quat_mul(NhQuat a, NhQuat b)
{
    NhQuat q = { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                 a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
    return q;
}

NhQuat nh_quat_norm(NhQuat q)
{
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-12f)
        return nh_quat_identity();
    float inv = 1.0f / l;
    NhQuat r = { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    return r;
}

NhQuat nh_quat_nlerp(NhQuat a, NhQuat b, float t)
{
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float s = d < 0.0f ? -1.0f : 1.0f;
    NhQuat r = { a.x + (b.x * s - a.x) * t, a.y + (b.y * s - a.y) * t, a.z + (b.z * s - a.z) * t,
                 a.w + (b.w * s - a.w) * t };
    return nh_quat_norm(r);
}

NhQuat nh_quat_euler(float pitch_x, float yaw_y, float roll_z)
{
    NhQuat qy = nh_quat_axis_angle(nh_v3(0, 1, 0), yaw_y);
    NhQuat qx = nh_quat_axis_angle(nh_v3(1, 0, 0), pitch_x);
    NhQuat qz = nh_quat_axis_angle(nh_v3(0, 0, 1), roll_z);
    return nh_quat_mul(nh_quat_mul(qy, qx), qz);
}

void nh_m34_from_quat_pos(NhMat34 *out, NhQuat q, NhVec3 pos)
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    out->m[0][0] = 1.0f - 2.0f * (yy + zz);
    out->m[0][1] = 2.0f * (xy - wz);
    out->m[0][2] = 2.0f * (xz + wy);
    out->m[0][3] = pos.x;
    out->m[1][0] = 2.0f * (xy + wz);
    out->m[1][1] = 1.0f - 2.0f * (xx + zz);
    out->m[1][2] = 2.0f * (yz - wx);
    out->m[1][3] = pos.y;
    out->m[2][0] = 2.0f * (xz - wy);
    out->m[2][1] = 2.0f * (yz + wx);
    out->m[2][2] = 1.0f - 2.0f * (xx + yy);
    out->m[2][3] = pos.z;
}

NhVec3 nh_quat_rotate(NhQuat q, NhVec3 v)
{
    NhMat34 m;
    nh_m34_from_quat_pos(&m, q, nh_v3(0, 0, 0));
    return nh_m34_mul_dir(&m, v);
}
