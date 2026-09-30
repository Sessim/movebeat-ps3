#include "level_library.hpp"

#include <algorithm>
#include <dirent.h>

#include "app_paths.hpp"
#include "library_window.hpp"

namespace movebeat {
namespace ps3 {
namespace {

bool levelChoiceLess(const LevelChoice& left, const LevelChoice& right) {
    const bool left_current = left.directory == std::string(kLevelRoot) + "/current";
    const bool right_current = right.directory == std::string(kLevelRoot) + "/current";
    if (left_current != right_current) return left_current;
    if (left.manifest.title != right.manifest.title)
        return left.manifest.title < right.manifest.title;
    return left.directory < right.directory;
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
    for (dirent* entry = readdir(directory); entry && choices.size() < 64;
         entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name.empty() || name == "." || name == ".." ||
            name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos) continue;
        LevelChoice choice;
        choice.directory = std::string(kLevelRoot) + "/" + name;
        std::string error;
        movebeat::Chart validation_chart;
        if (movebeat::loadLevelDirectory(choice.directory, choice.manifest, error) &&
            movebeat::loadLevelChart(choice.directory, choice.manifest, 0,
                                     validation_chart, error)) {
            choices.push_back(choice);
        }
    }
    closedir(directory);
    std::sort(choices.begin(), choices.end(), levelChoiceLess);
    return choices;
}

void updateLibraryCards(std::vector<LevelChoice>& levels, std::size_t selected) {
    for (std::size_t index = 0; index < levels.size(); ++index) {
        if (libraryCardVisible(index, selected, levels.size())) {
            if (!levels[index].card.ready())
                levels[index].card.load(levels[index].directory);
        } else {
            levels[index].card.clear();
        }
    }
}

void recordLibraryCardMemory(RunStatus& diagnostic,
                             const std::vector<LevelChoice>& levels) {
    unsigned int loaded = 0;
    for (std::size_t index = 0; index < levels.size(); ++index)
        if (levels[index].card.ready()) ++loaded;
    diagnostic.library_cards_loaded = loaded;
    diagnostic.library_card_bytes = loaded *
        LibraryCard::WIDTH *
        LibraryCard::HEIGHT * sizeof(unsigned short);
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
