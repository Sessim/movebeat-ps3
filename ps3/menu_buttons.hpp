#ifndef MOVEBEAT_PS3_MENU_BUTTONS_HPP
#define MOVEBEAT_PS3_MENU_BUTTONS_HPP

#include "move_input.hpp"
#include <cstdint>

namespace movebeat {
namespace ps3 {

struct MenuButtonSample {
    unsigned short held;
    unsigned short pressed;
    unsigned short left_pressed;
    unsigned short right_pressed;
    unsigned short left_trigger;
    unsigned short right_trigger;

    MenuButtonSample()
        : held(0), pressed(0), left_pressed(0), right_pressed(0),
          left_trigger(0), right_trigger(0) {}
};

// A missing camera frame is not a button release. Accept GEM or inertial
// samples independently for each Move, and ignore buttons already held when a
// controller first connects. A second Move can still press the same button.
class MenuButtonEdges {
public:
    MenuButtonEdges() {
        for (unsigned int i = 0; i < 2; ++i) {
            previous_[i] = 0;
            trigger_[i] = 0;
            seen_[i] = false;
            stale_[i] = 0;
        }
    }

    MenuButtonSample update(const MoveFrame& frame, unsigned int ready_mask) {
        MenuButtonSample sample;
        const bool connected[2] = {frame.left_connected, frame.right_connected};
        const unsigned short buttons[2] = {frame.left_buttons, frame.right_buttons};
        const unsigned short triggers[2] = {frame.left_trigger, frame.right_trigger};
        for (unsigned int i = 0; i < 2; ++i) {
            if (!connected[i]) {
                previous_[i] = 0;
                trigger_[i] = 0;
                seen_[i] = false;
                stale_[i] = 0;
                continue;
            }
            if ((ready_mask & (1u << i)) != 0) {
                unsigned short newly_pressed = 0;
                if (seen_[i]) {
                    newly_pressed = static_cast<unsigned short>(
                        buttons[i] & ~previous_[i]);
                    sample.pressed |= newly_pressed;
                }
                if (i == 0) sample.left_pressed = newly_pressed;
                else sample.right_pressed = newly_pressed;
                previous_[i] = buttons[i];
                trigger_[i] = triggers[i];
                seen_[i] = true;
                stale_[i] = 0;
            } else if (stale_[i] < 4) {
                ++stale_[i];
            }
            // Brief gaps keep a trigger chord intact, but longer outages must
            // not leave a virtual button held forever. previous_ stays armed
            // so reacquiring an already-held button does not create an edge.
            if (stale_[i] <= 3) {
                sample.held |= previous_[i];
                if (i == 0) sample.left_trigger = trigger_[i];
                else sample.right_trigger = trigger_[i];
            }
        }
        return sample;
    }

private:
    unsigned short previous_[2];
    unsigned short trigger_[2];
    bool seen_[2];
    unsigned char stale_[2];
};

// Track-list navigation repeats only after a deliberate hold. A button that
// was already held when a Move connected cannot arm the repeater; it must be
// released and pressed again. Lost samples eventually clear the held state.
class MenuNavigationRepeat {
public:
    MenuNavigationRepeat() : active_(0), next_ms_(0) {}

    void reset() { active_ = 0; next_ms_ = 0; }

    unsigned short update(unsigned short held, unsigned short pressed,
                          unsigned int now_ms) {
        const unsigned short navigation = static_cast<unsigned short>(
            GEM_BUTTON_TRIANGLE | GEM_BUTTON_CROSS);
        held &= navigation;
        pressed &= navigation;
        if (held == 0 || held == navigation) {
            reset();
            return 0;
        }
        if (pressed & held) {
            active_ = held;
            next_ms_ = now_ms + 450u;
            return 0;
        }
        if (held != active_) return 0;
        if (static_cast<std::int32_t>(now_ms - next_ms_) < 0) return 0;
        next_ms_ = now_ms + 150u;
        return held;
    }

private:
    unsigned short active_;
    unsigned int next_ms_;
};

} // namespace ps3
} // namespace movebeat

#endif
