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
    // During the demo song: pause (Start), restart (Circle), pause again
    // and leave for the track list (Triangle).
    bool pause_test;

    EmulatorDemoOptions() : enabled(false), automatic_cuts(true),
                            tracking_loss_test(false),
                            chart_index(0), capture_at_ms(0),
                            menu_test(false), playlist_test(false),
                            playlist_shelf(1), pause_test(false) {}
};

EmulatorDemoOptions readEmulatorDemoOptions();

// USRDIR/renderer.ini switches for A/B checks on hardware:
// "renderer=software" keeps the PPU rasterizer, "effects=off" draws the RSX
// frame without glow, trails and translucent layers.
struct RenderOptions {
    bool software;
    bool effects;
    // Spheres in the hand colors, blue left and red right, once the hands
    // are known ("hand_colors=off" leaves the hues to libgem).
    bool hand_colors;
    // "background=off": no sky, frames or beat pulse behind the playfield,
    // to check whether the screen's light disturbs sphere tracking.
    bool background;
    // "camera_exposure=N" (40..511) and "camera_quality=F" (0..1) for
    // gemPrepareCamera; defaults 128 and 0.5.
    int camera_exposure;
    float camera_quality;
    // "left_hue=N", "right_hue=N": sphere hue angles (default 204 blue,
    // 352 red); a swap tells whether a color or a side of the room
    // troubles the camera.
    int left_hue;
    int right_hue;
    // "gem_filter=off": no libgem state filter (gemFilterState).
    bool gem_filter;

    RenderOptions() : software(false), effects(true), hand_colors(true),
                      background(true), camera_exposure(128), camera_quality(0.5f),
                      left_hue(204), right_hue(352), gem_filter(true) {}
};

RenderOptions readRenderOptions();

} // namespace ps3
} // namespace movebeat
