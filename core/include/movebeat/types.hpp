#ifndef MOVEBEAT_TYPES_HPP
#define MOVEBEAT_TYPES_HPP

#include <cmath>

namespace movebeat {

struct Vec3 {
    float x;
    float y;
    float z;

    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

inline Vec3 operator+(const Vec3& a, const Vec3& b) {
    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

inline Vec3 operator*(const Vec3& v, float scale) {
    return Vec3(v.x * scale, v.y * scale, v.z * scale);
}

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float lengthSquared(const Vec3& v) {
    return dot(v, v);
}

inline float length(const Vec3& v) {
    return std::sqrt(lengthSquared(v));
}

inline Vec3 normalized(const Vec3& v) {
    const float n = length(v);
    return n > 0.00001f ? v * (1.0f / n) : Vec3();
}

enum Hand {
    HAND_LEFT,
    HAND_RIGHT,
    HAND_NONE
};

enum CutDirection {
    CUT_UP,
    CUT_DOWN,
    CUT_LEFT,
    CUT_RIGHT,
    CUT_UP_LEFT,
    CUT_UP_RIGHT,
    CUT_DOWN_LEFT,
    CUT_DOWN_RIGHT,
    CUT_ANY
};

struct SaberPose {
    Vec3 handle;
    Vec3 tip;
    bool tracked;

    SaberPose() : handle(), tip(), tracked(false) {}
    SaberPose(const Vec3& handle_, const Vec3& tip_, bool tracked_)
        : handle(handle_), tip(tip_), tracked(tracked_) {}
};

} // namespace movebeat

#endif
