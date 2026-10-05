#ifndef MOVEBEAT_CALIBRATION_HPP
#define MOVEBEAT_CALIBRATION_HPP

#include <cmath>

#include "movebeat/types.hpp"

namespace movebeat {

// A deliberate stable pose, sampled for at least one second. Hardware axis
// calibration remains the responsibility of the input adapter.
class PoseCalibration {
public:
    PoseCalibration() { reset(); }
    void reset() { count_ = 0; start_ = last_ = 0; sum_ = handle_sum_ = Vec3(); }
    unsigned int progressMs() const { return count_ ? last_ - start_ : 0; }
    // Mean midpoint of the two tips, and of the two handles.
    Vec3 origin() const { return count_ ? sum_ * (1.0f / count_) : Vec3(); }
    Vec3 handleOrigin() const { return count_ ? handle_sum_ * (1.0f / count_) : Vec3(); }

    bool sample(unsigned int time, const SaberPose& left, const SaberPose& right) {
        if (!valid(left) || !valid(right)) { reset(); return false; }
        if (count_ && time == last_) return false;
        if (count_ && (time < last_ || time - last_ > 100 ||
                      !stable(left, anchor_left_) || !stable(right, anchor_right_)))
            reset();
        if (!count_) {
            anchor_left_ = left;
            anchor_right_ = right;
            start_ = time;
        }
        last_ = time;
        sum_ = sum_ + (left.tip + right.tip) * 0.5f;
        handle_sum_ = handle_sum_ + (left.handle + right.handle) * 0.5f;
        ++count_;
        return progressMs() >= 1000 && count_ >= 30;
    }

private:
    static bool finite(const Vec3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    static bool valid(const SaberPose& p) {
        return p.tracked && finite(p.tip) && finite(p.handle) &&
               lengthSquared(p.tip - p.handle) > 0.0001f;
    }
    static bool stable(const SaberPose& p, const SaberPose& anchor) {
        return lengthSquared(p.tip - anchor.tip) <= 0.04f * 0.04f &&
               lengthSquared(p.handle - anchor.handle) <= 0.04f * 0.04f &&
               dot(normalized(p.tip - p.handle),
                   normalized(anchor.tip - anchor.handle)) >= 0.9961947f;
    }
    unsigned int count_, start_, last_;
    Vec3 sum_, handle_sum_;
    SaberPose anchor_left_, anchor_right_;
};

// How camera-space poses become play-space poses: the play origin (tip
// midpoint of the forward pose) goes to (0,0,0); the space turns about the
// vertical axis so the player's forward is the playfield's; hand positions
// scale about the forward handle midpoint so every arm span reaches the
// same lanes. Blades keep their real length and direction.
struct BodyFrame {
    Vec3 tip_origin;
    Vec3 handle_origin;
    float yaw_cos;
    float yaw_sin;
    float scale;

    BodyFrame() : yaw_cos(1.0f), yaw_sin(0.0f), scale(1.0f) {}

    Vec3 turn(const Vec3& v) const {
        return Vec3(v.x * yaw_cos + v.z * yaw_sin, v.y,
                    -v.x * yaw_sin + v.z * yaw_cos);
    }
    SaberPose apply(const SaberPose& pose) const {
        if (!pose.tracked) return pose;
        const Vec3 handle = turn(pose.handle - handle_origin) * scale +
                            turn(handle_origin - tip_origin);
        return SaberPose(handle, handle + turn(pose.tip - pose.handle), true);
    }
    // A quick one-pose recalibration moves the origin, keeping turn and scale.
    void recenter(const Vec3& tips, const Vec3& handles) {
        tip_origin = tips;
        handle_origin = handles;
    }
};

// Guided three-pose calibration, as in dance games: both Move held forward
// on outstretched arms and pointed at the screen, then arms out to the
// sides, then arms down. The player says "I am in the pose" with the Move
// button of each controller; each hand is then measured on its own (once
// its sphere has been tracked steadily for a moment, positions averaged
// over a short window), so one badly tracked hand never spoils the other.
// Measurements are accepted as given; implausible ones are only flagged,
// and the derived scale and turn stay within limits. It yields the play
// origin, which physical slot is the left hand, the arm span (lane scale),
// the facing (turn) and a check that the camera's up axis points up.
class BodyCalibration {
public:
    enum Step { STEP_FORWARD = 0, STEP_SIDES = 1, STEP_DOWN = 2, STEP_DONE = 3 };
    // Progress of one hand in the current step.
    enum HandState {
        HAND_WAITING = 0,     // its Move button not pressed yet
        HAND_SETTLING = 1,    // pressed; waiting for a steadily tracked sphere
        HAND_MEASURING = 2,   // averaging its position
        HAND_DONE = 3
    };

