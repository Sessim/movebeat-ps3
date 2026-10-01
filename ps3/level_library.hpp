#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "library_card.hpp"
#include "run_diagnostics.hpp"
#include "movebeat/chart.hpp"
#include "movebeat/level.hpp"

namespace movebeat {
namespace ps3 {

struct LevelChoice {
    std::string directory;
    movebeat::LevelManifest manifest;
    LibraryCard card;
    // No usable library.mblc: draw the placeholder, do not retry each frame.
    bool card_missing;
    // A chart failed to parse when the level was started.
    bool chart_broken;

    LevelChoice() : card_missing(false), chart_broken(false) {}
};

const std::size_t kMaxLibraryLevels = 256;
// Cards kept loaded on each side of the selection (7 cards, about 1.4 MB).
const std::size_t kLibraryCardWindow = 3;

// Scans kLevelRoot for at most kMaxLibraryLevels bundles with a valid
// level.ini and existing chart files, "current" first, then by title
// (ASCII case-insensitive). Charts are not parsed here.
std::vector<LevelChoice> discoverLevels();
// Unloads cards outside the window around `selected` and loads at most
// `max_loads` missing ones, nearest first. Returns the number loaded, so
// scrolling spreads disk reads over frames instead of stalling one.
unsigned int updateLibraryCards(std::vector<LevelChoice>& levels,
                                std::size_t selected, unsigned int max_loads);
void recordLibraryCardMemory(RunStatus& diagnostic,
                             const std::vector<LevelChoice>& levels);

// Short chart used when no level bundle can be loaded.
movebeat::Chart tutorialChart();
// Last chart event plus two seconds of outro, saturated to 32 bits.
unsigned int estimateChartDurationMs(const movebeat::Chart& chart);

} // namespace ps3
} // namespace movebeat
