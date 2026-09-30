#ifndef MOVEBEAT_CALIBRATION_HPP
#define MOVEBEAT_CALIBRATION_HPP

#include "movebeat/types.hpp"

namespace movebeat {

// A deliberate stable pose, sampled for at least one second. Hardware axis
// calibration remains the responsibility of the input adapter.
class PoseCalibration {
public:
    PoseCalibration() { reset(); }
    void reset() { count_ = 0; start_ = last_ = 0; sum_ = Vec3(); }
    unsigned int progressMs() const { return count_ ? last_ - start_ : 0; }
    Vec3 origin() const { return count_ ? sum_ * (1.0f / count_) : Vec3(); }

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
    Vec3 sum_;
    SaberPose anchor_left_, anchor_right_;
};

} // namespace movebeat
#endif
