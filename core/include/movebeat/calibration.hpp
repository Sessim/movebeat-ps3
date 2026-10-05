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

// How camera-space poses become play space, where +z is the player's
// forward (toward the notes): the hit origin goes to (0,0,0); the space
// turns about the vertical axis so the player's facing is the playfield's,
// and its depth flips when the player faces the camera's -z (libgem on
// hardware); hand positions scale about the forward handle midpoint so
// every arm span reaches the same lanes. Blades keep their real length.
//
// The hit origin, where the notes are cut, lies hitDepth() along the
// blades from the handles of the forward pose: mid-blade with arms
// stretched out, nearer the tip with arms bent as in play. (At the blade
// tips of the stretched pose only the very tips ever reached the notes.)
struct BodyFrame {
    Vec3 hit_origin;
    Vec3 handle_origin;
    float yaw_cos;
    float yaw_sin;
    float scale;
    bool flip_depth;

    static float hitDepth() { return 0.45f; }

    BodyFrame() : yaw_cos(1.0f), yaw_sin(0.0f), scale(1.0f), flip_depth(false) {}

    Vec3 turn(const Vec3& v) const {
        return Vec3(v.x * yaw_cos + v.z * yaw_sin, v.y,
                    -v.x * yaw_sin + v.z * yaw_cos);
    }
    // Directions and offsets: turned, then the depth flipped if needed.
    Vec3 orient(const Vec3& v) const {
        const Vec3 turned = turn(v);
        return flip_depth ? Vec3(turned.x, turned.y, -turned.z) : turned;
    }
    SaberPose apply(const SaberPose& pose) const {
        if (!pose.tracked) return pose;
        const Vec3 handle = orient(pose.handle - handle_origin) * scale +
                            orient(handle_origin - hit_origin);
        return SaberPose(handle, handle + orient(pose.tip - pose.handle), true);
    }
    // A quick one-pose recalibration (blades pointing forward) moves the
    // origin, keeping turn, depth and scale.
    void recenter(const Vec3& tips, const Vec3& handles) {
        handle_origin = handles;
        hit_origin = handles + normalized(tips - handles) * hitDepth();
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
    // for this long. Dropouts up to gapMs() are skipped, not restarted:
    // on hardware libgem loses a sphere for a frame or two all the time.
    static unsigned int settleMs() { return 300; }
    static unsigned int measureMs() { return 250; }
    static unsigned int gapMs() { return 200; }

    BodyCalibration() { reset(); }

    void reset() {
        step_ = STEP_FORWARD;
        for (int slot = 0; slot < 2; ++slot) {
            hand_[slot] = HandProgress();
            result_[slot] = SaberPose();
        }
        forward_direction_ = forward_handles_ = Vec3();
        flip_depth_ = true;
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
    // The forward pose pointed along the camera's -z (play depth flipped).
    bool flipDepth() const { return flip_depth_; }
    bool forwardKnown() const { return step_ > STEP_FORWARD; }

    BodyFrame frame() const {
        BodyFrame frame;
        frame.handle_origin = forward_handles_;
        frame.hit_origin = forward_handles_ + forward_direction_ * BodyFrame::hitDepth();
        frame.flip_depth = flip_depth_;
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
        // A short dropout is skipped; a longer one starts the steady wait
        // over.
        const bool gap = hand.tracking && (time < hand.last || time - hand.last > gapMs());
        if (!valid(pose) && !gap && hand.tracking) return;
        if (!valid(pose) || gap) {
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
            forward_handles_ = (a.handle + b.handle) * 0.5f;
            forward_height_ = forward_handles_.y;
            forward_separation_ = std::fabs(a.handle.x - b.handle.x);
            // The player faces where the blades point. A player facing the
            // camera points along its -z (libgem on hardware): play depth
            // flips then. Only a clear +z keeps it (the emulator scripts).
            const Vec3 direction = normalized(normalized(a.tip - a.handle) +
                                              normalized(b.tip - b.handle));
            forward_direction_ = direction;
            flip_depth_ = !(direction.z > 0.3f);
            const float horizontal = std::sqrt(direction.x * direction.x +
                                               direction.z * direction.z);
            turn_degrees_ = 0.0f;
            turn_applied_ = false;
            if (horizontal >= 0.7f) {
                // The turn that brings the facing onto the camera's -z
                // (flipped) or +z axis.
                turn_degrees_ = (flip_depth_ ? std::atan2(direction.x, -direction.z)
                                             : -std::atan2(direction.x, direction.z)) *
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
    Vec3 forward_direction_, forward_handles_;
    bool flip_depth_;
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
