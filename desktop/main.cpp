#include <cstdlib>
#include <iostream>
#include <string>

#include "movebeat/chart.hpp"
#include "movebeat/game.hpp"
#include "movebeat/level.hpp"

namespace {

movebeat::Vec3 directionVector(movebeat::CutDirection direction) {
    switch (direction) {
        case movebeat::CUT_UP: return movebeat::Vec3(0.0f, 0.45f, 0.0f);
        case movebeat::CUT_DOWN: return movebeat::Vec3(0.0f, -0.45f, 0.0f);
        case movebeat::CUT_LEFT: return movebeat::Vec3(-0.45f, 0.0f, 0.0f);
        case movebeat::CUT_RIGHT: return movebeat::Vec3(0.45f, 0.0f, 0.0f);
        case movebeat::CUT_UP_LEFT: return movebeat::Vec3(-0.32f, 0.32f, 0.0f);
        case movebeat::CUT_UP_RIGHT: return movebeat::Vec3(0.32f, 0.32f, 0.0f);
        case movebeat::CUT_DOWN_LEFT: return movebeat::Vec3(-0.32f, -0.32f, 0.0f);
        case movebeat::CUT_DOWN_RIGHT: return movebeat::Vec3(0.32f, -0.32f, 0.0f);
        case movebeat::CUT_ANY: return movebeat::Vec3(0.45f, 0.0f, 0.0f);
    }
    return movebeat::Vec3();
}

movebeat::SaberPose poseAt(const movebeat::Vec3& tip) {
    return movebeat::SaberPose(tip + movebeat::Vec3(0.0f, -0.4f, 0.0f), tip, true);
}

} // namespace

int runSimulation(int argc, char** argv) {
    const std::string input_path = argc > 1 ? argv[1] : "assets/charts/tutorial.mbchart";
    std::string chart_path = input_path;
    movebeat::Chart chart;
    std::string error;
    movebeat::LevelManifest level;
    if (movebeat::loadLevelDirectory(input_path, level, error)) {
        if (!movebeat::loadLevelChart(input_path, level, 0, chart, error)) {
            std::cerr << "MoveBeat: " << error << "\n";
            return EXIT_FAILURE;
        }
        chart_path = input_path + "/" + level.charts[0].file;
    } else if (!movebeat::loadChartFile(input_path, chart, error)) {
        std::cerr << "MoveBeat: " << error << "\n";
        return EXIT_FAILURE;
    }

    const movebeat::GameConfig config;
    movebeat::Game game(config);
    game.load(chart);
    const movebeat::SaberPose parked = poseAt(movebeat::Vec3(10.0f, 10.0f, 0.0f));

    unsigned int cut_note_count = 0;
    for (std::size_t i = 0; i < chart.notes.size(); ++i) {
        const movebeat::Note& note = chart.notes[i];
        if (note.kind == movebeat::NOTE_BOMB) {
            game.update(note.time_ms + 181, parked, parked);
            continue;
        }
        ++cut_note_count;
        const movebeat::Vec3 center = game.notePosition(note, note.time_ms);
        const movebeat::Vec3 motion = directionVector(note.direction);
        const movebeat::SaberPose before = poseAt(center - motion);
        const movebeat::SaberPose after = poseAt(center + motion);
        const unsigned int before_time = note.time_ms > 16 ? note.time_ms - 16 : 0;

        // Break the tracking history before teleporting the deterministic
        // simulator to its next scripted swing.
        const movebeat::SaberPose untracked;
        const unsigned int reset_time = note.time_ms > 32 ? note.time_ms - 32 : 0;
        game.update(reset_time, untracked, untracked);

        if (note.hand == movebeat::HAND_LEFT) {
            game.update(before_time, before, parked);
            game.update(note.time_ms, after, parked);
        } else {
            game.update(before_time, parked, before);
            game.update(note.time_ms, parked, after);
        }
    }

    unsigned int final_time_ms = chart.notes.empty() ? 0u : chart.notes.back().time_ms;
    if (!chart.notes.empty()) {
        const unsigned long long settled = static_cast<unsigned long long>(final_time_ms)
            + config.late_window_ms + config.swing_angle_window_ms + 1u;
        final_time_ms = settled > static_cast<unsigned long long>(~0u)
            ? ~0u : static_cast<unsigned int>(settled);
    }
    for (std::size_t i = 0; i < chart.obstacles.size(); ++i) {
        const unsigned long long end = static_cast<unsigned long long>(
            chart.obstacles[i].time_ms) + chart.obstacles[i].duration_ms + 1u;
        const unsigned int clamped = end > static_cast<unsigned long long>(~0u)
            ? ~0u : static_cast<unsigned int>(end);
        if (clamped > final_time_ms) final_time_ms = clamped;
    }
    for (std::size_t i = 0; i < chart.arcs.size(); ++i) {
        const unsigned int end = chart.arcs[i].tail_time_ms == ~0u
            ? ~0u : chart.arcs[i].tail_time_ms + 1u;
        if (end > final_time_ms) final_time_ms = end;
    }
    // Finish pending scoring without inventing another motion sample.
    game.update(final_time_ms, movebeat::SaberPose(), movebeat::SaberPose());

    const movebeat::ScoreState& score = game.score();
    std::cout << "MoveBeat deterministic simulation\n"
              << "chart: " << chart_path << "\n"
              << "notes: " << chart.notes.size() << "\n"
              << "arcs: " << chart.arcs.size() << "\n"
              << "hits: " << score.hits << "\n"
              << "misses: " << score.misses << "\n"
              << "bombs avoided: " << score.bombs_avoided << "\n"
              << "walls avoided: " << score.obstacles_avoided << "\n"
              << "max combo: " << score.max_combo << "\n"
              << "score: " << score.score << "\n";
    return game.finished(final_time_ms) &&
        score.hits == cut_note_count && score.misses == 0 &&
        score.bombs_hit == 0 && score.obstacles_hit == 0 &&
        score.obstacles_avoided == chart.obstacles.size()
        ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char** argv) {
    const int result = runSimulation(argc, argv);
    std::cout.flush();
    std::cerr.flush();
    // Some MSVC environments used by the desktop harness hang while tearing
    // down process-global iostream state. All simulation-owned objects have
    // already left scope, so skip only the redundant global CRT teardown.
    std::_Exit(result);
}
