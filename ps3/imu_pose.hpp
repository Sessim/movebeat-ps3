#ifndef MOVEBEAT_PS3_IMU_POSE_HPP
#define MOVEBEAT_PS3_IMU_POSE_HPP

#include <cmath>

#include "movebeat/types.hpp"

namespace movebeat {
namespace ps3 {

// libgem's orientation quaternion as four raw components, in the order
// gemState stores them.
struct RawQuat {
    float v[4];
    RawQuat() { v[0] = v[1] = v[2] = 0.0f; v[3] = 1.0f; }
    RawQuat(float a, float b, float c, float d) { v[0] = a; v[1] = b; v[2] = c; v[3] = d; }
};

// The sphere is the camera's anchor, the Move's own gyro and accelerometer
// (fused by libgem into an orientation quaternion) its second source. While
// the camera sees the sphere, the pose is the camera's. When it loses the
// sphere (hidden, or a Move pointed straight at the lens) the hand stays
// where it was last seen and the blade keeps turning with the quaternion,
// for up to holdMs(); then the hand counts as lost.
//
// Which way libgem's quaternion turns (component order, and whether it maps
// controller to world or world to controller) is not yet checked on
// hardware, so all four readings are scored against the camera while the
// sphere is visible and the best one is used.
class ImuSaberTracker {
public:
    enum { VARIANTS = 4 };
    static unsigned int holdMs() { return 3000; }
    // How far apart, in time, a camera direction and the quaternion are
    // compared to score the readings.
    static unsigned int scoreIntervalMs() { return 150; }

    ImuSaberTracker() { reset(); }

    void reset() {
        have_last_ = have_ref_ = false;
        last_ms_ = ref_ms_ = 0;
        for (int variant = 0; variant < VARIANTS; ++variant) error_[variant] = 0.0f;
        scored_ = 0;
        held_ = false;
    }

    // `optical`: this frame's camera pose (untracked when the camera lost
    // the sphere). `quat_valid`/`quat`: libgem's orientation this frame.
    SaberPose update(unsigned int now_ms, const SaberPose& optical, bool quat_valid,
                     const RawQuat& quat) {
        held_ = false;
        const bool usable_quat = quat_valid && unit(quat);
        if (optical.tracked) {
            if (usable_quat) {
                score(now_ms, optical, quat);
                last_ = optical;
                last_quat_ = quat;
                last_ms_ = now_ms;
                have_last_ = true;
            } else {
                have_last_ = have_ref_ = false;
            }
            return optical;
        }
        have_ref_ = false;
        if (!usable_quat || !have_last_ || now_ms < last_ms_ ||
            now_ms - last_ms_ > holdMs())
            return SaberPose();
        const Vec3 shaft = turn(bestVariant(), quat, last_quat_, last_.tip - last_.handle);
        held_ = true;
        return SaberPose(last_.handle, last_.handle + shaft, true);
    }

    // True when the latest pose came from the quaternion, not the camera.
    bool held() const { return held_; }
    int bestVariant() const {
        int best = 0;
        for (int variant = 1; variant < VARIANTS; ++variant)
            if (error_[variant] < error_[best]) best = variant;
        return best;
    }
    // Smoothed angle (degrees) between camera and quaternion turns.
    float errorDegrees(int variant) const {
        return variant >= 0 && variant < VARIANTS ? error_[variant] : 0.0f;
    }
    unsigned int scoredSamples() const { return scored_; }

    // The rotation from `from` to `to` in one reading of the components,
    // applied to `v`. Readings: 0 xyzw world-from-controller, 1 xyzw
    // controller-from-world, 2 wxyz world-from-controller, 3 wxyz
    // controller-from-world.
    static Vec3 turn(int variant, const RawQuat& to, const RawQuat& from, const Vec3& v) {
        Q a = read(to, variant >= 2), b = read(from, variant >= 2);
        // world-from-controller q: world change = q_to * conj(q_from).
        // controller-from-world q: world change = conj(q_to) * q_from.
        const Q change = (variant % 2 == 0) ? multiply(a, conjugate(b))
                                            : multiply(conjugate(a), b);
        return rotate(change, v);
    }

private:
    struct Q { float w, x, y, z; };

    static Q read(const RawQuat& raw, bool w_first) {
        Q q;
        if (w_first) { q.w = raw.v[0]; q.x = raw.v[1]; q.y = raw.v[2]; q.z = raw.v[3]; }
        else { q.x = raw.v[0]; q.y = raw.v[1]; q.z = raw.v[2]; q.w = raw.v[3]; }
        return q;
    }
    static bool unit(const RawQuat& raw) {
        float n = 0.0f;
        for (int i = 0; i < 4; ++i) {
            if (!std::isfinite(raw.v[i])) return false;
            n += raw.v[i] * raw.v[i];
        }
        return n > 0.8f && n < 1.2f;
    }
    static Q conjugate(const Q& q) { Q r = {q.w, -q.x, -q.y, -q.z}; return r; }
    static Q multiply(const Q& a, const Q& b) {
        Q r;
        r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
        r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
        r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
        r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
        return r;
    }
    static Vec3 rotate(const Q& q, const Vec3& v) {
        // v + 2w (u x v) + 2 u x (u x v), u = (x, y, z)
        const Vec3 u(q.x, q.y, q.z);
        const Vec3 c1(u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x);
        const Vec3 c2(u.y * c1.z - u.z * c1.y, u.z * c1.x - u.x * c1.z,
                      u.x * c1.y - u.y * c1.x);
        return v + c1 * (2.0f * q.w) + c2 * 2.0f;
    }

    void score(unsigned int now_ms, const SaberPose& optical, const RawQuat& quat) {
        const Vec3 direction = normalized(optical.tip - optical.handle);
        if (!have_ref_ || now_ms < ref_ms_) {
            ref_direction_ = direction;
            ref_quat_ = quat;
            ref_ms_ = now_ms;
            have_ref_ = true;
            return;
        }
        if (now_ms - ref_ms_ < scoreIntervalMs()) return;
        // Only a real turn tells the readings apart.
        if (dot(direction, ref_direction_) < 0.995f) {
            for (int variant = 0; variant < VARIANTS; ++variant) {
                const Vec3 predicted = normalized(turn(variant, quat, ref_quat_, ref_direction_));
                float cosine = dot(predicted, direction);
                cosine = cosine > 1.0f ? 1.0f : (cosine < -1.0f ? -1.0f : cosine);
                const float degrees = std::acos(cosine) * 57.29577951308232f;
                error_[variant] = scored_ ? error_[variant] * 0.9f + degrees * 0.1f : degrees;
            }
            ++scored_;
        }
        ref_direction_ = direction;
        ref_quat_ = quat;
        ref_ms_ = now_ms;
    }

    bool have_last_;
    SaberPose last_;
    RawQuat last_quat_;
    unsigned int last_ms_;
    bool have_ref_;
    Vec3 ref_direction_;
    RawQuat ref_quat_;
    unsigned int ref_ms_;
    float error_[VARIANTS];
    unsigned int scored_;
    bool held_;
};

} // namespace ps3
} // namespace movebeat

#endif
