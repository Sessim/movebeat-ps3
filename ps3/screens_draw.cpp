#include "screens_draw.hpp"

#include <algorithm>
#include <string.h>

#include <cmath>

#include "menu_motion.hpp"
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

namespace {

std::uint32_t withOpacity(std::uint32_t color, float alpha) {
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    return (static_cast<std::uint32_t>(alpha * 255.0f + 0.5f) << 24) |
           (color & 0x00ffffffu);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Where and how large a carousel row is drawn, by its offset from the
// selection (fractional while scrolling).
struct RowLayout {
    float y;
    float cover;
    float label_scale;
    float alpha;
};

RowLayout carouselRow(float offset, float center_y) {
    const float distance = std::fabs(offset);
    const float near = std::min(1.0f, distance);
    const float beyond = std::max(0.0f, distance - 1.0f);
    RowLayout row;
    // Compact enough that the outer rows clear the header and the hints.
    const float travel = distance <= 1.0f ? distance * 146.0f
                                          : 146.0f + beyond * 64.0f;
    row.y = center_y + (offset < 0.0f ? -travel : travel);
    row.cover = lerp(200.0f, 72.0f, near) * (1.0f - beyond * 0.1f);
    row.label_scale = lerp(1.0f, 0.62f, near) - beyond * 0.06f;
    row.alpha = distance <= 2.2f ? 1.0f
                                 : std::max(0.0f, 1.0f - (distance - 2.2f) / 1.3f);
    return row;
}

// Cover and label of one row, centered as a pair on center_x.
struct RowPlacement {
    float cover_x;
    float cover_y;
    float label_x;
    float label_y;
    float label_width;
    float label_height;
};

RowPlacement placeRow(const RowLayout& row, float center_x) {
    RowPlacement place;
    const float gap = 14.0f * row.label_scale;
    place.label_width = LibraryCard::LABEL_WIDTH * row.label_scale;
    place.label_height = LibraryCard::HEIGHT * row.label_scale;
    place.cover_x = center_x - (row.cover + gap + place.label_width) * 0.5f;
    place.cover_y = row.y - row.cover * 0.5f;
    place.label_x = place.cover_x + row.cover + gap;
    place.label_y = row.y - place.label_height * 0.5f;
    return place;
}

void drawCarouselRow(Framebuffer& screen, const LevelChoice& level,
                     const RowLayout& row, float center_x) {
    const RowPlacement place = placeRow(row, center_x);
    if (level.card.ready()) {
        level.card.drawCover(screen, place.cover_x, place.cover_y, row.cover,
                             row.alpha);
        level.card.drawLabel(screen, place.label_x, place.label_y,
                             row.label_scale, row.alpha);
    } else if (row.alpha > 0.35f) {
        screen.rectangle(static_cast<int>(place.cover_x),
                         static_cast<int>(place.cover_y),
                         static_cast<int>(row.cover), static_cast<int>(row.cover),
                         0xff1c2f4eu);
        drawMenuText(screen, "TRACK", static_cast<int>(place.label_x),
                     static_cast<int>(row.y) - 7, 2, 0xffd9e7ffu);
    }
    if (level.chart_broken && row.alpha > 0.35f)
        drawMenuText(screen, "CHART ERROR", static_cast<int>(place.label_x + 4),
                     static_cast<int>(place.label_y + place.label_height + 4), 2,
                     0xffff6470u);
}

} // namespace

void drawLevelLibrary(Framebuffer& screen,
                      const std::vector<LevelChoice>& levels,
                      const LibraryView& view) {
    const int width = screen.width();
    const int height = screen.height();
    const float center_x = width * 0.5f;
    const float center_y = height * 0.5f - 12.0f;
    drawMenuText(screen, "TRACKS", 40, 70, 4, 0xffd9e7ffu);
    if (levels.empty()) {
        drawMenuText(screen, "NO TRACKS", width / 2 - 90,
                     static_cast<int>(center_y), 3, 0xffffa0a0u);
        return;
    }
    const std::size_t count = levels.size();
    const float nearest = std::floor(view.position + 0.5f);
    const std::size_t selected = CarouselScroll::wrap(nearest, count);
    const int counter_x = 100;
    drawNumber(screen, static_cast<unsigned int>(selected + 1),
               counter_x - 26, 112, 4, 0xffd9e7ffu);
    screen.line(counter_x - 5, 135, counter_x + 5, 110, 0xff7d92b2u, 2);
    drawNumber(screen, static_cast<unsigned int>(count), counter_x + 26, 112, 4,
               0xffd9e7ffu);

    // Up to seven rows plus the ones sliding in, farthest first so the
    // selection is drawn on top. Short libraries show each track once.
    const int reach = count >= 7 ? 4 : static_cast<int>(count / 2);
    for (int ring = reach; ring >= 0; --ring) {
        for (int side = 0; side < (ring == 0 ? 1 : 2); ++side) {
            const float row_index = nearest + static_cast<float>(side == 0 ? ring : -ring);
            const float offset = row_index - view.position;
            if (std::fabs(offset) > 3.6f) continue;
            if (count < 7 && std::fabs(offset) > count * 0.5f) continue;
            const RowLayout row = carouselRow(offset, center_y);
            if (row.alpha <= 0.0f) continue;
            drawCarouselRow(screen, levels[CarouselScroll::wrap(row_index, count)],
                            row, center_x);
        }
    }

    // Halo around the selected cover: steady in the color of the chosen
    // difficulty, flaring briefly when the difficulty changes. It fades
    // while the list is between rows.
    const float between = std::fabs(view.position - nearest);
    const float settle = 1.0f - std::min(1.0f, between * 2.0f);
    const RowLayout selected_row = carouselRow(nearest - view.position, center_y);
    const RowPlacement place = placeRow(selected_row, center_x);
    const LevelChoice& level = levels[selected];
    if (settle > 0.0f) {
        const float x0 = place.cover_x - 4.0f, y0 = place.cover_y - 4.0f;
        const float x1 = place.cover_x + selected_row.cover + 4.0f;
        const float y1 = place.cover_y + selected_row.cover + 4.0f;
        if (screen.effectsEnabled()) {
            const float glow = 16.0f + 26.0f * view.halo_flash;
            const std::uint32_t color = withOpacity(
                view.halo_color, (0.45f + 0.55f * view.halo_flash) * settle);
            screen.glowLine(x0, y0, x1, y0, glow, color);
            screen.glowLine(x1, y0, x1, y1, glow, color);
            screen.glowLine(x1, y1, x0, y1, glow, color);
            screen.glowLine(x0, y1, x0, y0, glow, color);
        }
        const int border = 3 + static_cast<int>(3.0f * view.halo_flash);
        const int ix0 = static_cast<int>(x0), iy0 = static_cast<int>(y0);
        const int ix1 = static_cast<int>(x1), iy1 = static_cast<int>(y1);
        screen.rectangle(ix0 - border, iy0 - border, ix1 - ix0 + 2 * border, border,
                         view.halo_color);
        screen.rectangle(ix0 - border, iy1, ix1 - ix0 + 2 * border, border,
                         view.halo_color);
        screen.rectangle(ix0 - border, iy0, border, iy1 - iy0, view.halo_color);
        screen.rectangle(ix1, iy0, border, iy1 - iy0, view.halo_color);
        // The glow must not wash over the cover itself.
        if (level.card.ready())
            level.card.drawCover(screen, place.cover_x, place.cover_y,
                                 selected_row.cover, 1.0f);
    }

    // Difficulties of the selected track under its label.
    const int chips_y = static_cast<int>(selected_row.y + 58.0f);
    // Clear of the halo around the cover.
    int chip_x = static_cast<int>(place.label_x) + 16;
    for (std::size_t index = 0; index < level.manifest.charts.size() && index < 5;
         ++index) {
        const std::string name = menuLabel(level.manifest.charts[index].difficulty);
        const bool active = index == view.chart_index;
        const std::uint32_t color = active
            ? difficultyColor(level.manifest.charts[index].difficulty)
            : 0xff56607au;
        drawMenuText(screen, name.c_str(), chip_x, chips_y, 2, color);
        if (active)
            screen.rectangle(chip_x, chips_y + 17,
                             static_cast<int>(name.size()) * 12 - 2, 3, color);
        chip_x += static_cast<int>(name.size()) * 12 + 18;
    }
    if (view.preview_playing) {
        // Three bars bouncing with the clock: the preview is audible.
        const int bars_x = static_cast<int>(place.label_x + place.label_width) - 30;
        const int bars_y = static_cast<int>(selected_row.y - 44.0f);
        for (int bar = 0; bar < 3; ++bar) {
            const float wave = std::sin(view.now_ms * 0.012f + bar * 1.9f);
            const int bar_height = 6 + static_cast<int>((wave + 1.0f) * 6.0f);
            screen.rectangle(bars_x + bar * 8, bars_y + 18 - bar_height, 5,
                             bar_height, 0xff70d8ffu);
        }
    }

    drawMenuText(screen, "HOLD T  UP DOWN  SCROLL     LEFT RIGHT  DIFFICULTY",
                 width / 2 - 300, height - 56, 2, 0xff9cb3d3u);
    drawMenuText(screen, "BOTH MOVE  PLAY    ONE MOVE  RANDOM    START  CALIBRATE    SELECT  HANDS",
                 width / 2 - 432, height - 30, 2, 0xff9cb3d3u);
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
