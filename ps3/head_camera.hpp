#ifndef MOVEBEAT_PS3_HEAD_CAMERA_HPP
#define MOVEBEAT_PS3_HEAD_CAMERA_HPP

#include <cmath>

#include "movebeat/types.hpp"

namespace movebeat {
namespace ps3 {

// Turns the position of a Move worn on the head into a camera offset.
// The player only leans a little (a long step loses the hands from the
// PS Eye) and ducks under walls, so small head moves are amplified
// sideways and up/down; forward/back counts less. The sphere position
// comes in camera coordinates (+x right, +y up, +z away from the camera);
// the offset is in world terms (+z into the screen, toward the camera).
class HeadCamera {
public:
    HeadCamera()
        : gain_(1.5f), mirror_x_(false), flip_z_(false), has_neutral_(false),
          neutral_(0.0f, 0.0f, 0.0f), offset_(0.0f, 0.0f, 0.0f) {}

    // The head's resting position: the camera's home view.
    void recenter(const Vec3& sphere) {
        neutral_ = sphere;
        has_neutral_ = true;
        offset_ = Vec3(0.0f, 0.0f, 0.0f);
    }
    void forget() {
        has_neutral_ = false;
        offset_ = Vec3(0.0f, 0.0f, 0.0f);
    }

    // Follows the sphere while the camera sees it; when it does not, the
    // view holds where it was.
    const Vec3& update(bool seen, const Vec3& sphere) {
        if (!has_neutral_ || !seen) return offset_;
        const Vec3 moved = sphere - neutral_;
        Vec3 target((mirror_x_ ? -moved.x : moved.x) * gain_,
                     moved.y * gain_,
                     (flip_z_ ? moved.z : -moved.z) * gain_ * kDepthShare);
        target.x = clamp(target.x, -kMaxSide, kMaxSide);
        target.y = clamp(target.y, -kMaxDown, kMaxUp);
        target.z = clamp(target.z, -kMaxDepth, kMaxDepth);
        // Camera depth from the sphere's size is noisier than its image
        // position, so it is smoothed more.
        offset_.x += (target.x - offset_.x) * kFollow;
        offset_.y += (target.y - offset_.y) * kFollow;
        offset_.z += (target.z - offset_.z) * kFollowDepth;
        return offset_;
    }

    void changeGain(float step) {
        gain_ = clamp(gain_ + step, 0.0f, kMaxGain);
    }
    void toggleMirrorX() { mirror_x_ = !mirror_x_; }
    void toggleFlipZ() { flip_z_ = !flip_z_; }

    float gain() const { return gain_; }
    bool mirrorX() const { return mirror_x_; }
    bool flipZ() const { return flip_z_; }
    bool hasNeutral() const { return has_neutral_; }
    const Vec3& offset() const { return offset_; }

    static constexpr float kDepthShare = 1.0f / 3.0f;
    static constexpr float kMaxGain = 4.0f;
    static constexpr float kMaxSide = 0.5f;
    static constexpr float kMaxDown = 0.5f;
    static constexpr float kMaxUp = 0.3f;
    static constexpr float kMaxDepth = 0.4f;
    static constexpr float kFollow = 0.35f;
    static constexpr float kFollowDepth = 0.12f;

private:
    static float clamp(float value, float low, float high) {
        return value < low ? low : (value > high ? high : value);
    }

    float gain_;
    bool mirror_x_;
    bool flip_z_;
    bool has_neutral_;
    Vec3 neutral_;
    Vec3 offset_;
};

} // namespace ps3
} // namespace movebeat

#endif
