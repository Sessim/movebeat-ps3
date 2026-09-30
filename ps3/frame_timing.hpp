#ifndef MOVEBEAT_PS3_FRAME_TIMING_HPP
#define MOVEBEAT_PS3_FRAME_TIMING_HPP

namespace movebeat {
namespace ps3 {

struct FrameTimingSample {
    unsigned int count;
    unsigned int average_us;
    unsigned int maximum_us;
    unsigned int over_60hz_budget;

    FrameTimingSample()
        : count(0), average_us(0), maximum_us(0), over_60hz_budget(0) {}
};

class FrameTimingWindow {
public:
    FrameTimingWindow() : total_us_(0), maximum_us_(0), count_(0), over_budget_(0) {}

    void add(unsigned int elapsed_us) {
        total_us_ += elapsed_us;
        if (elapsed_us > maximum_us_) maximum_us_ = elapsed_us;
        if (elapsed_us > 16667u) ++over_budget_;
        ++count_;
    }

    FrameTimingSample take() {
        FrameTimingSample result;
        result.count = count_;
        result.average_us = count_ ? static_cast<unsigned int>(total_us_ / count_) : 0;
        result.maximum_us = maximum_us_;
        result.over_60hz_budget = over_budget_;
        total_us_ = 0;
        maximum_us_ = 0;
        count_ = 0;
        over_budget_ = 0;
        return result;
    }

private:
    unsigned long long total_us_;
    unsigned int maximum_us_;
    unsigned int count_;
    unsigned int over_budget_;
};

} // namespace ps3
} // namespace movebeat

#endif
