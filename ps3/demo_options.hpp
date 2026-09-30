#pragma once

#include <string>

namespace movebeat {
namespace ps3 {

// Local-only RPCS3 preview switches read from USRDIR/demo_mode.ini.
struct EmulatorDemoOptions {
    bool enabled;
    bool automatic_cuts;
    bool tracking_loss_test;
    std::string level_id;
    unsigned int chart_index;

    EmulatorDemoOptions() : enabled(false), automatic_cuts(true),
                            tracking_loss_test(false),
                            chart_index(0) {}
};

EmulatorDemoOptions readEmulatorDemoOptions();

} // namespace ps3
} // namespace movebeat
