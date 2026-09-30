#include "screens_draw.hpp"

#include <algorithm>
#include <string.h>

#include "text_draw.hpp"

namespace movebeat {
namespace ps3 {
namespace {

const char* startupFailureHint(GemRuntime::InitStage stage,
                               int error) {
    if (stage == GemRuntime::INIT_CAMERA_TYPE && error == -1003)
        return "PS EYE NOT FOUND";
    if (stage >= GemRuntime::INIT_CAMERA &&
        stage <= GemRuntime::INIT_CAMERA_START)
        return "CHECK PS EYE USB";
    if (stage == GemRuntime::INIT_CAMERA_MODULE ||
        stage == GemRuntime::INIT_GEM_MODULE)
        return "SYSTEM MODULE ERROR";
    return "MOVE ENGINE ERROR";
}

} // namespace

void drawConnection(Framebuffer& screen, int index,
                    bool connected, bool tracked, u32 color) {
    const int size = screen.height() / 30;
    const int x = screen.width() / 40 + index * (size + screen.width() / 80);
    const int y = screen.height() / 30;
    const u32 status_color = tracked ? color : (connected ? 0xffffa020u : 0xff502020u);
    screen.rectangle(x, y, size, size, status_color);
}

void drawLevelLibrary(Framebuffer& screen,
                      const std::vector<LevelChoice>& levels,
                      std::size_t selected, std::size_t chart_index) {
    const int center_x = screen.width() / 2;
    const int card_x = center_x - LibraryCard::WIDTH / 2;
    const int center_y = screen.height() / 2 - 55;
    drawMenuText(screen, "TRACKS", center_x - 72, 46, 4, 0xffd9e7ffu);
    if (levels.empty()) {
        drawMenuText(screen, "NO TRACKS", center_x - 90, center_y, 3,
                     0xffffa0a0u);
        return;
    }
    const int counter_x = center_x + 210;
    drawNumber(screen, static_cast<unsigned int>(selected + 1),
               counter_x - 22, 54, 4, 0xffd9e7ffu);
    screen.line(counter_x - 5, 77, counter_x + 5, 52, 0xff7d92b2u, 2);
    drawNumber(screen, static_cast<unsigned int>(levels.size()),
               counter_x + 22, 54, 4, 0xffd9e7ffu);
    const int offsets[] = {-1, 0, 1};
    for (int slot = 0; slot < 3; ++slot) {
        if (levels.size() == 1 && offsets[slot] != 0) continue;
        if (levels.size() == 2 && offsets[slot] < 0) continue;
        const std::size_t index = offsets[slot] < 0
            ? (selected == 0 ? levels.size() - 1 : selected - 1)
            : (selected + static_cast<std::size_t>(offsets[slot])) % levels.size();
        const int y = center_y + offsets[slot] * 116;
        const bool active = offsets[slot] == 0;
        screen.rectangle(card_x - 5, y - 5, 522, 106,
                         active ? 0xff54d8fcu : 0xff303950u);
        if (levels[index].card.ready())
            levels[index].card.draw(screen, card_x, y);
        else {
            screen.rectangle(card_x, y, 512, 96, 0xff121a2eu);
            drawMenuText(screen, "TRACK", card_x + 115, y + 31, 3,
                         0xffd9e7ffu);
        }
    }
    drawMenuText(screen, "TRI PREV  X NEXT  MOVE T SELECT", center_x - 270,
                 screen.height() - 108, 2, 0xff9cb3d3u);
    drawMenuText(screen, "SQ PREV  O NEXT DIFF", center_x - 228,
                 screen.height() - 82, 2, 0xff9cb3d3u);
    drawMenuText(screen, "START CALIBRATE", center_x - 168,
                 screen.height() - 56, 2, 0xff9cb3d3u);
    drawMenuText(screen, "SELECT SWAP HANDS", center_x - 183,
                 screen.height() - 30, 2, 0xff9cb3d3u);
    if (!levels[selected].manifest.charts.empty()) {
        const int count = static_cast<int>(levels[selected].manifest.charts.size());
        for (int index = 0; index < count && index < 20; ++index)
            screen.rectangle(center_x + (index - count / 2) * 18,
                             center_y + 102, 11, 7,
                             static_cast<std::size_t>(index) == chart_index
                                 ? 0xfff6e79au : 0xff4a5574u);
        if (chart_index < levels[selected].manifest.charts.size()) {
            const movebeat::LevelChart& chart =
                levels[selected].manifest.charts[chart_index];
            const int label_x = card_x + LibraryCard::WIDTH + 22;
            drawMenuText(screen, "DIFFICULTY", label_x, center_y + 7,
                         2, 0xff96a9ccu);
            drawMenuText(screen, menuLabel(chart.characteristic).c_str(),
                         label_x, center_y + 36, 2, 0xffb9c9e5u);
            drawMenuText(screen, menuLabel(chart.difficulty).c_str(),
                         label_x, center_y + 61, 3, 0xfff6e79au);
        }
    }
}

void drawMovePointer(Framebuffer& screen,
                     const ScreenPointer& pointer,
                     u32 color) {
    if (!pointer.valid) return;
    screen.circle(pointer.x, pointer.y, 11, color);
    screen.circle(pointer.x, pointer.y, 7, 0xff080b16u);
    screen.circle(pointer.x, pointer.y, 2, color);
    screen.line(pointer.x - 16, pointer.y, pointer.x - 10, pointer.y,
                color, 2);
    screen.line(pointer.x + 10, pointer.y, pointer.x + 16, pointer.y,
                color, 2);
    screen.line(pointer.x, pointer.y - 16, pointer.x, pointer.y - 10,
                color, 2);
    screen.line(pointer.x, pointer.y + 10, pointer.x, pointer.y + 16,
                color, 2);
}

void drawResults(Framebuffer& screen,
                 const movebeat::ScoreState& score,
                 const movebeat::HighScore& best) {
    const int large = screen.height() / 42 + 2;
    const int small = screen.height() / 70 + 2;
    const u32 score_color = score.failed ? 0xffff5060u : 0xfff0d050u;
    drawNumber(screen, score.score, screen.width() / 2,
               screen.height() / 4, large, score_color);
    drawNumber(screen, best.score, screen.width() / 2,
               screen.height() / 10, small, 0xffa080ffu);
    drawNumber(screen, score.hits, screen.width() / 3,
               screen.height() * 3 / 5, small, 0xff40c8ffu);
    drawNumber(screen, score.misses, screen.width() * 2 / 3,
               screen.height() * 3 / 5, small, 0xffff5060u);
    const int underline_y = screen.height() * 3 / 5 + small * 6;
    screen.rectangle(screen.width() / 4, underline_y,
                     screen.width() / 6, small / 2 + 1, 0xff40c8ffu);
    screen.rectangle(screen.width() * 7 / 12, underline_y,
                     screen.width() / 6, small / 2 + 1, 0xffff5060u);
}

void drawAudioError(Framebuffer& screen, int error) {
    const int center_x = screen.width() / 2;
    const int center_y = screen.height() / 2;
    screen.rectangle(center_x - 330, center_y - 120, 660, 245, 0xff411018u);
    screen.rectangle(center_x - 325, center_y - 115, 650, 235, 0xff200b11u);
    drawMenuText(screen, "AUDIO ERROR", center_x - 155, center_y - 84,
                 4, 0xffff6470u);
    drawHexNumber(screen, static_cast<unsigned int>(error), center_x,
                  center_y - 8, screen.height() / 75 + 2, 0xffffd070u);
    drawMenuText(screen, "TRI BACK TO TRACKS", center_x - 187,
                 center_y + 76, 2, 0xffd9e7ffu);
}

void drawTimingOffset(Framebuffer& screen, int offset_ms) {
    const unsigned int magnitude = static_cast<unsigned int>(
        offset_ms < 0 ? -offset_ms : offset_ms);
    const u32 color = offset_ms > 0 ? 0xff50e080u :
        (offset_ms < 0 ? 0xffff5060u : 0xffe0e0e8u);
    drawNumber(screen, magnitude, screen.width() / 2,
               screen.height() * 6 / 7, screen.height() / 90 + 2, color);
}

void drawBladeLength(Framebuffer& screen, int blade_length_cm) {
    drawNumber(screen, static_cast<unsigned int>(blade_length_cm),
               screen.width() / 2, screen.height() * 6 / 7,
               screen.height() / 90 + 2, 0xff50d8f0u);
}

void drawStartupFailure(Framebuffer& screen, GemRuntime::InitStage stage,
                        int error) {
    screen.rectangle(screen.width() / 7, screen.height() / 4,
                     screen.width() * 5 / 7, screen.height() * 11 / 20,
                     0xffb02030u);
    const int label_scale = std::max(2, screen.height() / 240);
    drawCenteredMenuText(screen, "MOVE START FAILED",
                         screen.height() * 28 / 100, label_scale,
                         0xffffffffu);
    drawCenteredMenuText(screen, startupFailureHint(stage, error),
                         screen.height() * 35 / 100, label_scale,
                         0xffffd070u);
    drawCenteredMenuText(screen, "STAGE",
                         screen.height() * 44 / 100, 2,
                         0xffffffffu);
    drawNumber(screen, static_cast<unsigned int>(stage),
               screen.width() / 2, screen.height() * 48 / 100,
               screen.height() / 45, 0xffffffffu);
    drawCenteredMenuText(screen, "ERROR CODE",
                         screen.height() * 62 / 100, 2,
                         0xffffffffu);
    drawHexNumber(screen, static_cast<unsigned int>(error),
                  screen.width() / 2, screen.height() * 66 / 100,
                  screen.height() / 55, 0xffffd070u);
}

void drawTrackingPauseOverlay(Framebuffer& screen, bool both_moves_tracked,
                              bool recalibrating, bool hands_separated,
                              unsigned int progress_ms) {
    const int width = screen.width() / 2;
    const int x = (screen.width() - width) / 2;
    const int y = screen.height() / 3;
    screen.rectangle(x - 12, y - 20, width + 24, 194, 0xff111b30u);
    const char* message = !both_moves_tracked ? "TRACKING LOST" :
        (!recalibrating ? "START RECALIBRATE" :
         (!hands_separated ? "SEPARATE MOVE HANDS" :
          "HOLD BOTH MOVE STILL"));
    drawMenuText(screen, message, x + 25, y, 3,
                 both_moves_tracked ? 0xffd9e7ffu : 0xffffad8bu);
    drawMenuText(screen, !both_moves_tracked ? "SHOW BOTH MOVE" :
                 (recalibrating ? "RETURN TO START POSE" :
                  "PRESS START"), x + 25, y + 50, 2, 0xff9cb3d3u);
    drawMenuText(screen, "HANDS APART - BUTTONS UP - SABERS FORWARD",
                 x + 25, y + 77, 2, 0xff9cb3d3u);
    screen.rectangle(x + 25, y + 108, width - 50, 14, 0xff303b56u);
    if (recalibrating && hands_separated) {
        const unsigned int elapsed = std::min(1000u, progress_ms);
        screen.rectangle(x + 25, y + 108,
                         (width - 50) * elapsed / 1000, 14,
                         0xff40e890u);
    }
}

void drawCalibrationOverlay(Framebuffer& screen, bool both_tracked,
                            bool hands_separated, unsigned int progress_ms) {
    const int width = screen.width() / 2;
    const int x = (screen.width() - width) / 2;
    const int y = screen.height() / 8;
    const unsigned int elapsed = std::min(1000u, progress_ms);
    screen.rectangle(x - 12, y - 18, width + 24, 122, 0xff111b30u);
    drawMenuText(screen, !both_tracked ? "TRACK BOTH SPHERES" :
                 (!hands_separated ? "SEPARATE MOVE HANDS" :
                  "HOLD BOTH MOVE STILL"), x + 12, y, 3,
                 hands_separated ? 0xffd9e7ffu : 0xffffad8bu);
    drawMenuText(screen, "HANDS APART - BUTTONS UP - SABERS FORWARD",
                 x + 12, y + 27, 2, 0xff9cb3d3u);
    screen.rectangle(x, y + 52, width, 14, 0xff303b56u);
    screen.rectangle(x, y + 52, width * elapsed / 1000, 14,
                     hands_separated ? 0xff40e890u : 0xffffad8bu);
    drawMenuText(screen, "TRI CANCEL", x + width / 2 - 65,
                 y + 80, 2, 0xff9cb3d3u);
}

void drawCaptureIndicator(Framebuffer& screen) {
    const int capture_size = screen.height() / 42 + 4;
    screen.rectangle(screen.width() - capture_size * 3,
                     screen.height() / 30, capture_size, capture_size,
                     0xffe040d0u);
}

void drawChartPips(Framebuffer& screen, std::size_t chart_count,
                   std::size_t selected_chart) {
    const int pip = screen.height() / 70 + 4;
    const int gap = pip / 2;
    const int total = static_cast<int>(chart_count) * (pip + gap);
    const int start_x = screen.width() / 2 - total / 2;
    for (std::size_t index = 0; index < chart_count; ++index) {
        screen.rectangle(start_x + static_cast<int>(index) * (pip + gap),
                         screen.height() / 30, pip, pip,
                         index == selected_chart
                             ? 0xfff0f0f0u : 0xff404050u);
    }
}

void drawHandMode(Framebuffer& screen, bool invert_hands) {
    drawMenuText(screen, invert_hands ? "HANDS FLIPPED" :
                 "HANDS AUTO", 24, screen.height() - 30, 2,
                 invert_hands ? 0xffffb068u : 0xff80d8c0u);
}

void drawRunStateIndicator(Framebuffer& screen, u32 color) {
    screen.rectangle(screen.width() - screen.width() / 20, screen.height() / 30,
                     screen.width() / 40, screen.height() / 30, color);
}

} // namespace ps3
} // namespace movebeat
