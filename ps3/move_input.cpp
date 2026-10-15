#include "move_input.hpp"

#include <cmath>

namespace movebeat {
namespace ps3 {
namespace {

float component(const vec_float4& value, unsigned int index) {
    union VectorAccess {
        vec_float4 vector;
        float values[4];
    } access;
    access.vector = value;
    return index < 4 ? access.values[index] : 0.0f;
}

Vec3 gemPointToGame(const vec_float4 value) {
    // RPCS3's cellGem model reports millimetres in camera space: +x right,
    // +y up, +z away from the camera. We use that expected libgem convention
    // until a raw capture from a physical PS3 confirms hardware signs and
    // scale. Calibration before play supplies the local origin.
    return Vec3(component(value, 0) * 0.001f,
                component(value, 1) * 0.001f,
                component(value, 2) * 0.001f);
}

SaberPose toSaberPose(const gemState& state, float blade_length_m) {
    // Either flag will do for play. On hardware libgem often reports a
    // sphere it sees without a position fix (VISIBLE alone), e.g. pointed
    // straight at the lens; its position still follows the image.
    const bool optical_tracking =
        (state.tracking & (GEM_TRACKING_POSITION_TRACKED | GEM_TRACKING_VISIBLE)) != 0;
    const Vec3 handle = gemPointToGame(state.handle_pos);
    const Vec3 sphere = gemPointToGame(state.pos);
    const bool finite = std::isfinite(handle.x) && std::isfinite(handle.y) &&
        std::isfinite(handle.z) && std::isfinite(sphere.x) &&
        std::isfinite(sphere.y) && std::isfinite(sphere.z) &&
        std::isfinite(blade_length_m) && blade_length_m > 0.0f;
    if (!optical_tracking || !finite) return SaberPose();
    const float shaft_length_squared = lengthSquared(sphere - handle);
    if (!std::isfinite(shaft_length_squared) || shaft_length_squared < 0.000001f)
        return SaberPose();
    const Vec3 shaft = normalized(sphere - handle);
    // Virtual blade continues from the glowing sphere. Its reach is adjustable
    // on the selection screen because camera geometry varies between setups.
    const Vec3 tip = sphere + shaft * blade_length_m;
    if (!std::isfinite(tip.x) || !std::isfinite(tip.y) || !std::isfinite(tip.z))
        return SaberPose();
    return SaberPose(handle, tip, true);
}

} // namespace

void applyGemSample(unsigned int index, const gemState& state, int result,
                    float blade_length_m, MoveFrame& frame) {
    SaberPose pose;
    unsigned short buttons = 0;
    unsigned short trigger = 0;
    bool partial = false;
    if (result == 0) {
        pose = toSaberPose(state, blade_length_m);
        const unsigned int full = GEM_TRACKING_POSITION_TRACKED | GEM_TRACKING_VISIBLE;
        partial = pose.tracked && (state.tracking & full) != full;
        buttons = state.paddata.buttons;
        trigger = state.paddata.ANA_T;
    }

    if (index == 0) {
        frame.left = pose;
        frame.left_buttons = buttons;
        frame.left_trigger = trigger;
    } else if (index == 1) {
        frame.right = pose;
        frame.right_buttons = buttons;
        frame.right_trigger = trigger;
    }
    if (index < 2) {
        frame.partial[index] = partial;
        const bool seen = result == 0 &&
            (state.tracking & (GEM_TRACKING_POSITION_TRACKED | GEM_TRACKING_VISIBLE)) != 0;
        const Vec3 sphere = gemPointToGame(state.pos);
        frame.sphere_seen[index] = seen && std::isfinite(sphere.x) &&
                                   std::isfinite(sphere.y) && std::isfinite(sphere.z);
        frame.sphere[index] = frame.sphere_seen[index] ? sphere : Vec3();
    }
}

void applyInertialSample(unsigned int index, const gemInertialState& state,
                         int result, MoveFrame& frame) {
    const unsigned short buttons = result == 0 ? state.pad.buttons : 0;
    const unsigned short trigger = result == 0 ? state.pad.ANA_T : 0;
    if (index == 0) {
        frame.left_buttons = buttons;
        frame.left_trigger = trigger;
    } else if (index == 1) {
        frame.right_buttons = buttons;
        frame.right_trigger = trigger;
    }
}

bool gemResultNeedsCalibration(int result) {
    return result == GEM_STATUS_SPHERE_NOT_CALIBRATED ||
           result == GEM_STATUS_NOT_CALIBRATED;
}

bool gemResultNeedsHues(int result) {
    return result == GEM_STATUS_HUE_NOT_SET;
}

bool gemSlotReady(unsigned int status) {
    return status == GEM_CONNECTION_READY;
}

} // namespace ps3
} // namespace movebeat