    // Reference arm span (handle to handle, meters) and the scale limits.
    static float referenceSpan() { return 1.6f; }
    static float minimumScale() { return 0.75f; }
    static float maximumScale() { return 1.35f; }
    static float maximumTurnDegrees() { return 30.0f; }
    // A sphere tracked this long after the press is trusted, then measured
    // for this long.
    static unsigned int settleMs() { return 500; }
    static unsigned int measureMs() { return 300; }

    BodyCalibration() { reset(); }

    void reset() {
        step_ = STEP_FORWARD;
        for (int slot = 0; slot < 2; ++slot) {
            hand_[slot] = HandProgress();
            result_[slot] = SaberPose();
        }
        forward_tips_ = forward_handles_ = Vec3();
        forward_separation_ = forward_height_ = 0.0f;
        span_ = drop_ = 0.0f;
        turn_degrees_ = 0.0f;
        turn_applied_ = false;
        slots_swapped_ = false;
        inverted_up_ = false;
        narrow_sides_ = false;
    }

    Step step() const { return step_; }
    bool done() const { return step_ == STEP_DONE; }
    HandState handState(int slot) const {
        return slot >= 0 && slot < 2 ? hand_[slot].state : HAND_WAITING;
    }

    // Results, valid once done().
    bool slotsSwapped() const { return slots_swapped_; }
    float span() const { return span_; }
    float drop() const { return drop_; }
    float turnDegrees() const { return turn_degrees_; }
    bool turnApplied() const { return turn_applied_; }
    // Flags of accepted but doubtful measurements: arms down came out
    // above arms forward (camera up axis flipped?), and the sideways pose
    // was hardly wider than the forward one (hands may be swapped).
    bool invertedUp() const { return inverted_up_; }
    bool narrowSides() const { return narrow_sides_; }
    Vec3 forwardHandles() const { return forward_handles_; }
    bool forwardKnown() const { return step_ > STEP_FORWARD; }

    BodyFrame frame() const {
        BodyFrame frame;
        frame.tip_origin = forward_tips_;
        frame.handle_origin = forward_handles_;
        if (turn_applied_) {
            const float radians = turn_degrees_ * 0.01745329251994329577f;
            frame.yaw_cos = std::cos(radians);
            frame.yaw_sin = std::sin(radians);
        }
        frame.scale = span_ > 0.0f ? clampScale(referenceSpan() / span_) : 1.0f;
        return frame;
    }

    // The player confirms the pose with this slot's Move button. A hand
    // already measured in this step may confirm again to measure anew.
    void confirm(int slot) {
        if (step_ == STEP_DONE || slot < 0 || slot > 1) return;
        hand_[slot] = HandProgress();
        hand_[slot].state = HAND_SETTLING;
    }

    // Feeds one frame of both physical slots; true when the last pose ends.
    bool sample(unsigned int time, const SaberPose& slot0, const SaberPose& slot1) {
        if (step_ == STEP_DONE) return false;
        const SaberPose* poses[2] = {&slot0, &slot1};
        for (int slot = 0; slot < 2; ++slot) advance(hand_[slot], *poses[slot], time,
                                                      result_[slot]);
        if (hand_[0].state != HAND_DONE || hand_[1].state != HAND_DONE) return false;
        finishStep();
        return step_ == STEP_DONE;
    }

private:
    struct HandProgress {
        HandState state;
        bool tracking;
        unsigned int since;
        unsigned int last;
        unsigned int count;
        Vec3 handle_sum;
        Vec3 tip_sum;

