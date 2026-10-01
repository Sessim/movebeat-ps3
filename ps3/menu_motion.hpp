#ifndef MOVEBEAT_PS3_MENU_MOTION_HPP
#define MOVEBEAT_PS3_MENU_MOTION_HPP

#include <cmath>
#include <cstdint>
#include <string>

namespace movebeat {
namespace ps3 {

// Scroll state of the circular track carousel. `position` is continuous:
// the entry at round(position) is selected and the rows around it are drawn
// at their fractional offsets. A drag moves it directly; on release it keeps
// the flick's velocity, slows down with friction and settles on a row.
// Button steps and the randomizer move toward a target row instead.
class CarouselScroll {
public:
    // Friction (1/s), speed below which inertia hands over to settling,
    // and the settling speed limits (rows/s).
    static float friction() { return 3.5f; }
    static float settleSpeed() { return 3.0f; }
    static float maxFlickSpeed() { return 40.0f; }
    static float maxTargetSpeed() { return 28.0f; }

    CarouselScroll() : position_(0.0f), velocity_(0.0f), target_(0.0f),
                       dragging_(false), targeting_(false) {}

    void reset(int index) {
        position_ = target_ = static_cast<float>(index);
        velocity_ = 0.0f;
        dragging_ = targeting_ = false;
    }

    void beginDrag() { dragging_ = true; targeting_ = false; velocity_ = 0.0f; }
    void drag(float rows) { if (dragging_) position_ += rows; }
    // Ends a drag; `rows_per_second` continues as inertia.
    void release(float rows_per_second) {
        dragging_ = false;
        if (rows_per_second > maxFlickSpeed()) rows_per_second = maxFlickSpeed();
        if (rows_per_second < -maxFlickSpeed()) rows_per_second = -maxFlickSpeed();
        velocity_ = rows_per_second;
        if (std::fabs(velocity_) < settleSpeed()) settle();
    }
    // Moves `rows` entries from the current target (buttons, randomizer).
    void step(int rows) {
        if (dragging_) return;
        const float base = targeting_ ? target_ : nearestRow();
        target_ = base + static_cast<float>(rows);
        targeting_ = true;
        velocity_ = 0.0f;
    }

    void update(float seconds) {
        if (!(seconds > 0.0f) || dragging_) return;
        if (seconds > 0.1f) seconds = 0.1f;
        if (!targeting_) {
            position_ += velocity_ * seconds;
            velocity_ *= std::exp(-friction() * seconds);
            if (std::fabs(velocity_) < settleSpeed()) settle();
            return;
        }
        // Ease out toward the target, with a speed cap for long spins.
        const float difference = target_ - position_;
        float speed = difference * 10.0f;
        if (speed > maxTargetSpeed()) speed = maxTargetSpeed();
        if (speed < -maxTargetSpeed()) speed = -maxTargetSpeed();
        float move = speed * seconds;
        if (std::fabs(move) >= std::fabs(difference) ||
            std::fabs(difference) < 0.002f) {
            position_ = target_;
            targeting_ = false;
            velocity_ = 0.0f;
            return;
        }
        position_ += move;
    }

    float position() const { return position_; }
    // Row closest to the current position (may be any integer).
    float nearestRow() const { return std::floor(position_ + 0.5f); }
    bool dragging() const { return dragging_; }
    // True when nothing moves: not dragged, no inertia, on a row.
    bool resting() const {
        return !dragging_ && !targeting_ && velocity_ == 0.0f &&
               position_ == nearestRow();
    }

    // Wraps an integer row to an index of a list of `count` entries.
    static std::size_t wrap(float row, std::size_t count) {
        if (count == 0) return 0;
        const long value = static_cast<long>(row);
        const long size = static_cast<long>(count);
        return static_cast<std::size_t>(((value % size) + size) % size);
    }
    std::size_t selected(std::size_t count) const {
        return wrap(nearestRow(), count);
    }

private:
    void settle() {
        // Carry the remaining motion a little before snapping, so a slow
        // flick lands on the row it was heading to.
        target_ = std::floor(position_ + velocity_ * 0.12f + 0.5f);
        targeting_ = true;
        velocity_ = 0.0f;
    }

    float position_;
    float velocity_;
    float target_;
    bool dragging_;
    bool targeting_;
};

// Hand gestures while the Move's T trigger is held. Positions are in meters
// (x right, y up). The first 3.5 cm of motion decide the axis: a vertical
// stroke drags the carousel (and flicks it on release), a horizontal stroke
// steps the difficulty once per 15 cm. Losing tracking cancels the gesture.
struct MenuGestureEvents {
    bool drag_started;
    float drag_rows;
    bool released;
    float flick_rows_per_second;
    int difficulty_step;

