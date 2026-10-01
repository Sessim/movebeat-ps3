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
    // Song time for one extra saved frame (demo_timed_frame.ppm); 0 = off.
    unsigned int capture_at_ms;

    EmulatorDemoOptions() : enabled(false), automatic_cuts(true),
                            tracking_loss_test(false),
                            chart_index(0), capture_at_ms(0) {}
};

EmulatorDemoOptions readEmulatorDemoOptions();

// USRDIR/renderer.ini switches for A/B checks on hardware:
// "renderer=software" keeps the PPU rasterizer, "effects=off" draws the RSX
// frame without glow, trails and translucent layers.
struct RenderOptions {
    bool software;
    bool effects;

    RenderOptions() : software(false), effects(true) {}
};

RenderOptions readRenderOptions();

} // namespace ps3
} // namespace movebeat
