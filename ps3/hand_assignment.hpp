#ifndef MOVEBEAT_PS3_HAND_ASSIGNMENT_HPP
#define MOVEBEAT_PS3_HAND_ASSIGNMENT_HPP

#include "move_input.hpp"

#include <cmath>
#include <utility>

namespace movebeat {
namespace ps3 {

// Hold the two handles visibly apart during the one-second starting pose.
// A near-overlap gives no reliable physical left/right assignment.
inline bool handsSeparatedForCalibration(const SaberPose& slot0,
                                         const SaberPose& slot1) {
    return slot0.tracked && slot1.tracked &&
           std::isfinite(slot0.handle.x) && std::isfinite(slot1.handle.x) &&
           std::fabs(slot0.handle.x - slot1.handle.x) >= 0.15f;
}

inline bool shouldSwapHandSlots(const SaberPose& slot0,
                                const SaberPose& slot1) {
    return slot0.handle.x > slot1.handle.x;
}

inline void swapMoveFrameHands(MoveFrame& frame) {
    std::swap(frame.left, frame.right);
    std::swap(frame.left_connected, frame.right_connected);
    std::swap(frame.image[0], frame.image[1]);
    std::swap(frame.left_buttons, frame.right_buttons);
    std::swap(frame.left_trigger, frame.right_trigger);
    std::swap(frame.held[0], frame.held[1]);
    std::swap(frame.partial[0], frame.partial[1]);
}

inline unsigned int physicalSlotForHand(unsigned int logical_hand,
                                         bool swapped) {
    return swapped ? 1u - logical_hand : logical_hand;
}

} // namespace ps3
} // namespace movebeat

#endif
