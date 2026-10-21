#include "screens_draw.hpp"

#include <algorithm>
#include <stdio.h>
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
        return tr("PS EYE NOT FOUND", "PS EYE НЕ НАЙДЕНА");
    if (stage >= GemRuntime::INIT_CAMERA &&
        stage <= GemRuntime::INIT_CAMERA_START)
        return tr("CHECK PS EYE USB", "ПРОВЕРЬТЕ USB PS EYE");
    if (stage == GemRuntime::INIT_CAMERA_MODULE ||
        stage == GemRuntime::INIT_GEM_MODULE)
        return tr("SYSTEM MODULE ERROR", "ОШИБКА СИСТЕМНОГО МОДУЛЯ");
    return tr("MOVE ENGINE ERROR", "ОШИБКА ДВИЖКА MOVE");
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
        drawMenuText(screen, tr("TRACK", "ТРЕК"), static_cast<int>(place.label_x),
                     static_cast<int>(row.y) - 7, 2, 0xffd9e7ffu);
    }
    // Best scores right of the label, one line per finished difficulty in
    // its color, easiest on top.
    if (row.alpha > 0.99f) {
        std::size_t records = 0;
        for (std::size_t index = 0; index < level.best_scores.size(); ++index)
            if (level.best_scores[index]) ++records;
        if (records) {
            const int scale = row.label_scale > 0.8f ? 4 : 3;
            const int line = scale * 8;
            const int x = static_cast<int>(place.label_x + place.label_width) + scale * 6;
            int y = static_cast<int>(row.y) - (static_cast<int>(records) * line - scale * 3) / 2;
            for (std::size_t index = 0; index < level.best_scores.size() &&
                                        index < level.manifest.charts.size(); ++index) {
                const unsigned int score = level.best_scores[index];
                if (!score) continue;
                char text[16];
                const int digits = snprintf(text, sizeof(text), "%u", score);
                drawNumber(screen, score, x + (digits * scale * 4 - scale) / 2, y, scale,
                           difficultyColor(level.manifest.charts[index].difficulty));
                y += line;
            }
        }
    }
    if (level.chart_broken && row.alpha > 0.35f)
        drawMenuText(screen, tr("CHART ERROR", "ОШИБКА КАРТЫ"),
                     static_cast<int>(place.label_x + 4),
                     static_cast<int>(place.label_y + place.label_height + 4), 2,
                     0xffff6470u);
}

} // namespace

// Lines of a hint group are 24 px apart; "what" sits level with their
// middle, after the widest "how" of the column. A right column ends at x.
template <typename Group>
void drawHintGroups(Framebuffer& screen, const Group* groups, int count, int x,
                    bool right_aligned) {
    const int scale = 2;
    const int gap = 24;
    int how_width = 0;
    int what_width = 0;
    for (int index = 0; index < count; ++index) {
        for (int line = 0; line < 2; ++line)
            if (groups[index].how[line])
                how_width = std::max(how_width, menuTextWidth(groups[index].how[line], scale));
        what_width = std::max(what_width, menuTextWidth(groups[index].what, scale));
    }
    const int start = right_aligned ? x - (how_width + gap + what_width) : x;
    for (int index = 0; index < count; ++index) {
        const Group& group = groups[index];
        const int lines = group.how[1] ? 2 : 1;
        for (int line = 0; line < lines; ++line)
            drawMenuText(screen, group.how[line], start, group.y + line * 24 +
                         (lines == 1 ? 12 : 0), scale, 0xff9cb3d3u);
        drawMenuText(screen, group.what, start + how_width + gap, group.y + 12, scale,
                     0xffd9e7ffu);
    }
}

