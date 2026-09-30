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
};

// Scans kLevelRoot for at most 64 valid level bundles, "current" first, then
// by title.
std::vector<LevelChoice> discoverLevels();
// Keeps only the selected and neighbouring library cards in memory.
void updateLibraryCards(std::vector<LevelChoice>& levels, std::size_t selected);
void recordLibraryCardMemory(RunStatus& diagnostic,
                             const std::vector<LevelChoice>& levels);

// Short chart used when no level bundle can be loaded.
movebeat::Chart tutorialChart();
// Last chart event plus two seconds of outro, saturated to 32 bits.
unsigned int estimateChartDurationMs(const movebeat::Chart& chart);

} // namespace ps3
} // namespace movebeat
