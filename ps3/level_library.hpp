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

// A playlist bundle (playlist.ini, library.mblc) and the library tracks it
// names, in playlist order. Its card is loaded only while it is shown.
struct PlaylistChoice {
    std::string directory;
    movebeat::PlaylistManifest manifest;
    std::vector<std::size_t> tracks;
    // The manifest song of each entry of `tracks`.
    std::vector<std::size_t> song_indices;
    LibraryCard card;
    bool card_missing;

    PlaylistChoice() : card_missing(false) {}
};

const std::size_t kMaxLibraryLevels = 256;
const std::size_t kMaxLibraryPlaylists = 32;
// Cards kept loaded on each side of the selection (7 cards, about 1.4 MB).
const std::size_t kLibraryCardWindow = 3;

// Scans kLevelRoot once. Level bundles (at most kMaxLibraryLevels, valid
// level.ini and existing chart files) are returned, "current" first, then
// by title (ASCII case-insensitive); charts are not parsed here. Playlist
// bundles with at least one track in the library go to `playlists`, by
// title.
// USB drives add the bundles in MOVEBEAT/levels (kUsbLevelRootFormat); a
// folder name already on the hard disk wins. `usb_levels` receives how many
// levels came from USB.
std::vector<LevelChoice> discoverLibrary(std::vector<PlaylistChoice>& playlists,
                                         unsigned int* usb_levels = 0);
// Unloads cards outside the window around `position` of `order` (library
// indices as the carousel shows them) and loads at most `max_loads`
// missing ones, nearest first. Returns the number loaded, so scrolling
// spreads disk reads over frames instead of stalling one.
unsigned int updateLibraryCards(std::vector<LevelChoice>& levels,
                                const std::vector<std::size_t>& order,
                                std::size_t position, unsigned int max_loads);
void recordLibraryCardMemory(RunStatus& diagnostic,
                             const std::vector<LevelChoice>& levels);

// Short chart used when no level bundle can be loaded.
movebeat::Chart tutorialChart();
// Last chart event plus two seconds of outro, saturated to 32 bits.
unsigned int estimateChartDurationMs(const movebeat::Chart& chart);

} // namespace ps3
} // namespace movebeat
