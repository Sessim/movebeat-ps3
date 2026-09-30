#ifndef MOVEBEAT_PS3_CUT_FEEDBACK_HPP
#define MOVEBEAT_PS3_CUT_FEEDBACK_HPP

#include "movebeat/game.hpp"

namespace movebeat {
namespace ps3 {

struct CutBurst {
    Vec3 position;
    Hand hand;
    unsigned int started_ms;
    bool active;

    CutBurst() : hand(HAND_LEFT), started_ms(0), active(false) {}
};

class CutFeedback {
public:
    enum { MAX_BURSTS = 32, DURATION_MS = 180 };

    CutFeedback() : next_(0) {}

    void clear() {
        for (unsigned int i = 0; i < MAX_BURSTS; ++i) bursts_[i].active = false;
        next_ = 0;
    }

    void add(const FrameEvents& events, unsigned int now_ms) {
        for (unsigned int i = 0; i < events.cut_visual_count; ++i) {
            CutBurst& burst = bursts_[next_];
            burst.position = events.cut_visuals[i].position;
            burst.hand = events.cut_visuals[i].hand;
            burst.started_ms = now_ms;
            burst.active = true;
            next_ = (next_ + 1u) % MAX_BURSTS;
        }
    }

    bool visible(unsigned int index, unsigned int now_ms) const {
        return index < MAX_BURSTS && bursts_[index].active &&
               now_ms >= bursts_[index].started_ms &&
               now_ms - bursts_[index].started_ms < DURATION_MS;
    }

    const CutBurst& burst(unsigned int index) const { return bursts_[index]; }

private:
    CutBurst bursts_[MAX_BURSTS];
    unsigned int next_;
};

} // namespace ps3
} // namespace movebeat

#endif
