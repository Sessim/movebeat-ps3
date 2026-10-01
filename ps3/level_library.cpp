#include "level_library.hpp"

#include <algorithm>
#include <dirent.h>
#include <stdio.h>

#include "app_paths.hpp"
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

std::vector<LevelChoice> discoverLevels() {
    std::vector<LevelChoice> choices;
    DIR* directory = opendir(kLevelRoot);
    if (!directory) return choices;
    for (dirent* entry = readdir(directory);
         entry && choices.size() < kMaxLibraryLevels;
         entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name.empty() || name == "." || name == ".." ||
            name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos) continue;
        LevelChoice choice;
        choice.directory = std::string(kLevelRoot) + "/" + name;
        std::string error;
        // Only level.ini is parsed here; charts are parsed when a level is
        // started, so startup cost no longer grows with chart sizes.
        if (movebeat::loadLevelDirectory(choice.directory, choice.manifest, error) &&
            levelChartFilesExist(choice.directory, choice.manifest)) {
            choices.push_back(choice);
        }
    }
    closedir(directory);
    std::sort(choices.begin(), choices.end(), levelChoiceLess);
    return choices;
}

unsigned int updateLibraryCards(std::vector<LevelChoice>& levels,
                                std::size_t selected, unsigned int max_loads) {
    const std::size_t count = levels.size();
    for (std::size_t index = 0; index < count; ++index)
        if (libraryCardDistance(index, selected, count) > kLibraryCardWindow)
            levels[index].card.clear();
    // Nearest first: the selected card, then +1, -1, +2, -2, ...
    unsigned int loads = 0;
    for (std::size_t distance = 0;
         distance <= kLibraryCardWindow && distance <= count / 2u; ++distance) {
        for (int side = 0; side < 2 && loads < max_loads; ++side) {
            if (distance == 0 && side == 1) break;
            const std::size_t index = side == 0
                ? (selected + distance) % count
                : (selected + count - distance % count) % count;
            LevelChoice& choice = levels[index];
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
