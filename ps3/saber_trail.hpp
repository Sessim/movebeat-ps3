#ifndef MOVEBEAT_PS3_SABER_TRAIL_HPP
#define MOVEBEAT_PS3_SABER_TRAIL_HPP

#include "movebeat/types.hpp"

namespace movebeat {
namespace ps3 {

// One remembered blade position: the swept part runs from base to tip.
struct TrailSample {
    Vec3 base;
    Vec3 tip;
    unsigned int time_ms;
};

// Short history of one saber for the swing trail. Length is defined in
// milliseconds, not frames, so the trail looks the same at 30 and 60 fps;
// the fixed capacity bounds the geometry to a few quads per saber.
class SaberTrail {
public:
    static const unsigned int kCapacity = 12;
    static const unsigned int kLifetimeMs = 120;
    // Fast real swings move the tip at roughly 10-15 m/s. Anything faster
    // is a tracking jump (or a scripted pose change) and restarts the trail
    // instead of drawing a streak across the screen.
    static float maxTipSpeedMetersPerSecond() { return 18.0f; }
    // The trail covers the outer part of the blade, as a swing would.
    static float baseFraction() { return 0.45f; }

    SaberTrail() : head_(0), count_(0) {}

    void clear() { count_ = 0; }

    // Records the current pose; an untracked saber erases the trail so a
    // tracking gap never draws a streak across the screen.
    void add(const SaberPose& pose, unsigned int now_ms) {
        if (!pose.tracked) {
            clear();
            return;
        }
        if (count_ > 0u) {
            const TrailSample& newest = sample(0);
            const float dx = pose.tip.x - newest.tip.x;
            const float dy = pose.tip.y - newest.tip.y;
            const float dz = pose.tip.z - newest.tip.z;
            // Treat very short intervals as one 60 Hz frame so a duplicate
            // timestamp cannot turn a small move into an "infinite" speed.
            unsigned int interval_ms = age(newest, now_ms);
            if (interval_ms < 16u) interval_ms = 16u;
            const float limit = maxTipSpeedMetersPerSecond() *
                                interval_ms / 1000.0f;
            if (dx * dx + dy * dy + dz * dz > limit * limit) clear();
        }
        head_ = (head_ + kCapacity - 1u) % kCapacity;
        TrailSample& sample = samples_[head_];
        const float t = baseFraction();
        sample.base = Vec3(pose.handle.x + (pose.tip.x - pose.handle.x) * t,
                           pose.handle.y + (pose.tip.y - pose.handle.y) * t,
                           pose.handle.z + (pose.tip.z - pose.handle.z) * t);
        sample.tip = pose.tip;
        sample.time_ms = now_ms;
        if (count_ < kCapacity) ++count_;
        prune(now_ms);
    }

    // Drops samples older than the lifetime.
    void prune(unsigned int now_ms) {
        while (count_ > 1u && age(sample(count_ - 1u), now_ms) > kLifetimeMs)
            --count_;
    }

    unsigned int size() const { return count_; }
    // index 0 is the newest sample.
    const TrailSample& sample(unsigned int index) const {
        return samples_[(head_ + index) % kCapacity];
    }

    static unsigned int age(const TrailSample& sample, unsigned int now_ms) {
        return now_ms >= sample.time_ms ? now_ms - sample.time_ms : 0u;
    }

    // 1 for a fresh sample, 0 at the end of its lifetime.
    static float fade(const TrailSample& sample, unsigned int now_ms) {
        const unsigned int sample_age = age(sample, now_ms);
        if (sample_age >= kLifetimeMs) return 0.0f;
        return 1.0f - static_cast<float>(sample_age) / kLifetimeMs;
    }

private:
    TrailSample samples_[kCapacity];
    unsigned int head_;
    unsigned int count_;
};

} // namespace ps3
} // namespace movebeat

#endif
