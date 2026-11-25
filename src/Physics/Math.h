#pragma once
#include <cmath>

namespace helix::math {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator-() const { return { -x, -y, -z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    float LengthSq() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSq()); }

    Vec3 Normalized() const {
        float len = Length();
        if (len < 1e-8f) return { 0.f, 0.f, 0.f };
        return { x / len, y / len, z / len };
    }

    static float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Vec3 Cross(const Vec3& a, const Vec3& b) {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }
};

struct Quat {
    float x = 0.f, y = 0.f, z = 0.f, w = 1.f;

    static Quat Identity() { return {}; }

    static Quat FromAxisAngle(const Vec3& axis, float radians) {
        Vec3 n = axis.Normalized();
        float half = radians * 0.5f;
        float s = std::sin(half);
        return { n.x * s, n.y * s, n.z * s, std::cos(half) };
    }

    Quat operator*(const Quat& o) const {
        return {
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z
        };
    }

    Quat Normalized() const {
        float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len < 1e-8f) return Identity();
        return { x / len, y / len, z / len, w / len };
    }

    // Integrate orientation by an angular velocity (rad/s) over dt seconds.
    Quat Integrate(const Vec3& angularVelocity, float dt) const {
        Quat omega{ angularVelocity.x, angularVelocity.y, angularVelocity.z, 0.f };
        Quat delta = omega * (*this);
        Quat result{
            x + delta.x * 0.5f * dt,
            y + delta.y * 0.5f * dt,
            z + delta.z * 0.5f * dt,
            w + delta.w * 0.5f * dt
        };
        return result.Normalized();
    }

    Vec3 Forward() const {
        // +Z forward convention
        return {
            2.f * (x * z + w * y),
            2.f * (y * z - w * x),
            1.f - 2.f * (x * x + y * y)
        };
    }

    Vec3 Up() const {
        return {
            2.f * (x * y - w * z),
            1.f - 2.f * (x * x + z * z),
            2.f * (y * z + w * x)
        };
    }

    Vec3 Right() const {
        return {
            1.f - 2.f * (y * y + z * z),
            2.f * (x * y + w * z),
            2.f * (x * z - w * y)
        };
    }
};

struct Mat4 {
    float m[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    static Mat4 Identity() { return {}; }

    static Mat4 FromTRS(const Vec3& t, const Quat& q, float scale) {
        Mat4 result{};
        Vec3 right = q.Right() * scale;
        Vec3 up = q.Up() * scale;
        Vec3 fwd = q.Forward() * scale;
        result.m[0] = right.x; result.m[1] = right.y; result.m[2] = right.z; result.m[3] = 0.f;
        result.m[4] = up.x;    result.m[5] = up.y;    result.m[6] = up.z;    result.m[7] = 0.f;
        result.m[8] = fwd.x;   result.m[9] = fwd.y;   result.m[10] = fwd.z;  result.m[11] = 0.f;
        result.m[12] = t.x;    result.m[13] = t.y;    result.m[14] = t.z;    result.m[15] = 1.f;
        return result;
    }

    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& upHint) {
        Vec3 z = (eye - target).Normalized();          // forward = -z in view space (RH)
        Vec3 x = Vec3::Cross(upHint, z).Normalized();
        Vec3 y = Vec3::Cross(z, x);
        Mat4 r{};
        r.m[0] = x.x; r.m[4] = x.y; r.m[8]  = x.z; r.m[12] = -Vec3::Dot(x, eye);
        r.m[1] = y.x; r.m[5] = y.y; r.m[9]  = y.z; r.m[13] = -Vec3::Dot(y, eye);
        r.m[2] = z.x; r.m[6] = z.y; r.m[10] = z.z; r.m[14] = -Vec3::Dot(z, eye);
        r.m[3] = 0;   r.m[7] = 0;   r.m[11] = 0;   r.m[15] = 1;
        return r;
    }

    static Mat4 Perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
        float f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 r{};
        for (int i = 0; i < 16; ++i) r.m[i] = 0.f;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = farZ / (nearZ - farZ);
        r.m[11] = -1.f;
        r.m[14] = (farZ * nearZ) / (nearZ - farZ);
        return r;
    }

    static Mat4 Multiply(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float sum = 0.f;
                for (int k = 0; k < 4; ++k) {
                    sum += a.m[row * 4 + k] * b.m[k * 4 + col];
                }
                r.m[row * 4 + col] = sum;
            }
        }
        return r;
    }
};

} // namespace helix::math
