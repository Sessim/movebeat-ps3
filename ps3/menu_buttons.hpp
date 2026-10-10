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
    unsigned short left_held;
    unsigned short right_held;
    unsigned short left_trigger;
    unsigned short right_trigger;

    MenuButtonSample()
        : held(0), pressed(0), left_pressed(0), right_pressed(0),
          left_held(0), right_held(0), left_trigger(0), right_trigger(0) {}
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
                if (i == 0) {
                    sample.left_held = previous_[i];
                    sample.left_trigger = trigger_[i];
                } else {
                    sample.right_held = previous_[i];
                    sample.right_trigger = trigger_[i];
                }
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
        // Triangle: the track above, Circle: the track below.
        const unsigned short navigation = static_cast<unsigned short>(
            GEM_BUTTON_TRIANGLE | GEM_BUTTON_CIRCLE);
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

// The Move buttons of both controllers pressed together (within
// windowMs of each other) form the start chord. A Move button pressed alone
// is a tap, reported on release so a chord never begins with a tap; a hold
// longer than tapMs, or one that overlapped the other Move button, is
// neither. Slots are physical: the chord does not need to know the hands.
struct MoveChordEvents {
    bool chord;
    int tap_slot;
    // Both Move buttons held for holdMs() (once per hold).
    bool long_chord;

    MoveChordEvents() : chord(false), tap_slot(-1), long_chord(false) {}
};

class MoveButtonChord {
public:
    static unsigned int windowMs() { return 300u; }
    static unsigned int tapMs() { return 800u; }
    static unsigned int holdMs() { return 500u; }

    MoveButtonChord() : both_(false), long_fired_(false), both_since_ms_(0) {
        for (unsigned int i = 0; i < 2; ++i) {
            held_[i] = used_[i] = false;
            press_ms_[i] = 0;
        }
    }

    MoveChordEvents update(bool slot0_held, bool slot1_held, unsigned int now_ms) {
        MoveChordEvents events;
        const bool held[2] = {slot0_held, slot1_held};
        bool pressed = false;
        for (unsigned int i = 0; i < 2; ++i) {
            if (!held[i] || held_[i]) continue;
            press_ms_[i] = now_ms;
            used_[i] = false;
            pressed = true;
        }
        if (pressed && held[0] && held[1]) {
            // Both down: a chord when the presses were close together and
            // neither was claimed yet; otherwise both are spent.
            const unsigned int gap = press_ms_[0] > press_ms_[1]
                ? press_ms_[0] - press_ms_[1] : press_ms_[1] - press_ms_[0];
            if (!used_[0] && !used_[1] && gap <= windowMs()) events.chord = true;
            used_[0] = used_[1] = true;
        }
        for (unsigned int i = 0; i < 2; ++i) {
            if (held[i] || !held_[i]) continue;
            if (!used_[i] && now_ms - press_ms_[i] <= tapMs())
                events.tap_slot = static_cast<int>(i);
            used_[i] = true;
        }
        if (held[0] && held[1]) {
            if (!both_) {
                both_ = true;
                long_fired_ = false;
                both_since_ms_ = now_ms;
            }
            if (!long_fired_ && now_ms - both_since_ms_ >= holdMs()) {
                events.long_chord = true;
                long_fired_ = true;
            }
        } else {
            both_ = false;
        }
        held_[0] = held[0];
        held_[1] = held[1];
        return events;
    }

private:
    bool both_;
    bool long_fired_;
    unsigned int both_since_ms_;
    bool held_[2];
    bool used_[2];
    unsigned int press_ms_[2];
};

} // namespace ps3
} // namespace movebeat

#endif
