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
// sides, then arms down. Each pose is held still for a second. It yields
// the play origin, which physical slot is the left hand (from the wide
// sideways pose), the arm span (lane scale), the facing (turn) and a check
// that the camera's up axis points up.
class BodyCalibration {
public:
    enum Step { STEP_FORWARD = 0, STEP_SIDES = 1, STEP_DOWN = 2, STEP_DONE = 3 };
    // Why the current step does not progress.
    enum Status { STATUS_UNTRACKED, STATUS_POSE, STATUS_HOLD };

    // Reference arm span (handle to handle, meters) and the scale limits.
    static float referenceSpan() { return 1.6f; }
    static float minimumScale() { return 0.75f; }
    static float maximumScale() { return 1.35f; }
    static float maximumTurnDegrees() { return 30.0f; }

    BodyCalibration() { reset(); }

    void reset() {
        step_ = STEP_FORWARD;
        status_ = STATUS_UNTRACKED;
        count_ = start_ = last_ = 0;
        tip_sum_ = handle_sum_ = direction_sum_ = Vec3();
        forward_tips_ = forward_handles_ = Vec3();
        forward_separation_ = forward_height_ = 0.0f;
        span_ = drop_ = 0.0f;
        turn_degrees_ = 0.0f;
        turn_applied_ = false;
        slots_swapped_ = false;
        inverted_up_ = false;
    }

    Step step() const { return step_; }
    Status status() const { return status_; }
    bool done() const { return step_ == STEP_DONE; }
    unsigned int progressMs() const { return count_ ? last_ - start_ : 0; }

    // Results, valid once done().
    bool slotsSwapped() const { return slots_swapped_; }
    float span() const { return span_; }
    float drop() const { return drop_; }
    float turnDegrees() const { return turn_degrees_; }
    bool turnApplied() const { return turn_applied_; }
    // The forward pose's hands are this high: steps 2 and 3 compare to it.
    float forwardHeight() const { return forward_height_; }
    Vec3 forwardHandles() const { return forward_handles_; }
    bool forwardKnown() const { return step_ > STEP_FORWARD; }
    // True when "arms down" came out above the forward pose.
    bool invertedUp() const { return inverted_up_; }

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

    // Feeds one frame of both physical slots; true when the last pose ends.
    bool sample(unsigned int time, const SaberPose& slot0, const SaberPose& slot1) {
        if (step_ == STEP_DONE) return false;
        if (!valid(slot0) || !valid(slot1)) {
            status_ = STATUS_UNTRACKED;
            restart();
            return false;
        }
        if (!matches(slot0, slot1)) {
            status_ = STATUS_POSE;
            restart();
            return false;
        }
        status_ = STATUS_HOLD;
        if (count_ && time == last_) return false;
        if (count_ && (time < last_ || time - last_ > 100 ||
                       !stable(slot0, anchor_[0]) || !stable(slot1, anchor_[1])))
            restart();
        if (!count_) {
            anchor_[0] = slot0;
            anchor_[1] = slot1;
            start_ = time;
        }
        last_ = time;
        ++count_;
        tip_sum_ = tip_sum_ + (slot0.tip + slot1.tip) * 0.5f;
        handle_sum_ = handle_sum_ + (slot0.handle + slot1.handle) * 0.5f;
        direction_sum_ = direction_sum_ + normalized(slot0.tip - slot0.handle) +
                         normalized(slot1.tip - slot1.handle);
        if (progressMs() < 1000 || count_ < 30) return false;
        finishStep(slot0, slot1);
        return step_ == STEP_DONE;
    }

private:
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
    // Looser than the one-pose check: arms held out shake more.
    static bool stable(const SaberPose& p, const SaberPose& anchor) {
        return lengthSquared(p.handle - anchor.handle) <= 0.06f * 0.06f &&
               dot(normalized(p.tip - p.handle),
                   normalized(anchor.tip - anchor.handle)) >= 0.9659258f;
    }

    bool matches(const SaberPose& a, const SaberPose& b) {
        const float separation = std::fabs(a.handle.x - b.handle.x);
        const float height_gap = std::fabs(a.handle.y - b.handle.y);
        const float height = (a.handle.y + b.handle.y) * 0.5f;
        const Vec3 da = normalized(a.tip - a.handle);
        const Vec3 db = normalized(b.tip - b.handle);
        if (step_ == STEP_FORWARD) {
            // Hands apart but in front of the body, level, blades parallel
            // and not pointing up or down.
            return separation >= 0.12f && separation <= 0.9f && height_gap <= 0.2f &&
                   dot(da, db) >= 0.75f && std::fabs(da.y) <= 0.6f &&
                   std::fabs(db.y) <= 0.6f;
        }
        if (step_ == STEP_SIDES) {
            return separation >= 0.9f && separation >= forward_separation_ + 0.45f &&
                   height_gap <= 0.25f && std::fabs(height - forward_height_) <= 0.4f;
        }
        // Arms down: both hands well below the forward pose and back near
        // the body. Hands well above it mean an upside-down camera axis.
        inverted_up_ = height - forward_height_ >= 0.3f;
        return forward_height_ - height >= 0.3f &&
               separation <= span_ * 0.7f;
    }

    void finishStep(const SaberPose& slot0, const SaberPose& slot1) {
        const float inverse = 1.0f / static_cast<float>(count_);
        if (step_ == STEP_FORWARD) {
            forward_tips_ = tip_sum_ * inverse;
            forward_handles_ = handle_sum_ * inverse;
            forward_height_ = forward_handles_.y;
            forward_separation_ = std::fabs(slot0.handle.x - slot1.handle.x);
            // The player faces where the blades point; the camera looks
            // along +z, so a player facing it points along -z.
            const Vec3 direction = direction_sum_;
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
            span_ = std::fabs(slot0.handle.x - slot1.handle.x);
            // The left hand is the one further left (smaller camera x).
            slots_swapped_ = slot0.handle.x > slot1.handle.x;
            step_ = STEP_DOWN;
        } else {
            drop_ = forward_height_ - (slot0.handle.y + slot1.handle.y) * 0.5f;
            step_ = STEP_DONE;
        }
        restart();
    }

    void restart() {
        count_ = start_ = last_ = 0;
        tip_sum_ = handle_sum_ = direction_sum_ = Vec3();
    }

    Step step_;
    Status status_;
    unsigned int count_, start_, last_;
    Vec3 tip_sum_, handle_sum_, direction_sum_;
    SaberPose anchor_[2];
    Vec3 forward_tips_, forward_handles_;
    float forward_separation_, forward_height_;
    float span_, drop_;
    float turn_degrees_;
    bool turn_applied_;
    bool slots_swapped_;
    bool inverted_up_;
};

} // namespace movebeat
#endif