    MenuGestureEvents() : drag_started(false), drag_rows(0.0f),
                          released(false), flick_rows_per_second(0.0f),
                          difficulty_step(0) {}
};

class MenuGesture {
public:
    static float lockDistance() { return 0.035f; }
    static float rowsPerMeter() { return 12.0f; }
    static float difficultyDistance() { return 0.15f; }

    MenuGesture() { cancel(); }

    void cancel() {
        state_ = IDLE;
        velocity_ = 0.0f;
        last_ms_ = 0;
    }

    MenuGestureEvents update(bool trigger_down, bool tracked, float x, float y,
                             unsigned int now_ms) {
        MenuGestureEvents events;
        if (!trigger_down || !tracked) {
            if (state_ == VERTICAL) {
                events.released = true;
                // A stale velocity after lost tracking must not throw the list.
                events.flick_rows_per_second = tracked ? velocity_ * rowsPerMeter() : 0.0f;
            }
            cancel();
            return events;
        }
        if (state_ == IDLE) {
            state_ = DECIDING;
            start_x_ = anchor_x_ = last_x_ = x;
            start_y_ = last_y_ = y;
            last_ms_ = now_ms;
            velocity_ = 0.0f;
            return events;
        }
        const float dx = x - start_x_;
        const float dy = y - start_y_;
        if (state_ == DECIDING &&
            dx * dx + dy * dy >= lockDistance() * lockDistance()) {
            if (std::fabs(dy) >= std::fabs(dx)) {
                state_ = VERTICAL;
                events.drag_started = true;
                // Count the locking distance too, so the list follows the hand.
                events.drag_rows = dy * rowsPerMeter();
            } else {
                state_ = HORIZONTAL;
                anchor_x_ = start_x_;
            }
        } else if (state_ == VERTICAL) {
            events.drag_rows = (y - last_y_) * rowsPerMeter();
        }
        if (state_ == VERTICAL) {
            const unsigned int elapsed = now_ms - last_ms_;
            if (elapsed > 0u && elapsed < 200u) {
                const float sample = (y - last_y_) * 1000.0f / elapsed;
                velocity_ = velocity_ * 0.5f + sample * 0.5f;
            }
        } else if (state_ == HORIZONTAL) {
            const float travel = x - anchor_x_;
            if (travel >= difficultyDistance()) {
                events.difficulty_step = 1;
                anchor_x_ += difficultyDistance();
            } else if (travel <= -difficultyDistance()) {
                events.difficulty_step = -1;
                anchor_x_ -= difficultyDistance();
            }
        }
        last_x_ = x;
        last_y_ = y;
        last_ms_ = now_ms;
        return events;
    }

    bool vertical() const { return state_ == VERTICAL; }
    bool horizontal() const { return state_ == HORIZONTAL; }

private:
    enum State { IDLE, DECIDING, VERTICAL, HORIZONTAL };
    State state_;
    float start_x_, start_y_, anchor_x_, last_x_, last_y_;
    float velocity_;
    unsigned int last_ms_;
};

// Difficulty rank for the halo color: Easy 0 ... Expert+ 4.
inline int difficultyRank(const std::string& name) {
    std::string lower;
    for (std::size_t index = 0; index < name.size(); ++index) {
        char letter = name[index];
        if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
        if (letter != ' ' && letter != '+' && letter != '_') lower.push_back(letter);
        if (name[index] == '+') lower += "plus";
    }
    if (lower == "easy") return 0;
    if (lower == "normal") return 1;
    if (lower == "hard") return 2;
    if (lower == "expert") return 3;
    if (lower == "expertplus") return 4;
    return 2;
}

// Green (easy) through yellow and orange to red (Expert+).
inline std::uint32_t difficultyColor(const std::string& name) {
    static const std::uint32_t colors[5] = {
        0xff40e070u, 0xffb8e040u, 0xffffc030u, 0xffff7030u, 0xffff3040u
    };
    return colors[difficultyRank(name)];
}

// Small deterministic generator for the randomizer (no libc rand state).
class MenuRandom {
public:
    explicit MenuRandom(std::uint32_t seed = 1u) : state_(seed ? seed : 1u) {}
    void seed(std::uint32_t value) { state_ = value ? value : 1u; }
    std::uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    // A different entry than `current`, or `current` for a single entry.
    std::size_t pickOther(std::size_t current, std::size_t count) {
        if (count < 2) return current;
        const std::size_t offset = 1u + next() % (count - 1u);
        return (current + offset) % count;
    }

private:
    std::uint32_t state_;
};

} // namespace ps3
} // namespace movebeat

#endif
