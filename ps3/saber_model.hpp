#ifndef MOVEBEAT_PS3_SABER_MODEL_HPP
#define MOVEBEAT_PS3_SABER_MODEL_HPP

#include <cmath>

#include "movebeat/types.hpp"

namespace movebeat {
namespace ps3 {

// How a tracked saber is drawn: a black cylindrical hilt that stands in for
// the PS Move in the player's hand, a glowing emitter where the Move's lit
// sphere sits, and the blade beyond it. Drawing only; cuts still use the
// game's handle-to-tip segment.
struct SaberModel {
    bool valid;
    Vec3 hilt_base;   // bottom of the grip
    Vec3 hilt_top;    // top of the grip, under the emitter
    Vec3 emitter;     // center of the glowing sphere
    Vec3 blade_tip;
};

// Real Move: about 20 cm long, 4.6 cm sphere, grip under 5 cm wide.
const float kHiltBelowHandle = 0.06f;
const float kHiltAboveHandle = 0.12f;
const float kHiltRadius = 0.022f;
const float kEmitterRadius = 0.026f;

inline SaberModel saberModel(const SaberPose& pose) {
    SaberModel model;
    model.valid = false;
    model.hilt_base = model.hilt_top = model.emitter = model.blade_tip =
        pose.handle;
    if (!pose.tracked) return model;
    const float dx = pose.tip.x - pose.handle.x;
    const float dy = pose.tip.y - pose.handle.y;
    const float dz = pose.tip.z - pose.handle.z;
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    // A blade must remain beyond the emitter.
    if (!(length > kHiltAboveHandle + kEmitterRadius * 2.0f)) return model;
    const Vec3 direction(dx / length, dy / length, dz / length);
    model.hilt_base = Vec3(pose.handle.x - direction.x * kHiltBelowHandle,
                           pose.handle.y - direction.y * kHiltBelowHandle,
                           pose.handle.z - direction.z * kHiltBelowHandle);
    model.hilt_top = Vec3(pose.handle.x + direction.x * kHiltAboveHandle,
                          pose.handle.y + direction.y * kHiltAboveHandle,
                          pose.handle.z + direction.z * kHiltAboveHandle);
    model.emitter = Vec3(model.hilt_top.x + direction.x * kEmitterRadius,
                         model.hilt_top.y + direction.y * kEmitterRadius,
                         model.hilt_top.z + direction.z * kEmitterRadius);
    model.blade_tip = pose.tip;
    model.valid = true;
    return model;
}

} // namespace ps3
} // namespace movebeat

#endif
