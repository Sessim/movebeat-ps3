#include "level_library.hpp"

#include <algorithm>
#include <dirent.h>
#include <stdio.h>

#include "app_paths.hpp"
#include "library_shelf.hpp"
#include "library_window.hpp"

namespace movebeat {
namespace ps3 {
namespace {

// ASCII letters fold to lower case; other bytes (e.g. UTF-8 Cyrillic)
// compare as they are, which keeps their alphabetical byte order.
int compareTitles(const std::string& left, const std::string& right) {
    const std::size_t length = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < length; ++index) {
        unsigned char a = static_cast<unsigned char>(left[index]);
        unsigned char b = static_cast<unsigned char>(right[index]);
        if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b) return a < b ? -1 : 1;
    }
    if (left.size() == right.size()) return 0;
    return left.size() < right.size() ? -1 : 1;
}

bool playlistChoiceLess(const PlaylistChoice& left, const PlaylistChoice& right) {
    const int titles = compareTitles(left.manifest.title, right.manifest.title);
    if (titles != 0) return titles < 0;
    return left.directory < right.directory;
}

bool levelChoiceLess(const LevelChoice& left, const LevelChoice& right) {
    const bool left_current = left.directory == std::string(kLevelRoot) + "/current";
    const bool right_current = right.directory == std::string(kLevelRoot) + "/current";
    if (left_current != right_current) return left_current;
    const int titles = compareTitles(left.manifest.title, right.manifest.title);
    if (titles != 0) return titles < 0;
    return left.directory < right.directory;
}

bool levelChartFilesExist(const std::string& directory,
                          const movebeat::LevelManifest& manifest) {
    if (manifest.charts.empty()) return false;
    for (std::size_t index = 0; index < manifest.charts.size(); ++index) {
        const std::string path = directory + "/" + manifest.charts[index].file;
        FILE* file = 0;
#if defined(_MSC_VER)
        fopen_s(&file, path.c_str(), "rb");
#else
        file = fopen(path.c_str(), "rb");
#endif
        if (!file) return false;
        fclose(file);
    }
    return true;
}

movebeat::Note makeNote(unsigned int time_ms, int lane, int row,
                        movebeat::Hand hand, movebeat::CutDirection direction,
                        movebeat::NoteKind kind = movebeat::NOTE_CUT) {
    movebeat::Note note;
    note.time_ms = time_ms;
    note.lane = lane;
    note.row = row;
    note.hand = hand;
    note.direction = direction;
    note.kind = kind;
    return note;
}

} // namespace

namespace {

// Adds the level and playlist bundles of one library folder; a folder name
// already found (on the hard disk, or an earlier USB drive) is skipped.
// Returns the number of levels added.
unsigned int scanLibraryRoot(const std::string& root, std::vector<LevelChoice>& choices,
                             std::vector<PlaylistChoice>& playlists,
                             std::vector<std::string>& names) {
    DIR* directory = opendir(root.c_str());
    if (!directory) return 0;
    unsigned int added = 0;
    for (dirent* entry = readdir(directory); entry; entry = readdir(directory)) {
        const std::string name(entry->d_name);
        // Dot names include the FTP uploader's unfinished staging folders.
        if (name.empty() || name[0] == '.' ||
            name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos) continue;
        if (std::find(names.begin(), names.end(), name) != names.end()) continue;
        const std::string path = root + "/" + name;
        std::string error;
        // Only level.ini is parsed here; charts are parsed when a level is
        // started, so startup cost no longer grows with chart sizes.
        if (choices.size() < kMaxLibraryLevels) {
            LevelChoice choice;
            choice.directory = path;
            if (movebeat::loadLevelDirectory(path, choice.manifest, error)) {
                if (levelChartFilesExist(path, choice.manifest)) {
                    choices.push_back(choice);
                    names.push_back(name);
                    ++added;
                }
                continue;
            }
        }
        if (playlists.size() < kMaxLibraryPlaylists) {
            PlaylistChoice playlist;
            playlist.directory = path;
            if (movebeat::loadPlaylistDirectory(path, playlist.manifest, error)) {
                playlists.push_back(playlist);
                names.push_back(name);
            }
        }
    }
    closedir(directory);
    return added;
}

} // namespace

