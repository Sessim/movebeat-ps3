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
    // Stay in the library and replay a scripted navigation run to measure
    // the cost of each step (tools/rpcs3_menu_smoke.ps1).
    bool menu_test;
    // Pick the first playlist, start it with both Move buttons and play it
    // through with scripted cuts (tools/rpcs3_playlist_smoke.ps1).
    bool playlist_test;
    // The playlist shelf the test plays (1 = first playlist by title).
    unsigned int playlist_shelf;

    EmulatorDemoOptions() : enabled(false), automatic_cuts(true),
                            tracking_loss_test(false),
                            chart_index(0), capture_at_ms(0),
                            menu_test(false), playlist_test(false),
                            playlist_shelf(1) {}
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
