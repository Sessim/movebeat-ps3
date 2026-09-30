#ifndef MOVEBEAT_PS3_TRACKING_PAUSE_HPP
#define MOVEBEAT_PS3_TRACKING_PAUSE_HPP

namespace movebeat {
namespace ps3 {

class TrackingPauseGate {
public:
    enum { LOSS_THRESHOLD_MS = 250 };

    TrackingPauseGate() : losing_(false), active_(false), lost_since_ms_(0) {}

    void reset() {
        losing_ = false;
        active_ = false;
        lost_since_ms_ = 0;
    }

    // Returns true once when sustained loss first requires a pause.
    bool observe(unsigned int now_ms, bool both_tracked) {
        if (active_) return false;
        if (both_tracked) {
            losing_ = false;
            return false;
        }
        if (!losing_ || now_ms < lost_since_ms_) {
            losing_ = true;
            lost_since_ms_ = now_ms;
            return false;
        }
        if (now_ms - lost_since_ms_ < LOSS_THRESHOLD_MS) return false;
        active_ = true;
        return true;
    }

private:
    bool losing_;
    bool active_;
    unsigned int lost_since_ms_;
};

inline unsigned int unpausedElapsedMs(unsigned long long now_us,
                                      unsigned long long start_us,
                                      unsigned long long pause_started_us,
                                      bool paused) {
    const unsigned long long endpoint = paused ? pause_started_us : now_us;
    return endpoint >= start_us
        ? static_cast<unsigned int>((endpoint - start_us) / 1000ull) : 0u;
}

} // namespace ps3
} // namespace movebeat

#endif
