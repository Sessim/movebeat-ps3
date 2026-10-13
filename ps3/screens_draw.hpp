#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "framebuffer.hpp"
#include "gem_runtime.hpp"
#include "level_library.hpp"
#include "library_shelf.hpp"
#include "menu_pointer.hpp"
#include "movebeat/game.hpp"
#include "movebeat/records.hpp"

namespace movebeat {
namespace ps3 {

// Menu screens and overlays drawn on top of or instead of the playfield.
void drawStartupFailure(Framebuffer& screen, GemRuntime::InitStage stage,
                        int error);
void drawConnection(Framebuffer& screen, int index,
                    bool connected, bool tracked, u32 color);
// What the library screen shows besides the tracks themselves.
struct LibraryView {
    float position;               // continuous carousel position
    std::size_t chart_index;      // selected difficulty of the selection
    std::uint32_t halo_color;     // difficulty color around the cover
    float halo_flash;             // 1 right after a difficulty change, then 0
    bool preview_playing;
    unsigned int now_ms;
    // The shelf shown: all tracks (playlist 0) or one playlist.
    const PlaylistChoice* playlist;
    std::size_t shelf;
    std::size_t shelf_count;

    LibraryView() : position(0.0f), chart_index(0), halo_color(0),
                    halo_flash(0.0f), preview_playing(false), now_ms(0),
                    playlist(0), shelf(0), shelf_count(1) {}
};

// Seven-row vertical carousel with the selection in the middle; `order`
// lists the library indices of the shelf in carousel order.
void drawLevelLibrary(Framebuffer& screen,
                      const std::vector<LevelChoice>& levels,
                      const std::vector<std::size_t>& order,
                      const LibraryView& view);
void drawMovePointer(Framebuffer& screen, const ScreenPointer& pointer,
                     u32 color);
void drawResults(Framebuffer& screen, const movebeat::ScoreState& score,
                 const movebeat::HighScore& best);
// Under the results of a playlist song: which song it was, the next one
// and the seconds until it starts.
void drawPlaylistNext(Framebuffer& screen, const PlaylistRun& run,
                      const LevelChoice& next, unsigned int seconds_left);
// After the last song of a playlist run: totals and the latest songs.
void drawPlaylistSummary(Framebuffer& screen, const PlaylistRun& run,
                         const PlaylistChoice& playlist,
                         const std::vector<LevelChoice>& levels);
// Start during a song: continue, restart, calibrate or back to the
// track list.
void drawPauseMenu(Framebuffer& screen);
// Under the results of a single song: continue or replay.
void drawResultsHint(Framebuffer& screen);
void drawAudioError(Framebuffer& screen, int error);
void drawTimingOffset(Framebuffer& screen, int offset_ms);
void drawBladeLength(Framebuffer& screen, int blade_length_cm);
void drawTrackingPauseOverlay(Framebuffer& screen, bool both_moves_tracked,
                              bool recalibrating, bool hands_separated,
                              unsigned int progress_ms);
// The guided calibration: which pose (step 0 forward, 1 sides, 2 down),
// each controller's progress (0 waiting for its Move button, 1 waiting for
// a steady sphere, 2 measuring, 3 done) and where the two hands are now
// (meters relative to the forward pose, x right, y up).
struct CalibrationView {
    int step;
    // The forward pose was refused: the blades pointed different ways.
    bool directions_disagree;
    int hand_state[2];
    bool hand_visible[2];
    float hand_x[2];
    float hand_y[2];

    CalibrationView() : step(0), directions_disagree(false) {
        for (int slot = 0; slot < 2; ++slot) {
            hand_state[slot] = 0;
            hand_visible[slot] = false;
            hand_x[slot] = hand_y[slot] = 0.0f;
        }
    }
};

void drawCalibrationOverlay(Framebuffer& screen, const CalibrationView& view);
void drawCaptureIndicator(Framebuffer& screen);
// USB bundles being copied to the hard disk (`copied` of `total`), or,
// with `total` 0, how many the finished copy added.
void drawUsbImport(Framebuffer& screen, unsigned int copied, unsigned int total);
void drawChartPips(Framebuffer& screen, std::size_t chart_count,
                   std::size_t selected_chart);
void drawHandMode(Framebuffer& screen, bool invert_hands);

} // namespace ps3
} // namespace movebeat
