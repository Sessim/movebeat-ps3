#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "framebuffer.hpp"
#include "gem_runtime.hpp"
#include "level_library.hpp"
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
};

// Seven-row vertical carousel with the selection in the middle.
void drawLevelLibrary(Framebuffer& screen,
                      const std::vector<LevelChoice>& levels,
                      const LibraryView& view);
void drawMovePointer(Framebuffer& screen, const ScreenPointer& pointer,
                     u32 color);
void drawResults(Framebuffer& screen, const movebeat::ScoreState& score,
                 const movebeat::HighScore& best);
void drawAudioError(Framebuffer& screen, int error);
void drawTimingOffset(Framebuffer& screen, int offset_ms);
void drawBladeLength(Framebuffer& screen, int blade_length_cm);
void drawTrackingPauseOverlay(Framebuffer& screen, bool both_moves_tracked,
                              bool recalibrating, bool hands_separated,
                              unsigned int progress_ms);
void drawCalibrationOverlay(Framebuffer& screen, bool both_tracked,
                            bool hands_separated, unsigned int progress_ms);
void drawCaptureIndicator(Framebuffer& screen);
void drawChartPips(Framebuffer& screen, std::size_t chart_count,
                   std::size_t selected_chart);
void drawHandMode(Framebuffer& screen, bool invert_hands);
void drawRunStateIndicator(Framebuffer& screen, u32 color);

} // namespace ps3
} // namespace movebeat