        HandProgress() : state(HAND_WAITING), tracking(false), since(0), last(0),
                         count(0) {}
    };
    static float clampScale(float scale) {
        return scale < minimumScale() ? minimumScale()
             : (scale > maximumScale() ? maximumScale() : scale);
    }
    static bool finite(const Vec3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    static bool valid(const SaberPose& p) {
        return p.tracked && finite(p.tip) && finite(p.handle) &&
               lengthSquared(p.tip - p.handle) > 0.0001f;
    }

    static void advance(HandProgress& hand, const SaberPose& pose, unsigned int time,
                        SaberPose& result) {
        if (hand.state != HAND_SETTLING && hand.state != HAND_MEASURING) return;
        // A lost sphere, or a gap, starts the steady wait over.
        if (!valid(pose) || (hand.tracking && (time < hand.last || time - hand.last > 150))) {
            hand.state = HAND_SETTLING;
            hand.tracking = false;
            hand.count = 0;
            hand.handle_sum = hand.tip_sum = Vec3();
            if (!valid(pose)) return;
        }
        if (!hand.tracking) {
            hand.tracking = true;
            hand.since = time;
        }
        hand.last = time;
        if (hand.state == HAND_SETTLING) {
            if (time - hand.since < settleMs()) return;
            hand.state = HAND_MEASURING;
            hand.since = time;
        }
        hand.handle_sum = hand.handle_sum + pose.handle;
        hand.tip_sum = hand.tip_sum + pose.tip;
        ++hand.count;
        if (time - hand.since >= measureMs() && hand.count >= 8) {
            const float inverse = 1.0f / static_cast<float>(hand.count);
            result = SaberPose(hand.handle_sum * inverse, hand.tip_sum * inverse, true);
            hand.state = HAND_DONE;
        }
    }

    void finishStep() {
        const SaberPose& a = result_[0];
        const SaberPose& b = result_[1];
        if (step_ == STEP_FORWARD) {
            forward_tips_ = (a.tip + b.tip) * 0.5f;
            forward_handles_ = (a.handle + b.handle) * 0.5f;
            forward_height_ = forward_handles_.y;
            forward_separation_ = std::fabs(a.handle.x - b.handle.x);
            // The player faces where the blades point; the camera looks
            // along +z, so a player facing it points along -z.
            const Vec3 direction = normalized(a.tip - a.handle) +
                                   normalized(b.tip - b.handle);
            const float horizontal = std::sqrt(direction.x * direction.x +
                                               direction.z * direction.z);
            const float total = length(direction);
            turn_degrees_ = 0.0f;
            turn_applied_ = false;
            if (total > 0.0f && horizontal >= 0.7f * total) {
                turn_degrees_ = std::atan2(direction.x, -direction.z) *
                                57.29577951308232f;
                turn_applied_ = std::fabs(turn_degrees_) <= maximumTurnDegrees();
            }
            step_ = STEP_SIDES;
        } else if (step_ == STEP_SIDES) {
            span_ = std::fabs(a.handle.x - b.handle.x);
            // The left hand is the one further left (smaller camera x).
            slots_swapped_ = a.handle.x > b.handle.x;
            narrow_sides_ = span_ < forward_separation_ + 0.3f;
            step_ = STEP_DOWN;
        } else {
            drop_ = forward_height_ - (a.handle.y + b.handle.y) * 0.5f;
            inverted_up_ = drop_ < 0.0f;
            step_ = STEP_DONE;
        }
        for (int slot = 0; slot < 2; ++slot) hand_[slot] = HandProgress();
    }

    Step step_;
    HandProgress hand_[2];
    SaberPose result_[2];
    Vec3 forward_tips_, forward_handles_;
    float forward_separation_, forward_height_;
    float span_, drop_;
    float turn_degrees_;
    bool turn_applied_;
    bool slots_swapped_;
    bool inverted_up_;
    bool narrow_sides_;
};

} // namespace movebeat
#endif