std::vector<LevelChoice> discoverLibrary(std::vector<PlaylistChoice>& playlists,
                                         unsigned int* usb_levels) {
    std::vector<LevelChoice> choices;
    std::vector<std::string> names;
    playlists.clear();
    scanLibraryRoot(kLevelRoot, choices, playlists, names);
    // Bundles copied to a USB drive play from there: no FTP needed.
    unsigned int from_usb = 0;
    for (int drive = 0; drive < 8; ++drive) {
        char root[48];
        snprintf(root, sizeof(root), kUsbLevelRootFormat, drive);
        from_usb += scanLibraryRoot(root, choices, playlists, names);
    }
    if (usb_levels) *usb_levels = from_usb;
    std::sort(choices.begin(), choices.end(), levelChoiceLess);

    std::vector<const movebeat::LevelManifest*> manifests;
    for (std::size_t index = 0; index < choices.size(); ++index)
        manifests.push_back(&choices[index].manifest);
    std::vector<PlaylistChoice> usable;
    for (std::size_t index = 0; index < playlists.size(); ++index) {
        PlaylistChoice& playlist = playlists[index];
        playlist.tracks = resolvePlaylistTracks(playlist.manifest, manifests,
                                                playlist.song_indices);
        if (!playlist.tracks.empty()) usable.push_back(playlist);
        else printf("Playlist without library tracks: %s\n", playlist.directory.c_str());
    }
    std::sort(usable.begin(), usable.end(), playlistChoiceLess);
    playlists.swap(usable);
    return choices;
}

unsigned int updateLibraryCards(std::vector<LevelChoice>& levels,
                                const std::vector<std::size_t>& order,
                                std::size_t position, unsigned int max_loads) {
    const std::size_t count = order.size();
    // A library index may appear once in an order; keep its card when its
    // place is inside the window.
    std::vector<bool> keep(levels.size(), false);
    for (std::size_t place = 0; place < count; ++place)
        if (order[place] < levels.size() &&
            libraryCardDistance(place, position, count) <= kLibraryCardWindow)
            keep[order[place]] = true;
    for (std::size_t index = 0; index < levels.size(); ++index)
        if (!keep[index]) levels[index].card.clear();
    if (count == 0 || position >= count) return 0;
    // Nearest first: the selected card, then +1, -1, +2, -2, ...
    unsigned int loads = 0;
    for (std::size_t distance = 0;
         distance <= kLibraryCardWindow && distance <= count / 2u; ++distance) {
        for (int side = 0; side < 2 && loads < max_loads; ++side) {
            if (distance == 0 && side == 1) break;
            const std::size_t place = side == 0
                ? (position + distance) % count
                : (position + count - distance % count) % count;
            if (order[place] >= levels.size()) continue;
            LevelChoice& choice = levels[order[place]];
            if (choice.card.ready() || choice.card_missing) continue;
            if (!choice.card.load(choice.directory)) choice.card_missing = true;
            ++loads;
        }
    }
    return loads;
}

void recordLibraryCardMemory(RunStatus& diagnostic,
                             const std::vector<LevelChoice>& levels) {
    unsigned int loaded = 0;
    for (std::size_t index = 0; index < levels.size(); ++index)
        if (levels[index].card.ready()) ++loaded;
    diagnostic.library_cards_loaded = loaded;
    diagnostic.library_card_bytes = loaded * LibraryCard::BYTES;
}

movebeat::Chart tutorialChart() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(2000, 1, 1, movebeat::HAND_LEFT, movebeat::CUT_UP));
    chart.notes.push_back(makeNote(2600, 2, 1, movebeat::HAND_RIGHT, movebeat::CUT_UP));
    chart.notes.push_back(makeNote(3200, 0, 0, movebeat::HAND_LEFT, movebeat::CUT_RIGHT));
    chart.notes.push_back(makeNote(3800, 3, 2, movebeat::HAND_RIGHT, movebeat::CUT_LEFT));
    chart.notes.push_back(makeNote(4400, 1, 1, movebeat::HAND_NONE, movebeat::CUT_ANY,
                                   movebeat::NOTE_BOMB));
    chart.notes.push_back(makeNote(5000, 2, 0, movebeat::HAND_RIGHT,
                                   movebeat::CUT_UP_LEFT));
    chart.notes.push_back(makeNote(5500, 3, 1, movebeat::HAND_RIGHT,
                                   movebeat::CUT_ANY, movebeat::NOTE_CHAIN));
    movebeat::Arc arc;
    arc.head_time_ms = 2600;
    arc.tail_time_ms = 3800;
    arc.head_lane = 2;
    arc.head_row = 1;
    arc.tail_lane = 3;
    arc.tail_row = 2;
    arc.hand = movebeat::HAND_RIGHT;
    chart.arcs.push_back(arc);
    return chart;
}

unsigned int estimateChartDurationMs(const movebeat::Chart& chart) {
    unsigned int last_event_ms = 0;
    for (std::size_t index = 0; index < chart.notes.size(); ++index)
        last_event_ms = std::max(last_event_ms, chart.notes[index].time_ms);
    for (std::size_t index = 0; index < chart.obstacles.size(); ++index) {
        const movebeat::Obstacle& obstacle = chart.obstacles[index];
        last_event_ms = std::max(last_event_ms,
            obstacle.time_ms + obstacle.duration_ms);
    }
    for (std::size_t index = 0; index < chart.arcs.size(); ++index)
        last_event_ms = std::max(last_event_ms, chart.arcs[index].tail_time_ms);
    return last_event_ms > 0xffffffffu - 2000u
        ? 0xffffffffu : last_event_ms + 2000u;
}

} // namespace ps3
} // namespace movebeat
