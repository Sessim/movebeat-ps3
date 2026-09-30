#ifndef MOVEBEAT_PS3_MOVE_INPUT_HPP
#define MOVEBEAT_PS3_MOVE_INPUT_HPP

#include "movebeat/types.hpp"

#include <io/move.h>

namespace movebeat {
namespace ps3 {

// PSL1GHT's move.h exposes the ABI but not the symbolic values from the
// original cellGem header. Keep the runtime statuses and buttons named here so
// startup recovery never depends on unexplained numeric literals.
enum GemRuntimeStatus {
    GEM_STATUS_OK = 0,
    GEM_STATUS_NOT_CONNECTED = 1,
    GEM_STATUS_SPHERE_NOT_CALIBRATED = 2,
    GEM_STATUS_SPHERE_CALIBRATING = 3,
    GEM_STATUS_COMPUTING_COLORS = 4,
    GEM_STATUS_HUE_NOT_SET = 5,
    GEM_STATUS_NO_VIDEO = 6,
    GEM_STATUS_TIME_OUT_OF_RANGE = 7,
    GEM_STATUS_NOT_CALIBRATED = 8
};

enum GemConnectionStatus {
    GEM_CONNECTION_DISCONNECTED = 0,
    GEM_CONNECTION_READY = 1
};

// cellGemTrackHues encodes these modes in the high byte; they are not hue
// angles. DONT_CARE asks libgem to assign distinct controller hues itself.
static const u32 GEM_HUE_DONT_TRACK = 2u << 24;
static const u32 GEM_HUE_DONT_CARE = 4u << 24;
static const u32 GEM_HUE_DONT_CHANGE = 8u << 24;

// Regular cellGemTrackHues values are hue angles in degrees. These match the
// project's left-blue/right-red saber palette; their slot mapping follows the
// calibrated hands rather than USB connection order.
static const u32 GEM_HUE_LEFT_HAND = 204u;
static const u32 GEM_HUE_RIGHT_HAND = 352u;

inline u32 gemHueForPhysicalSlot(unsigned int slot, bool slots_swapped) {
    if (slot == 0u)
        return slots_swapped ? GEM_HUE_RIGHT_HAND : GEM_HUE_LEFT_HAND;
    if (slot == 1u)
        return slots_swapped ? GEM_HUE_LEFT_HAND : GEM_HUE_RIGHT_HAND;
    return GEM_HUE_DONT_CARE;
}

enum GemButtonMask {
    GEM_BUTTON_SELECT = 1u << 0,
    GEM_BUTTON_T = 1u << 1,
    GEM_BUTTON_MOVE = 1u << 2,
    GEM_BUTTON_START = 1u << 3,
    GEM_BUTTON_TRIANGLE = 1u << 4,
    GEM_BUTTON_CIRCLE = 1u << 5,
    GEM_BUTTON_CROSS = 1u << 6,
    GEM_BUTTON_SQUARE = 1u << 7
};

struct MoveFrame {
    struct ImagePoint {
        float x;
        float y;
        bool valid;
        ImagePoint() : x(0.0f), y(0.0f), valid(false) {}
    };
    SaberPose left;
    SaberPose right;
    ImagePoint image[2];
    bool left_connected;
    bool right_connected;
    unsigned short left_buttons;
    unsigned short right_buttons;
    unsigned short left_trigger;
    unsigned short right_trigger;

    MoveFrame()
        : left(), right(), left_connected(false), right_connected(false),
          left_buttons(0), right_buttons(0), left_trigger(0), right_trigger(0) {}
};

// Converts a libgem sample already captured for this camera frame. Keeping the
// conversion separate avoids a second gemGetState() call and guarantees that
// pose and buttons describe the same instant.
void applyGemSample(unsigned int index, const gemState& state, int result,
                    float blade_length_m, MoveFrame& frame);
// Buttons remain usable without an optical camera frame. This intentionally
// leaves both saber poses untracked; inertial data alone is not a position.
void applyInertialSample(unsigned int index, const gemInertialState& state,
                         int result, MoveFrame& frame);
bool gemResultNeedsCalibration(int result);
bool gemResultNeedsHues(int result);
bool gemSlotReady(unsigned int status);

} // namespace ps3
} // namespace movebeat

#endif
