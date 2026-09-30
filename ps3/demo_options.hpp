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

// True when USRDIR/renderer.ini contains "renderer=software": keeps the PPU
// rasterizer for A/B checks on hardware or if the RSX path misbehaves.
bool softwareRendererRequested();

} // namespace ps3
} // namespace movebeat