void drawLevelLibrary(Framebuffer& screen,
                      const std::vector<LevelChoice>& levels,
                      const std::vector<std::size_t>& order,
                      const LibraryView& view) {
    const int width = screen.width();
    const int height = screen.height();
    const float center_x = width * 0.5f;
    const float center_y = height * 0.5f - 12.0f;
    // Header: TRACKS, or the playlist's own card (its title may be any
    // script, so it comes pre-rendered from the PC like the track cards).
    int counter_y = 112;
    if (!view.playlist) {
        drawMenuText(screen, tr("TRACKS", "ТРЕКИ"), 40, 70, 4, 0xffd9e7ffu);
    } else if (view.playlist->card.ready()) {
        view.playlist->card.drawCover(screen, 40.0f, 34.0f, 72.0f, 1.0f);
        view.playlist->card.drawLabel(screen, 120.0f, 34.0f, 0.75f, 1.0f);
        counter_y = 124;
    } else {
        drawMenuText(screen, tr("PLAYLIST", "ПЛЕЙЛИСТ"), 40, 70, 4, 0xffd9e7ffu);
    }
    // One mark per shelf (all tracks, then each playlist), Select steps.
    if (view.shelf_count > 1) {
        const std::size_t marks = std::min<std::size_t>(view.shelf_count, 16);
        for (std::size_t mark = 0; mark < marks; ++mark) {
            const bool current = mark == view.shelf;
            screen.rectangle(40 + static_cast<int>(mark) * 18, counter_y + 40,
                             current ? 14 : 10, 5,
                             current ? 0xff70d8ffu : 0xff3a4766u);
        }
    }
    if (order.empty()) {
        drawCenteredMenuText(screen, tr("NO TRACKS", "НЕТ ТРЕКОВ"),
                             static_cast<int>(center_y), 3, 0xffffa0a0u);
        return;
    }
    const std::size_t count = order.size();
    const float nearest = std::floor(view.position + 0.5f);
    const std::size_t selected = CarouselScroll::wrap(nearest, count);
    const int counter_x = 100;
    drawNumber(screen, static_cast<unsigned int>(selected + 1),
               counter_x - 26, counter_y, 4, 0xffd9e7ffu);
    screen.line(counter_x - 5, counter_y + 23, counter_x + 5, counter_y - 2,
                0xff7d92b2u, 2);
    drawNumber(screen, static_cast<unsigned int>(count), counter_x + 26, counter_y, 4,
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
            RowLayout row = carouselRow(offset, center_y);
            // Rows below fade out before they reach the control hints.
            if (offset > 2.0f) row.alpha *= std::max(0.0f, 1.0f - (offset - 2.0f) / 0.6f);
            if (row.alpha <= 0.0f) continue;
            drawCarouselRow(screen, levels[order[CarouselScroll::wrap(row_index, count)]],
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
    const LevelChoice& level = levels[order[selected]];
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
        const std::string name = difficultyLabel(level.manifest.charts[index].difficulty);
        const int name_width = menuTextWidth(name.c_str(), 2);
        const bool active = index == view.chart_index;
        const std::uint32_t color = active
            ? difficultyColor(level.manifest.charts[index].difficulty)
            : 0xff56607au;
        drawMenuText(screen, name.c_str(), chip_x, chips_y, 2, color);
        if (active)
            screen.rectangle(chip_x, chips_y + 17, name_width - 2, 3, color);
        chip_x += name_width + 18;
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

    // Controls, in groups: the ways to do something (one or two lines),
    // then what it does; Move to play in the middle, larger.
    struct HintGroup {
        const char* how[2];
        const char* what;
        int y;
    };
    const HintGroup left[2] = {
        {{tr("HOLD " ICON_T " UP-DOWN", "ЗАЖМИ " ICON_T " ВВЕРХ-ВНИЗ"),
          ICON_TRIANGLE " " ICON_CIRCLE}, tr("SCROLL TRACKS", "ЛИСТАТЬ ТРЕКИ"), height - 112},
        {{tr("HOLD " ICON_T " LEFT-RIGHT", "ЗАЖМИ " ICON_T " ВЛЕВО-ВПРАВО"), ICON_CROSS},
         tr("DIFFICULTY", "ВЫБОР СЛОЖНОСТИ"), height - 58}
    };
    const HintGroup right[2] = {
        {{tr("HOLD " ICON_MOVE ICON_MOVE, "ЗАЖМИ " ICON_MOVE ICON_MOVE), 0},
         tr("RANDOM TRACK", "СЛУЧАЙНЫЙ ТРЕК"), height - 112},
        {{ICON_SQUARE, 0}, tr("PLAYLISTS", "ПЛЕЙЛИСТЫ"), height - 58}
    };
    const int right_groups = view.shelf_count > 1 ? 2 : 1;
    drawHintGroups(screen, left, 2, 40, false);
    drawHintGroups(screen, right, right_groups, width - 40, true);
    drawCenteredMenuText(screen, tr(ICON_MOVE "  PLAY", ICON_MOVE "  ИГРАТЬ"),
                         height - 82, 3, 0xffd9e7ffu);
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
    const int row_y = screen.height() * 3 / 5;
    const int label_scale = std::max(2, small / 3);
    const int label_y = row_y - label_scale * 7 - small;
    const int columns[3] = { screen.width() / 4, screen.width() / 2,
                             screen.width() * 3 / 4 };
    const char* labels[3] = { tr("HITS", "ПОПАДАНИЯ"), tr("MAX COMBO", "МАКС КОМБО"),
                              tr("MISSES", "ПРОМАХИ") };
    const unsigned int values[3] = { score.hits, score.max_combo, score.misses };
    const u32 colors[3] = { 0xff40c8ffu, 0xfff0f0f0u, 0xffff5060u };
    for (int column = 0; column < 3; ++column) {
        const int label_width = menuTextWidth(labels[column], label_scale);
        drawMenuText(screen, labels[column], columns[column] - label_width / 2,
                     label_y, label_scale, 0xff9cb3d3u);
        drawNumber(screen, values[column], columns[column], row_y, small, colors[column]);
        screen.rectangle(columns[column] - screen.width() / 12, row_y + small * 6,
                         screen.width() / 6, small / 2 + 1, colors[column]);
    }
    drawCenteredMenuText(screen, tr("BEST", "РЕКОРД"),
                         screen.height() / 10 - label_scale * 7 - small,
                         label_scale, 0xffa080ffu);
}

void drawResultsHint(Framebuffer& screen) {
    drawCenteredMenuText(screen, tr(
        ICON_CROSS ICON_CIRCLE ICON_MOVE "  CONTINUE      " ICON_TRIANGLE "  REPLAY",
        ICON_CROSS ICON_CIRCLE ICON_MOVE "  ДАЛЬШЕ      " ICON_TRIANGLE "  ЕЩЁ РАЗ"),
                         screen.height() - 40, 2, 0xff9cb3d3u);
}

void drawPlaylistNext(Framebuffer& screen, const PlaylistRun& run,
                      const LevelChoice& next, unsigned int seconds_left) {
    const int width = screen.width();
    const int y = screen.height() * 3 / 4;
    screen.rectangle(width / 2 - 360, y - 12, 720, 132, 0xff111b30u);
    // Song k of n.
    drawNumber(screen, static_cast<unsigned int>(run.position() + 1),
               width / 2 - 318, y + 4, 4, 0xffd9e7ffu);
    screen.line(width / 2 - 298, y + 27, width / 2 - 288, y + 2, 0xff7d92b2u, 2);
    drawNumber(screen, static_cast<unsigned int>(run.trackCount()),
               width / 2 - 266, y + 4, 4, 0xffd9e7ffu);
    drawMenuText(screen, tr("NEXT", "ДАЛЕЕ"), width / 2 - 200, y + 6, 3, 0xff9cb3d3u);
    if (next.card.ready()) {
        next.card.drawCover(screen, width / 2 - 110.0f, static_cast<float>(y), 72.0f, 1.0f);
        next.card.drawLabel(screen, width / 2 - 30.0f, static_cast<float>(y), 0.75f, 1.0f);
    } else {
        drawMenuText(screen, tr("TRACK", "ТРЕК"), width / 2 - 110, y + 26, 3, 0xffd9e7ffu);
    }
    drawNumber(screen, seconds_left, width / 2 + 330, y + 18, 6, 0xfff0d050u);
    drawCenteredMenuText(screen, tr(
        ICON_CROSS "  NOW     " ICON_TRIANGLE "  REPLAY     " ICON_SQUARE "  STOP",
        ICON_CROSS "  СЕЙЧАС     " ICON_TRIANGLE "  ПОВТОР     " ICON_SQUARE "  СТОП"),
        y + 92, 2, 0xff9cb3d3u);
}

void drawPlaylistSummary(Framebuffer& screen, const PlaylistRun& run,
                         const PlaylistChoice& playlist,
                         const std::vector<LevelChoice>& levels) {
    const int width = screen.width();
    const int height = screen.height();
    if (playlist.card.ready()) {
        playlist.card.drawCover(screen, width / 2 - 250.0f, 28.0f, 72.0f, 1.0f);
        playlist.card.drawLabel(screen, width / 2 - 170.0f, 28.0f, 0.75f, 1.0f);
    }
    drawCenteredMenuText(screen, tr("PLAYLIST COMPLETE", "ПЛЕЙЛИСТ ПРОЙДЕН"), 116, 3,
                         0xff70e0a0u);
    drawNumber(screen, run.totalScore(), width / 2, 150, 8, 0xfff0d050u);
    drawNumber(screen, run.totalHits(), width / 4, 222, 4, 0xff40c8ffu);
    drawNumber(screen, run.maxCombo(), width / 2, 222, 4, 0xfff0f0f0u);
    drawNumber(screen, run.totalMisses(), width * 3 / 4, 222, 4, 0xffff5060u);
    const char* labels[3] = { tr("HITS", "ПОПАДАНИЯ"), tr("MAX COMBO", "МАКС КОМБО"),
                              tr("MISSES", "ПРОМАХИ") };
    for (int column = 0; column < 3; ++column)
        drawMenuText(screen, labels[column],
                     width * (column + 1) / 4 - menuTextWidth(labels[column], 2) / 2, 200, 2,
                     0xff9cb3d3u);
    // The latest songs, one row each: number, card, score.
    const std::vector<PlaylistSongResult>& results = run.results();
    const std::size_t rows = std::min<std::size_t>(results.size(), 7);
    const std::size_t first = results.size() - rows;
    for (std::size_t row = 0; row < rows; ++row) {
        const PlaylistSongResult& result = results[first + row];
        const int y = 252 + static_cast<int>(row) * 56;
        screen.rectangle(width / 2 - 380, y, 760, 50,
                         row % 2 ? 0xff0c1426u : 0xff111b30u);
        drawNumber(screen, static_cast<unsigned int>(run.firstPosition() + first + row + 1),
                   width / 2 - 350, y + 15, 4, 0xff7d92b2u);
        if (result.level < levels.size() && levels[result.level].card.ready()) {
            levels[result.level].card.drawCover(screen, width / 2 - 310.0f, y + 3.0f,
                                                44.0f, 1.0f);
            levels[result.level].card.drawLabel(screen, width / 2 - 258.0f, y + 3.0f,
                                                0.46f, 1.0f);
        } else {
            drawMenuText(screen, tr("TRACK", "ТРЕК"), width / 2 - 300, y + 15, 2,
                         0xffd9e7ffu);
        }
        if (result.failed)
            drawMenuText(screen, tr("FAILED", "ПРОВАЛ"), width / 2 + 120, y + 16, 2,
                         0xffff5060u);
        drawNumber(screen, result.score, width / 2 + 300, y + 13, 4,
                   result.failed ? 0xffff5060u : 0xfff0d050u);
    }
    drawCenteredMenuText(screen, tr(ICON_CROSS "  BACK TO TRACKS", ICON_CROSS "  К ТРЕКАМ"),
                         height - 40, 2, 0xffd9e7ffu);
}

void drawPauseMenu(Framebuffer& screen) {
    const int width = 520;
    const int height = 295;
    const int x = (screen.width() - width) / 2;
    const int y = (screen.height() - height) / 2;
    screen.rectangle(x, y, width, height, 0xff101828u);
    drawCenteredMenuText(screen, tr("PAUSED", "ПАУЗА"), y + 30, 4, 0xffd9e7ffu);
    const char* items[4] = {
        tr(ICON_CROSS "  CONTINUE", ICON_CROSS "  ПРОДОЛЖИТЬ"),
        tr(ICON_CIRCLE "  RESTART", ICON_CIRCLE "  ЗАНОВО"),
        tr(ICON_SQUARE "  CALIBRATE", ICON_SQUARE "  КАЛИБРОВКА"),
        tr(ICON_TRIANGLE "  BACK TO TRACKS", ICON_TRIANGLE "  К ТРЕКАМ")
    };
    const u32 colors[4] = {0xff40e890u, 0xffffc030u, 0xff70d8ffu, 0xffff6470u};
    int block = 0;
    for (int item = 0; item < 4; ++item)
        block = std::max(block, menuTextWidth(items[item], 3));
    for (int item = 0; item < 4; ++item)
        drawMenuText(screen, items[item], (screen.width() - block) / 2, y + 100 + item * 45, 3,
                     colors[item]);
}

void drawAudioError(Framebuffer& screen, int error) {
    const int center_x = screen.width() / 2;
    const int center_y = screen.height() / 2;
    screen.rectangle(center_x - 330, center_y - 120, 660, 245, 0xff411018u);
    screen.rectangle(center_x - 325, center_y - 115, 650, 235, 0xff200b11u);
    drawCenteredMenuText(screen, tr("AUDIO ERROR", "ОШИБКА ЗВУКА"), center_y - 84,
                         4, 0xffff6470u);
    drawHexNumber(screen, static_cast<unsigned int>(error), center_x,
                  center_y - 8, screen.height() / 75 + 2, 0xffffd070u);
    drawCenteredMenuText(screen, tr(ICON_CROSS "  BACK TO TRACKS", ICON_CROSS "  К ТРЕКАМ"),
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
    drawCenteredMenuText(screen, tr("MOVE START FAILED", "MOVE НЕ ЗАПУСТИЛСЯ"),
                         screen.height() * 28 / 100, label_scale,
                         0xffffffffu);
    drawCenteredMenuText(screen, startupFailureHint(stage, error),
                         screen.height() * 35 / 100, label_scale,
                         0xffffd070u);
    drawCenteredMenuText(screen, tr("STAGE", "ЭТАП"),
                         screen.height() * 44 / 100, 2,
                         0xffffffffu);
    drawNumber(screen, static_cast<unsigned int>(stage),
               screen.width() / 2, screen.height() * 48 / 100,
               screen.height() / 45, 0xffffffffu);
    drawCenteredMenuText(screen, tr("ERROR CODE", "КОД ОШИБКИ"),
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
    // Both spheres back in view: the level resumes by itself (the bar),
    // or Start re-centers the play space first.
    const char* message = !both_moves_tracked ? tr("TRACKING LOST", "ПОТЕРЯ ОТСЛЕЖИВАНИЯ") :
        (!recalibrating ? tr("RESUMING", "ПРОДОЛЖАЕМ") :
         (!hands_separated ? tr("SEPARATE MOVE HANDS", "РАЗВЕДИТЕ РУКИ") :
          tr("HOLD BOTH MOVE STILL", "ДЕРЖИТЕ ОБА MOVE НЕПОДВИЖНО")));
    drawMenuText(screen, message, x + 25, y, 3,
                 both_moves_tracked ? 0xffd9e7ffu : 0xffffad8bu);
    drawMenuText(screen, !both_moves_tracked
                     ? tr("SHOW BOTH MOVE TO THE CAMERA", "ПОКАЖИТЕ ОБА MOVE КАМЕРЕ") :
                 (recalibrating ? tr("RETURN TO START POSE", "ВЕРНИТЕСЬ В НАЧАЛЬНУЮ ПОЗУ") :
                  tr(ICON_START "  RECENTER", ICON_START "  ЦЕНТРОВКА")),
                 x + 25, y + 50, 2, 0xff9cb3d3u);
    // The start pose matters only while Start re-centers the play space;
    // otherwise the song resumes by itself once both spheres are back.
    if (both_moves_tracked && recalibrating)
        drawMenuText(screen, tr("HANDS APART - BUTTONS UP - SABERS FORWARD",
                                "РУКИ ВРОЗЬ - КНОПКАМИ ВВЕРХ - МЕЧИ ВПЕРЕД"),
                     x + 25, y + 77, 2, 0xff9cb3d3u);
    // A sphere the camera lost for good: its Move button recalibrates it.
    drawMenuText(screen, tr("AIM FORWARD   " ICON_MOVE "  RESETS SPHERE   "
                            ICON_TRIANGLE "  QUIT",
                            "НАПРАВЬТЕ ВПЕРЕД   " ICON_MOVE "  СБРОС СФЕРЫ   "
                            ICON_TRIANGLE "  ВЫХОД"),
                 x + 25, y + 140, 2, 0xff9cb3d3u);
    screen.rectangle(x + 25, y + 108, width - 50, 14, 0xff303b56u);
    if (both_moves_tracked && (!recalibrating || hands_separated)) {
        const unsigned int elapsed = std::min(1000u, progress_ms);
        screen.rectangle(x + 25, y + 108,
                         (width - 50) * elapsed / 1000, 14,
                         0xff40e890u);
    }
}

namespace {

// Front view of a player in the pose of a calibration step; shoulders at
// (cx, sy). One meter is `scale` pixels, as for the live hand dots.
void drawPoseFigure(Framebuffer& screen, int step, int cx, int sy, float scale,
                    u32 color) {
    const int shoulder = static_cast<int>(0.2f * scale);
    screen.circle(cx, sy - 40, 18, color);
    screen.line(cx, sy - 20, cx, sy + 110, color, 5);
    screen.line(cx - shoulder, sy, cx + shoulder, sy, color, 5);
    screen.line(cx, sy + 110, cx - 26, sy + 190, color, 5);
    screen.line(cx, sy + 110, cx + 26, sy + 190, color, 5);
    for (int side = -1; side <= 1; side += 2) {
        int hand_x, hand_y;
        if (step == 1) {
            hand_x = cx + side * static_cast<int>(0.8f * scale);
            hand_y = sy;
        } else if (step == 2) {
            hand_x = cx + side * static_cast<int>(0.25f * scale);
            hand_y = sy + static_cast<int>(0.6f * scale);
        } else {
            // Arms reach toward the viewer: short, foreshortened.
            hand_x = cx + side * static_cast<int>(0.2f * scale);
            hand_y = sy + 10;
        }
        screen.line(cx + side * shoulder, sy, hand_x, hand_y, color, 5);
        screen.circle(hand_x, hand_y, 10, color);
    }
}

} // namespace

void drawCalibrationOverlay(Framebuffer& screen, const CalibrationView& view) {
    const char* const titles[3] = {
        tr("ARMS FORWARD", "РУКИ ВПЕРЁД"), tr("ARMS OUT TO THE SIDES", "РУКИ В СТОРОНЫ"),
        tr("ARMS DOWN", "РУКИ ВНИЗ")
    };
    const char* const hints[3] = {
        tr("HOLD BOTH MOVE STRAIGHT AHEAD", "ВЫТЯНИТЕ ОБА MOVE ПЕРЕД СОБОЙ"),
        tr("SPREAD THEM WIDE  LIKE A LETTER T", "РАЗВЕДИТЕ ШИРОКО, КАК БУКВА Т"),
        tr("RELAX BOTH ARMS ALONG THE BODY", "ОПУСТИТЕ РУКИ ВДОЛЬ ТЕЛА")
    };
    const int step = view.step < 0 ? 0 : (view.step > 2 ? 2 : view.step);
    const int width = 900;
    const int height = 420;
    const int x = (screen.width() - width) / 2;
    const int y = (screen.height() - height) / 2 - 20;
    screen.rectangle(x, y, width, height, 0xff101828u);

    // Steps: three marks, done ones green, the current one bright.
    for (int mark = 0; mark < 3; ++mark) {
        const u32 color = mark < step ? 0xff40e890u
                        : (mark == step ? 0xffd9e7ffu : 0xff3a4766u);
        screen.rectangle(x + 30 + mark * 46, y + 24, 36, 8, color);
    }
    drawNumber(screen, static_cast<unsigned int>(step + 1), x + 200, y + 18, 3,
               0xffd9e7ffu);
    screen.line(x + 212, y + 34, x + 220, y + 16, 0xff7d92b2u, 2);
    drawNumber(screen, 3u, x + 232, y + 18, 3, 0xffd9e7ffu);
    drawMenuText(screen, titles[step], x + 30, y + 56, 4, 0xffd9e7ffu);
    drawMenuText(screen, hints[step], x + 30, y + 102, 2, 0xff9cb3d3u);

    // The figure, and the player's hands on it.
    const float scale = 140.0f;
    const int cx = x + width - 230;
    const int sy = y + 150;
    drawPoseFigure(screen, step, cx, sy, scale, 0xff3a5a86u);
    const u32 hand_colors[2] = {view.hand_color[0] ? view.hand_color[0] : 0xff70d8ffu,
                                view.hand_color[1] ? view.hand_color[1] : 0xffff6878u};
    for (int slot = 0; slot < 2; ++slot) {
        if (!view.hand_visible[slot]) continue;
        float px = cx + view.hand_x[slot] * scale;
        float py = sy - view.hand_y[slot] * scale;
        px = std::max(static_cast<float>(x + 470), std::min(px, static_cast<float>(x + width - 20)));
        py = std::max(static_cast<float>(y + 20), std::min(py, static_cast<float>(y + height - 20)));
        if (screen.effectsEnabled())
            screen.glowDisc(px, py, 22.0f, withOpacity(hand_colors[slot], 0.5f));
        screen.circle(static_cast<int>(px), static_cast<int>(py), 9, hand_colors[slot]);
    }

    // Each controller: press its Move button in the pose, then hold while
    // its sphere settles and is measured (each row says which).
    if (step == 0 && view.directions_disagree)
        drawMenuText(screen, tr("BLADES DISAGREE  HOLD BOTH STRAIGHT AHEAD AGAIN",
                                "МЕЧИ НЕ СОВПАЛИ - СНОВА ВЫТЯНИТЕ ОБА ВПЕРЁД"),
                     x + 30, y + 176, 2, 0xffff6470u);
    for (int slot = 0; slot < 2; ++slot) {
        const int row_y = y + 222 + slot * 48;
        screen.circle(x + 42, row_y + 8, 10, hand_colors[slot]);
        const int state = view.hand_state[slot];
        const char* text = state == 0
            ? (view.hand_visible[slot] ? tr("PRESS " ICON_MOVE, "НАЖМИТЕ " ICON_MOVE)
                                       : tr("NOT SEEN  PRESS " ICON_MOVE,
                                            "НЕ ВИДНО - НАЖМИТЕ " ICON_MOVE))
            : state == 1 ? (view.hand_visible[slot] ? tr("HOLD STILL", "НЕ ДВИГАЙТЕСЬ")
                                                    : tr("LOOKING FOR THE CONTROLLER",
                                                         "ИЩЕМ КОНТРОЛЛЕР"))
            : state == 2 ? tr("MEASURING", "ИЗМЕРЯЕМ") : tr("OK", "ГОТОВО");
        const u32 color = state == 3 ? 0xff40e890u
                        : (state == 0 ? 0xffd9e7ffu : 0xffffc030u);
        drawMenuText(screen, text, x + 66, row_y, 3, color);
    }
    drawMenuText(screen, tr(ICON_TRIANGLE "  START OVER      " ICON_CROSS "  BACK",
                            ICON_TRIANGLE "  ПЕРЕКАЛИБРОВАТЬ      " ICON_CROSS "  НАЗАД"),
                 x + 30, y + height - 40, 2,
                 0xff9cb3d3u);
}

void drawCaptureIndicator(Framebuffer& screen) {
    const int capture_size = screen.height() / 42 + 4;
    screen.rectangle(screen.width() - capture_size * 3,
                     screen.height() / 30, capture_size, capture_size,
                     0xffe040d0u);
}

void drawUsbImport(Framebuffer& screen, unsigned int copied, unsigned int total) {
    char text[40];
    if (total)
        snprintf(text, sizeof(text), tr("USB COPY %u OF %u", "USB: КОПИЯ %u ИЗ %u"),
                 copied, total);
    else snprintf(text, sizeof(text), tr("USB ADDED %u", "USB: ДОБАВЛЕНО %u"), copied);
    const int scale = 2;
    const int x = screen.width() - menuTextWidth(text, scale) - screen.width() / 40;
    drawMenuText(screen, text, x, screen.height() / 30, scale, 0xff70e0a0u);
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
    const char* text = invert_hands ? tr("HANDS FLIPPED", "РУКИ НАОБОРОТ") :
                                      tr("HANDS AUTO", "РУКИ: АВТО");
    drawMenuText(screen, text, screen.width() - menuTextWidth(text, 2) - screen.width() / 40,
                 screen.height() / 30 + 34, 2,
                 invert_hands ? 0xffffb068u : 0xff80d8c0u);
}

} // namespace ps3
} // namespace movebeat
