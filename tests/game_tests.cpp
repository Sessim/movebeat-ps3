#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "movebeat/chart.hpp"
#include "movebeat/calibration.hpp"
#include "movebeat/game.hpp"
#include "movebeat/level.hpp"
#include "movebeat/records.hpp"
#include "movebeat/settings.hpp"
#include "../ps3/projection.hpp"
#include "../ps3/note_cube.hpp"
#include "../ps3/software_note_renderer.hpp"
#include "../ps3/tracking_pause.hpp"
#include "../ps3/library_window.hpp"
#include "../ps3/frame_timing.hpp"
#include "../ps3/cut_feedback.hpp"
#if defined(__PPU__)
#include "../ps3/move_input.hpp"
void runGemRuntimeHueTests();
#endif

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        ++failures;
    }
}

void checkpoint(const char* name) {
    std::printf("RUN: %s\n", name);
}

bool near(float left, float right) {
    return std::fabs(left - right) < 0.0001f;
}

movebeat::SaberPose pose(float x, float y, float z) {
    const movebeat::Vec3 tip(x, y, z);
    return movebeat::SaberPose(tip + movebeat::Vec3(0.0f, -0.4f, 0.0f), tip, true);
}

movebeat::SaberPose angledPose(float x, float y, float z, float degrees) {
    const float radians = degrees * 0.0174532925f;
    const movebeat::Vec3 tip(x, y, z);
    const movebeat::Vec3 blade(std::cos(radians) * 0.4f,
                              std::sin(radians) * 0.4f, 0.0f);
    return movebeat::SaberPose(tip - blade, tip, true);
}

movebeat::Note makeNote(unsigned int time, int lane, int row,
                        movebeat::Hand hand, movebeat::CutDirection direction) {
    movebeat::Note note;
    note.time_ms = time;
    note.lane = lane;
    note.row = row;
    note.hand = hand;
    note.direction = direction;
    return note;
}

void testChartParser() {
    std::istringstream input(
        "# comment\n"
        "2000,2,1,R,U,15.5\n"
        "1500,0,2,B,A\n"
        "1000,1,0,L,UL\n"
        "C,1100,2,1,R\n"
        "W,1200,500,1,2,1,1\n"
        "A,900,1400,0,0,0,1.5,3,2,1,0.5,2,L\n"
        "A,1600,1700,0,0,3,2,R\n");
    movebeat::Chart chart;
    std::string error;
    expect(movebeat::loadChart(input, chart, error), "valid chart parses");
    expect(chart.notes.size() == 4, "parser returns notes and chain links");
    expect(chart.notes[0].time_ms == 1000, "parser sorts notes by time");
    expect(chart.notes[0].direction == movebeat::CUT_UP_LEFT, "parser reads diagonal direction");
    expect(chart.notes[1].kind == movebeat::NOTE_CHAIN &&
           chart.notes[1].hand == movebeat::HAND_RIGHT,
           "parser reads chain links");
    expect(chart.notes[2].kind == movebeat::NOTE_BOMB, "parser reads bombs");
    expect(chart.notes[3].direction == movebeat::CUT_UP, "parser reads direction");
    expect(near(chart.notes[0].angle_offset_degrees, 0.0f) &&
           near(chart.notes[3].angle_offset_degrees, 15.5f),
           "parser defaults old rows to zero and reads optional note angle offsets");
    expect(chart.obstacles.size() == 1, "parser returns walls separately");
    expect(chart.obstacles[0].duration_ms == 500 && chart.obstacles[0].width == 2,
           "parser preserves wall duration and dimensions");
    expect(chart.arcs.size() == 2, "parser returns arcs separately");
    expect(chart.arcs[0].head_time_ms == 900 &&
           chart.arcs[0].tail_time_ms == 1400 &&
           chart.arcs[0].tail_lane == 3 && chart.arcs[0].tail_row == 2 &&
           chart.arcs[0].head_direction == movebeat::CUT_UP &&
           near(chart.arcs[0].head_control_point_multiplier, 1.5f) &&
           chart.arcs[0].tail_direction == movebeat::CUT_DOWN &&
           near(chart.arcs[0].tail_control_point_multiplier, 0.5f) &&
           chart.arcs[0].mid_anchor_mode == 2 &&
           chart.arcs[0].hand == movebeat::HAND_LEFT,
           "parser preserves arc timing, curve controls, and hand");
    expect(chart.arcs[1].head_direction == movebeat::CUT_ANY &&
           near(chart.arcs[1].head_control_point_multiplier, 1.0f) &&
           chart.arcs[1].hand == movebeat::HAND_RIGHT,
           "parser remains compatible with legacy endpoint-only arcs");

    std::istringstream invalid("1000,9,0,L,A\n");
    expect(!movebeat::loadChart(invalid, chart, error), "invalid lane is rejected");
    expect(!error.empty(), "invalid chart explains the error");
    std::istringstream invalid_wall("W,1000,0,3,2,0,1\n");
    expect(!movebeat::loadChart(invalid_wall, chart, error),
           "invalid wall is rejected");
    std::istringstream invalid_chain("C,1000,1,1,B\n");
    expect(!movebeat::loadChart(invalid_chain, chart, error),
           "chain link requires a playable hand");
    std::istringstream invalid_arc("A,1500,1000,0,0,3,2,L\n");
    expect(!movebeat::loadChart(invalid_arc, chart, error),
           "arc tail cannot precede its head");
    std::istringstream invalid_arc_curve(
        "A,1000,1500,0,0,9,1,3,2,8,1,0,L\n");
    expect(!movebeat::loadChart(invalid_arc_curve, chart, error),
           "arc curve rejects an invalid direction code");
}

void testPoseCalibration() {
    movebeat::PoseCalibration calibration;
    const movebeat::SaberPose left = pose(-0.3f, 1.0f, 2.0f);
    const movebeat::SaberPose right = pose(0.3f, 1.0f, 2.0f);
    for (unsigned int t = 0; t < 1000; t += 20)
        expect(!calibration.sample(t, left, right), "calibration waits a full second");
    expect(calibration.sample(1000, left, right), "stable two-hand pose completes calibration");
    expect(near(calibration.origin().x, 0) && near(calibration.origin().y, 1) &&
           near(calibration.origin().z, 2), "calibration averages the two tips");
    expect(!calibration.sample(1020, movebeat::SaberPose(), right) &&
           calibration.progressMs() == 0, "tracking loss resets calibration");
    calibration.sample(1040, left, right);
    calibration.sample(1060, left, right);
    expect(!calibration.sample(1080, pose(-0.2f, 1, 2), right) &&
           calibration.progressMs() == 0, "hand movement restarts the stable interval");
    calibration.reset();
    bool jittered_pose_completed = false;
    for (unsigned int frame = 0; frame <= 50; ++frame) {
        const float jitter = 0.015f * std::sin(static_cast<float>(frame) * 0.61f);
        const movebeat::SaberPose jittered_left = pose(-0.3f + jitter, 1, 2);
        const movebeat::SaberPose jittered_right = pose(0.3f - jitter, 1, 2);
        jittered_pose_completed = calibration.sample(
            2000u + frame * 20u, jittered_left, jittered_right);
    }
    expect(jittered_pose_completed && near(calibration.origin().x, 0.0f),
           "camera-scale sub-4cm symmetric pose jitter still calibrates around the midpoint");
    calibration.reset();
    calibration.sample(5000, left, right);
    calibration.sample(5100, left, right);
    expect(!calibration.sample(5201, left, right) &&
           calibration.progressMs() == 0,
           "a tracking/sample gap over 100ms restarts the one-second calibration window");
    calibration.reset();
    calibration.sample(0, left, right);
    expect(!calibration.sample(1000, left, right),
           "two isolated frames cannot satisfy calibration");
    calibration.reset();
    for (int i = 0; i < 100; ++i)
        expect(!calibration.sample(42, left, right),
               "repeated timestamps do not advance calibration");
}

void testCorrectCut() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(1000, 1, 1, movebeat::HAND_LEFT, movebeat::CUT_RIGHT));
    movebeat::Game game;
    game.load(chart);
    const movebeat::Vec3 center = game.notePosition(chart.notes[0], 1000);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    game.update(984, pose(center.x - 0.4f, center.y, 0.0f), parked);
    game.update(1000, pose(center.x, center.y, 0.0f), parked);
    expect(game.score().hits == 1, "correct hand and direction hit the note");
    expect(game.events().left_hit, "cut reports feedback for the correct hand");
    expect(game.events().cut_visual_count == 1u &&
           game.events().cut_visuals[0].hand == movebeat::HAND_LEFT &&
           near(game.events().cut_visuals[0].position.x, center.x),
           "cut feedback carries the hit position and hand");
    movebeat::ps3::CutFeedback feedback;
    feedback.add(game.events(), 1000u);
    expect(feedback.visible(0u, 1000u) && feedback.visible(0u, 1179u) &&
           !feedback.visible(0u, 1180u),
           "cut burst expires after 180 ms");
    feedback.clear();
    expect(!feedback.visible(0u, 1000u), "cut burst clears on replay");
    expect(game.score().score == 15,
           "centered cut without blade rotation awards center accuracy points");
    expect(game.remainingNotes() == 0, "hit note is consumed");
    expect(game.noteCount() == 1, "renderer can inspect chart note count");
    expect(!game.notePending(0), "renderer does not draw consumed notes");
}

void testWholeBladeCut() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(1000, 1, 1, movebeat::HAND_LEFT, movebeat::CUT_UP));
    movebeat::Game game;
    game.load(chart);
    const movebeat::Vec3 center = game.notePosition(chart.notes[0], 1000);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    const movebeat::SaberPose lower(
        movebeat::Vec3(center.x - 0.5f, center.y - 0.2f, 0.0f),
        movebeat::Vec3(center.x + 0.5f, center.y - 0.2f, 0.0f), true);
    const movebeat::SaberPose upper(
        movebeat::Vec3(center.x - 0.5f, center.y + 0.2f, 0.0f),
        movebeat::Vec3(center.x + 0.5f, center.y + 0.2f, 0.0f), true);
    game.update(984, lower, parked);
    game.update(1000, upper, parked);
    expect(game.score().hits == 1,
           "the swept body of the blade cuts a cube even when the tip misses it");
    expect(game.events().left_hit,
           "a full-blade collision reports feedback for its assigned hand");

    movebeat::Game miss_game;
    miss_game.load(chart);
    const movebeat::SaberPose outside_lower(
        movebeat::Vec3(center.x - 0.5f, center.y + 0.30f, 0.0f),
        movebeat::Vec3(center.x + 0.5f, center.y + 0.30f, 0.0f), true);
    const movebeat::SaberPose outside_upper(
        movebeat::Vec3(center.x - 0.5f, center.y + 0.70f, 0.0f),
        movebeat::Vec3(center.x + 0.5f, center.y + 0.70f, 0.0f), true);
    miss_game.update(984, outside_lower, parked);
    miss_game.update(1000, outside_upper, parked);
    expect(miss_game.score().hits == 0 && miss_game.notePending(0),
           "the full-blade sweep still respects the 28cm hit tolerance");
}

void testWrongDirectionAndMiss() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(1000, 1, 1, movebeat::HAND_LEFT, movebeat::CUT_RIGHT));
    movebeat::Game game;
    game.load(chart);
    const movebeat::Vec3 center = game.notePosition(chart.notes[0], 1000);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    game.update(984, pose(center.x + 0.4f, center.y, 0.0f), parked);
    game.update(1000, pose(center.x - 0.4f, center.y, 0.0f), parked);
    expect(game.score().hits == 0, "opposite swing direction is rejected");
    game.update(1200, parked, parked);
    expect(game.score().misses == 1, "expired note becomes a miss");
    expect(game.events().miss, "expired note reports miss feedback");
    expect(game.score().combo == 0, "miss resets combo");
}

void testAngleOffsetDirection() {
    movebeat::Chart chart;
    movebeat::Note note = makeNote(1000, 1, 1, movebeat::HAND_LEFT,
                                   movebeat::CUT_UP);
    note.angle_offset_degrees = 90.0f;
    chart.notes.push_back(note);
    movebeat::Game game;
    game.load(chart);
    const movebeat::Vec3 center = game.notePosition(chart.notes[0], 1000);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    game.update(984, pose(center.x + 0.4f, center.y, 0.0f), parked);
    game.update(1000, pose(center.x - 0.4f, center.y, 0.0f), parked);
    expect(game.score().hits == 1,
           "a counter-clockwise angle offset rotates the accepted cut direction");

    movebeat::Game unrotated_game;
    unrotated_game.load(chart);
    unrotated_game.update(984, pose(center.x - 0.4f, center.y, 0.0f), parked);
    unrotated_game.update(1000, pose(center.x + 0.4f, center.y, 0.0f), parked);
    expect(unrotated_game.score().hits == 0,
           "the original direction is rejected after an angle offset");
}

void testLongTrackingGapCannotCut() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(1000, 1, 1, movebeat::HAND_LEFT,
                                   movebeat::CUT_RIGHT));
    movebeat::Game game;
    game.load(chart);
    const movebeat::Vec3 center = game.notePosition(chart.notes[0], 1000);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    game.update(850, pose(center.x - 0.4f, center.y, 0.0f), parked);
    game.update(1000, pose(center.x + 0.4f, center.y, 0.0f), parked);
    expect(game.score().hits == 0 && game.notePending(0),
           "a 150 ms input gap cannot teleport a blade through a note");
    game.update(1016, pose(center.x - 0.4f, center.y, 0.0f), parked);
    game.update(1032, pose(center.x + 0.4f, center.y, 0.0f), parked);
    expect(game.score().hits == 1,
           "valid short-gap swings still work after tracking resumes");
}

void testWrongHand() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(1000, 2, 1, movebeat::HAND_RIGHT, movebeat::CUT_ANY));
    movebeat::Game game;
    game.load(chart);
    const movebeat::Vec3 center = game.notePosition(chart.notes[0], 1000);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    game.update(984, pose(center.x - 0.4f, center.y, 0.0f), parked);
    game.update(1000, pose(center.x + 0.4f, center.y, 0.0f), parked);
    expect(game.score().hits == 0, "left saber cannot cut a right-hand note");
}

void testMultiplier() {
    movebeat::Chart chart;
    for (unsigned int i = 0; i < 5; ++i) {
        chart.notes.push_back(makeNote(1000 + i * 500, 1, 1,
                                      movebeat::HAND_LEFT, movebeat::CUT_RIGHT));
    }
    movebeat::Game game;
    game.load(chart);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    for (std::size_t i = 0; i < chart.notes.size(); ++i) {
        const movebeat::Note& note = chart.notes[i];
        const movebeat::Vec3 center = game.notePosition(note, note.time_ms);
        game.update(note.time_ms - 16, pose(center.x - 0.4f, center.y, 0.0f), parked);
        game.update(note.time_ms, pose(center.x, center.y, 0.0f), parked);
    }
    expect(game.score().hits == 5, "five sequential notes are hit");
    expect(game.score().max_combo == 5, "combo reaches five");
    expect(game.score().score == 90, "fifth centered note receives x2 multiplier");
}

void testFrameRateIndependentSwingAndStacks() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(1000, 1, 1, movebeat::HAND_LEFT,
                                   movebeat::CUT_RIGHT));
    chart.notes.push_back(makeNote(1000, 2, 1, movebeat::HAND_LEFT,
                                   movebeat::CUT_RIGHT));
    movebeat::Game game;
    game.load(chart);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    game.update(980, pose(-0.50f, 0.0f, 0.0f), parked);
    game.update(1000, pose(0.50f, 0.0f, 0.0f), parked);
    expect(game.score().hits == 2, "one physical swing can cut a two-note stack");
    expect(game.events().cut_visual_count == 2u,
           "stacked hits produce separate visual bursts");

    movebeat::Chart slow_chart;
    slow_chart.notes.push_back(makeNote(1000, 1, 1, movebeat::HAND_LEFT,
                                        movebeat::CUT_RIGHT));
    movebeat::Game slow_game;
    slow_game.load(slow_chart);
    const movebeat::Vec3 center = slow_game.notePosition(slow_chart.notes[0], 1000);
    slow_game.update(900, pose(center.x - 0.05f, center.y, 0.0f), parked);
    slow_game.update(1000, pose(center.x + 0.05f, center.y, 0.0f), parked);
    expect(slow_game.score().hits == 0, "slow movement through a note is not a cut");
}

void testPreAndPostSwingQuality() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(1000, 1, 1, movebeat::HAND_LEFT,
                                   movebeat::CUT_RIGHT));
    movebeat::Game game;
    game.load(chart);
    const movebeat::Vec3 center = game.notePosition(chart.notes[0], 1000);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    game.update(984, angledPose(center.x - 0.4f, center.y, 0.0f, -100.0f), parked);
    game.update(1000, angledPose(center.x, center.y, 0.0f, 0.0f), parked);
    expect(game.score().score == 85,
           "100-degree pre-swing plus centered contact awards 85 points");
    expect(!game.finished(1000),
           "game waits briefly for post-swing scoring after the final cut");
    game.update(1016, angledPose(center.x + 0.6f, center.y, 0.0f, 60.0f), parked);
    expect(game.score().score == 115,
           "60-degree follow-through raises a perfect cut to 115 points");
    expect(game.finished(1016),
           "full follow-through finalizes the cut without waiting for timeout");

    movebeat::Game stopped_game;
    stopped_game.load(chart);
    stopped_game.update(984, pose(center.x - 0.4f, center.y, 0.0f), parked);
    stopped_game.update(1000, pose(center.x, center.y, 0.0f), parked);
    stopped_game.update(1200, pose(center.x, center.y, 0.0f), parked);
    expect(stopped_game.score().score == 15 && stopped_game.finished(1200),
           "stopping after contact earns no angle bonus and finalizes on timeout");

    movebeat::Game dropout_game;
    dropout_game.load(chart);
    dropout_game.update(984,
        angledPose(center.x - 0.4f, center.y, 0.0f, -100.0f), parked);
    dropout_game.update(1000,
        angledPose(center.x, center.y, 0.0f, 0.0f), parked);
    dropout_game.update(1016, movebeat::SaberPose(), parked);
    dropout_game.update(1032,
        angledPose(center.x + 0.6f, center.y, 0.0f, 120.0f), parked);
    dropout_game.update(1200,
        angledPose(center.x + 0.6f, center.y, 0.0f, 120.0f), parked);
    expect(dropout_game.score().score == 85,
           "tracking reacquisition does not turn an orientation jump into post-swing score");

    movebeat::Game sampled_game;
    sampled_game.load(chart);
    sampled_game.update(968,
        angledPose(center.x - 0.5f, center.y, 0.0f, -100.0f), parked);
    sampled_game.update(984,
        angledPose(center.x - 0.4f, center.y, 0.0f, -50.0f), parked);
    sampled_game.update(1000,
        angledPose(center.x, center.y, 0.0f, 0.0f), parked);
    sampled_game.update(1016,
        angledPose(center.x + 0.5f, center.y, 0.0f, 30.0f), parked);
    sampled_game.update(1032,
        angledPose(center.x + 0.6f, center.y, 0.0f, 60.0f), parked);
    expect(sampled_game.score().score == 115,
           "angle score is stable when the same swing is split across more frames");

    movebeat::Game off_center_game;
    off_center_game.load(chart);
    off_center_game.update(984,
        movebeat::SaberPose(
            movebeat::Vec3(center.x - 0.8f, center.y + 0.14f, 0.0f),
            movebeat::Vec3(center.x - 0.4f, center.y + 0.14f, 0.0f), true),
        parked);
    off_center_game.update(1000,
        movebeat::SaberPose(
            movebeat::Vec3(center.x - 0.4f, center.y + 0.14f, 0.0f),
            movebeat::Vec3(center.x, center.y + 0.14f, 0.0f), true), parked);
    expect(off_center_game.score().score == 8,
           "a half-radius offset receives about half of the 15 center points");
}

void testFollowThroughDeadline() {
    movebeat::Chart chart;
    chart.notes.push_back(makeNote(1000, 1, 1, movebeat::HAND_LEFT,
                                   movebeat::CUT_RIGHT));
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    for (unsigned int delay = 200; delay <= 201; ++delay) {
        movebeat::Game game;
        game.load(chart);
        const movebeat::Vec3 center = game.notePosition(chart.notes[0], 1000);
        game.update(984, angledPose(center.x - 0.4f, center.y, 0, -100), parked);
        game.update(1000, angledPose(center.x, center.y, 0, 0), parked);
        game.update(1190, angledPose(center.x + 0.4f, center.y, 0, 0), parked);
        game.update(1000 + delay,
                    angledPose(center.x + 0.6f, center.y, 0, 60), parked);
        expect(game.score().score == (delay == 200 ? 115u : 85u),
               "follow-through is accepted at the deadline, but not after it");
        expect(game.finished(1000 + delay), "deadline settles the last cut");
    }
    movebeat::Game game;
    game.load(chart);
    const movebeat::Vec3 center = game.notePosition(chart.notes[0], 1000);
    game.update(984, angledPose(center.x - 0.4f, center.y, 0, -100), parked);
    game.update(1000, angledPose(center.x, center.y, 0, 0), parked);
    game.update(1000, angledPose(center.x + 0.6f, center.y, 0, 60), parked);
    expect(game.score().score == 85,
           "a second pose with the same timestamp cannot award follow-through");
}

void testBombs() {
    movebeat::Chart chart;
    movebeat::Note bomb = makeNote(1000, 1, 1, movebeat::HAND_NONE, movebeat::CUT_ANY);
    bomb.kind = movebeat::NOTE_BOMB;
    chart.notes.push_back(bomb);

    movebeat::Game hit_game;
    hit_game.load(chart);
    const movebeat::Vec3 center = hit_game.notePosition(bomb, 1000);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    hit_game.update(1000, pose(center.x, center.y, 0.0f), parked);
    expect(hit_game.score().bombs_hit == 1, "touching a bomb records a bomb hit");
    expect(hit_game.events().bomb_hit, "bomb contact reports strong feedback");
    expect(hit_game.score().misses == 1, "bomb hit breaks the combo like a miss");

    movebeat::Game avoid_game;
    avoid_game.load(chart);
    avoid_game.update(1200, parked, parked);
    expect(avoid_game.score().bombs_avoided == 1, "expired untouched bomb is avoided");
    expect(avoid_game.score().misses == 0, "avoiding a bomb is not a miss");
}

void testObstacles() {
    movebeat::Chart chart;
    movebeat::Obstacle obstacle;
    obstacle.time_ms = 1000;
    obstacle.duration_ms = 500;
    obstacle.lane = 1;
    obstacle.width = 2;
    obstacle.row = 1;
    obstacle.height = 1;
    chart.obstacles.push_back(obstacle);

    movebeat::Game hit_game;
    hit_game.load(chart);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);
    hit_game.update(1000, pose(0.0f, 0.0f, 0.0f), parked);
    expect(hit_game.score().obstacles_hit == 1,
           "touching an active wall records an obstacle hit");
    expect(hit_game.events().obstacle_hit && hit_game.events().miss,
           "wall contact reports penalty feedback");
    expect(hit_game.score().energy == 38 && hit_game.score().misses == 1,
           "wall contact drains energy and breaks combo");
    expect(!hit_game.obstaclePending(0), "touched wall is consumed once");

    movebeat::Game avoid_game;
    avoid_game.load(chart);
    avoid_game.update(1500, parked, parked);
    expect(!avoid_game.finished(1500), "wall remains active through its end time");
    avoid_game.update(1501, parked, parked);
    expect(avoid_game.score().obstacles_avoided == 1,
           "untouched expired wall is recorded as avoided");
    expect(avoid_game.finished(1501), "obstacle-only chart finishes after the wall");
    expect(avoid_game.obstacleCount() == 1,
           "renderer can inspect chart obstacle count");
}

void testChainLinks() {
    movebeat::Chart chart;
    movebeat::Note link = makeNote(1000, 1, 1, movebeat::HAND_LEFT,
                                   movebeat::CUT_ANY);
    link.kind = movebeat::NOTE_CHAIN;
    chart.notes.push_back(link);
    const movebeat::SaberPose parked = pose(10.0f, 10.0f, 0.0f);

    movebeat::Game hit_game;
    hit_game.load(chart);
    const movebeat::Vec3 center = hit_game.notePosition(link, 1000);
    hit_game.update(984, pose(center.x - 0.4f, center.y, 0.0f), parked);
    hit_game.update(1000, pose(center.x, center.y, 0.0f), parked);
    expect(hit_game.score().chain_links_hit == 1 && hit_game.score().hits == 1,
           "chain link follows the continuous swing and counts as a hit");
    expect(hit_game.score().score == 20,
           "chain link has reduced score weight");

    movebeat::Game miss_game;
    miss_game.load(chart);
    miss_game.update(1200, parked, parked);
    expect(miss_game.score().chain_links_missed == 1 &&
           miss_game.score().energy == 45,
           "missed chain link has a reduced energy penalty");
}

void testArcs() {
    movebeat::Chart chart;
    movebeat::Arc arc;
    arc.head_time_ms = 1000;
    arc.tail_time_ms = 1500;
    arc.head_lane = 0;
    arc.head_row = 0;
    arc.head_direction = movebeat::CUT_UP;
    arc.head_control_point_multiplier = 1.0f;
    arc.tail_lane = 3;
    arc.tail_row = 2;
    arc.tail_direction = movebeat::CUT_DOWN;
    arc.tail_control_point_multiplier = 1.0f;
    arc.hand = movebeat::HAND_LEFT;
    chart.arcs.push_back(arc);

    movebeat::Game game;
    game.load(chart);
    expect(game.arcCount() == 1 && game.arc(0).hand == movebeat::HAND_LEFT,
           "renderer can inspect arc metadata");
    const movebeat::Vec3 head = game.arcPoint(arc, 1000, 0.0f);
    const movebeat::Vec3 tail = game.arcPoint(arc, 1500, 1.0f);
    const movebeat::Vec3 middle = game.arcPoint(arc, 1250, 0.5f);
    expect(near(head.x, -0.825f) && near(head.y, -0.55f) && near(head.z, 0.0f),
           "arc starts at its head grid cell and time");
    expect(near(tail.x, 0.825f) && near(tail.y, 0.55f) && near(tail.z, 0.0f),
           "arc ends at its tail grid cell and time");
    expect(middle.y > 0.45f && near(middle.z, 0.0f),
           "arc endpoint cut directions bend its cubic visual path");

    movebeat::Arc clockwise;
    clockwise.head_time_ms = 1000;
    clockwise.tail_time_ms = 1500;
    clockwise.head_lane = clockwise.tail_lane = 1;
    clockwise.head_row = clockwise.tail_row = 1;
    clockwise.head_direction = clockwise.tail_direction = movebeat::CUT_UP;
    clockwise.hand = movebeat::HAND_LEFT;
    clockwise.mid_anchor_mode = 1;
    movebeat::Arc counter_clockwise = clockwise;
    counter_clockwise.mid_anchor_mode = 2;
    movebeat::Arc straight = clockwise;
    straight.mid_anchor_mode = 0;
    const movebeat::Vec3 clockwise_head = game.arcPoint(clockwise, 1000, 0.0f);
    const movebeat::Vec3 clockwise_tail = game.arcPoint(clockwise, 1500, 1.0f);
    const movebeat::Vec3 clockwise_mid = game.arcPoint(clockwise, 1250, 0.5f);
    const movebeat::Vec3 counter_clockwise_mid =
        game.arcPoint(counter_clockwise, 1250, 0.5f);
    const movebeat::Vec3 straight_mid = game.arcPoint(straight, 1250, 0.5f);
    expect(near(clockwise_head.x, -0.275f) &&
           near(clockwise_tail.x, -0.275f) &&
           clockwise_mid.x > straight_mid.x + 1.0f &&
           counter_clockwise_mid.x < straight_mid.x - 1.0f,
           "mid-anchor modes curve clockwise and counter-clockwise without moving endpoints");
    const movebeat::Vec3 moving_mid = game.arcPoint(clockwise, 1200, 0.5f);
    expect(near(moving_mid.z, 0.2f),
           "mid-anchor shape keeps the normal time-based travel depth");
    movebeat::Arc invalid_mid_anchor = clockwise;
    invalid_mid_anchor.tail_lane = 2;
    const movebeat::Vec3 invalid_mid =
        game.arcPoint(invalid_mid_anchor, 1250, 0.5f);
    movebeat::Arc ordinary_invalid = invalid_mid_anchor;
    ordinary_invalid.mid_anchor_mode = 0;
    const movebeat::Vec3 ordinary_invalid_mid =
        game.arcPoint(ordinary_invalid, 1250, 0.5f);
    expect(near(invalid_mid.x, ordinary_invalid_mid.x) &&
           near(invalid_mid.y, ordinary_invalid_mid.y),
           "mid-anchor rotation is ignored when its lane precondition is invalid");
    expect(!game.finished(1500), "arc-only chart remains active through the tail");
    expect(game.finished(1501), "arc-only chart finishes after the tail");

    movebeat::Chart followed_chart;
    movebeat::Arc followed;
    followed.head_time_ms = 1000;
    followed.tail_time_ms = 1500;
    followed.head_lane = followed.tail_lane = 1;
    followed.head_row = followed.tail_row = 1;
    followed.head_direction = followed.tail_direction = movebeat::CUT_ANY;
    followed.hand = movebeat::HAND_LEFT;
    followed_chart.arcs.push_back(followed);
    movebeat::Game follow_game;
    follow_game.load(followed_chart);
    const movebeat::SaberPose parked = pose(8.0f, 8.0f, 8.0f);
    follow_game.update(990, pose(-0.275f, 0.0f, 0.0f), parked);
    for (unsigned int time = 1015; time <= 1515; time += 25) {
        const float progress = time >= 1500 ? 1.0f :
            static_cast<float>(time - 1000) / 500.0f;
        const movebeat::Vec3 target = follow_game.arcPoint(followed,
            time < 1500 ? time : 1500, progress);
        follow_game.update(time, pose(target.x, target.y, target.z), parked);
    }
    expect(follow_game.score().arcs_hit == 1 &&
           follow_game.score().arcs_missed == 0 &&
           follow_game.score().score > 0,
           "following the assigned saber along an arc awards score");

    movebeat::Game miss_game;
    miss_game.load(followed_chart);
    miss_game.update(990, parked, parked);
    miss_game.update(1515, parked, parked);
    expect(miss_game.score().arcs_missed == 1 &&
           miss_game.score().energy == 42,
           "ignoring an arc registers a reduced miss penalty");
}

void testLevelManifest() {
    std::istringstream input(
        "format=1\nsource=beatsaver\nsource_key=abc\ntitle=Example\n"
        "song_author=Artist\nmapper=Mapper\nbpm=120\naudio=audio.ogg\n"
        "cover=cover.png\nchart_count=1\n"
        "chart0_characteristic=Standard\nchart0_difficulty=Hard\n"
        "chart0_file=Standard_Hard.mbchart\nchart0_notes=42\n"
        "chart0_obstacles=7\nchart0_chains=12\nchart0_arcs=4\nchart0_skipped=3\n"
        "chart0_njs=18\nchart0_start_beat_offset=0.5\n");
    movebeat::LevelManifest level;
    std::string error;
    expect(movebeat::loadLevelManifest(input, level, error), "BeatSaver level manifest parses");
    expect(level.source_key == "abc", "manifest preserves BeatSaver key");
    expect(level.charts.size() == 1 && level.charts[0].notes == 42,
           "manifest exposes chart metadata");
    expect(level.charts[0].obstacles == 7,
           "manifest exposes imported obstacle count");
    expect(level.charts[0].chains == 12,
           "manifest exposes imported chain-link count");
    expect(level.charts[0].arcs == 4,
           "manifest exposes imported arc count");
    expect(near(level.charts[0].note_jump_speed, 18.0f) &&
           near(level.charts[0].start_beat_offset, 0.5f),
           "manifest preserves per-chart note jump metadata");

    std::istringstream unsafe(
        "format=1\ntitle=X\nbpm=100\naudio=../song.ogg\nchart_count=1\n"
        "chart0_characteristic=Standard\nchart0_difficulty=Easy\n"
        "chart0_file=x.mbchart\nchart0_notes=1\nchart0_skipped=0\n");
    expect(!movebeat::loadLevelManifest(unsafe, level, error),
           "manifest rejects path traversal");

    std::istringstream invalid_jump(
        "format=1\ntitle=X\nbpm=100\naudio=song.ogg\nchart_count=1\n"
        "chart0_characteristic=Standard\nchart0_difficulty=Easy\n"
        "chart0_file=x.mbchart\nchart0_notes=1\nchart0_skipped=0\n"
        "chart0_njs=nan\n");
    expect(!movebeat::loadLevelManifest(invalid_jump, level, error),
           "manifest rejects non-finite note jump metadata");
}

void testNoteJumpConfiguration() {
    movebeat::GameConfig config;
    expect(movebeat::configureNoteJump(config, 120.0f, 18.0f, 0.5f),
           "valid Beat Saber note jump metadata configures the game");
    config.row_pitch_per_depth =
        -(movebeat::ps3::kProjectionEyeHeight +
          movebeat::ps3::kProjectionCameraHeight) /
        movebeat::ps3::kProjectionDepth;
    expect(near(config.note_travel_speed, 7.2f) &&
           near(config.note_spawn_depth, 9.0f),
           "NJS and spawn offset produce matching approach speed and distance");
    movebeat::Game game;
    game.setConfig(config);
    movebeat::Note note = makeNote(1000, 1, 1,
                                   movebeat::HAND_LEFT, movebeat::CUT_ANY);
    expect(near(game.notePosition(note, 0).z, 7.2f) &&
           near(game.noteSpawnDepth(), 9.0f),
           "configured NJS changes actual gameplay motion and visible spawn plane");
    note.time_ms = 1250;
    movebeat::Note upper = note;
    upper.row = 2;
    movebeat::Note lower = note;
    lower.row = 0;
    expect(near(game.notePosition(upper, 0).y - game.notePosition(note, 0).y,
                config.row_spacing) &&
           near(game.notePosition(upper, 1250).y -
                game.notePosition(note, 1250).y, config.row_spacing) &&
           game.notePosition(lower, 0).y < -config.row_spacing &&
           near(game.notePosition(lower, 1250).y, -config.row_spacing),
           "all rows follow the pitched sightline without losing vertical spacing");
    const movebeat::ps3::ScreenPoint strike = movebeat::ps3::projectNotePoint(
        movebeat::Vec3(0.0f, 0.0f, 0.0f), 1280, 720);
    const movebeat::ps3::ScreenPoint upper_spawn = movebeat::ps3::projectNotePoint(
        game.notePosition(upper, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint upper_strike = movebeat::ps3::projectNotePoint(
        game.notePosition(upper, 1250), 1280, 720);
    const movebeat::ps3::ScreenPoint lower_spawn = movebeat::ps3::projectNotePoint(
        game.notePosition(lower, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint lower_strike = movebeat::ps3::projectNotePoint(
        game.notePosition(lower, 1250), 1280, 720);
    expect(upper_spawn.y < strike.y && upper_strike.y < upper_spawn.y &&
           lower_spawn.y > strike.y && lower_strike.y > lower_spawn.y,
           "NJS-adjusted spawn depth keeps upper and lower notes on opposite trajectories");
    const movebeat::ps3::ScreenPoint upper_track_spawn =
        movebeat::ps3::projectWorldPoint(game.notePosition(upper, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint upper_track_strike =
        movebeat::ps3::projectWorldPoint(game.notePosition(upper, 1250), 1280, 720);
    const movebeat::ps3::ScreenPoint lower_track_spawn =
        movebeat::ps3::projectWorldPoint(game.notePosition(lower, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint lower_track_strike =
        movebeat::ps3::projectWorldPoint(game.notePosition(lower, 1250), 1280, 720);
    expect(upper_track_strike.y < upper_track_spawn.y &&
           lower_track_strike.y > lower_track_spawn.y,
           "rendered upper cubes rise and lower cubes descend toward the player");

    movebeat::Arc upper_arc;
    upper_arc.head_time_ms = 1250;
    upper_arc.tail_time_ms = 1750;
    upper_arc.head_lane = upper_arc.tail_lane = 1;
    upper_arc.head_row = upper_arc.tail_row = 2;
    upper_arc.head_direction = upper_arc.tail_direction = movebeat::CUT_ANY;
    const movebeat::Vec3 upper_arc_head = game.arcPoint(upper_arc, 0, 0.0f);
    const movebeat::Vec3 upper_note_spawn = game.notePosition(upper, 0);
    expect(near(upper_arc_head.x, upper_note_spawn.x) &&
           near(upper_arc_head.y, upper_note_spawn.y) &&
           near(upper_arc_head.z, upper_note_spawn.z),
           "upper-row arc starts on the same perspective path as its note");
    movebeat::Arc lower_arc = upper_arc;
    lower_arc.head_row = lower_arc.tail_row = 0;
    const movebeat::Vec3 lower_arc_head = game.arcPoint(lower_arc, 0, 0.0f);
    const movebeat::Vec3 lower_note_spawn = game.notePosition(lower, 0);
    expect(near(lower_arc_head.x, lower_note_spawn.x) &&
           near(lower_arc_head.y, lower_note_spawn.y) &&
           near(lower_arc_head.z, lower_note_spawn.z),
           "lower-row arc starts on the same perspective path as its note");

    const movebeat::GameConfig previous = config;
    expect(!movebeat::configureNoteJump(config, 0.0f, 18.0f, 0.0f) &&
           near(config.note_travel_speed, previous.note_travel_speed) &&
           near(config.note_spawn_depth, previous.note_spawn_depth),
           "invalid note jump metadata leaves safe defaults unchanged");
}

void testEnergyFailure() {
    movebeat::Chart chart;
    for (unsigned int index = 0; index < 5; ++index) {
        chart.notes.push_back(makeNote(1000 + index * 100, 1, 1,
                                      movebeat::HAND_LEFT, movebeat::CUT_ANY));
    }
    movebeat::Game game;
    game.load(chart);
    const movebeat::SaberPose untracked;
    game.update(2000, untracked, untracked);
    expect(game.score().energy == 0, "five misses drain the starting energy");
    expect(game.score().failed, "zero energy fails the level");
    expect(game.finished(2000), "failed level is finished");
}

void testHighScores() {
    const std::string key = movebeat::makeScoreKey("547fb", "Standard_Expert.mbchart");
    expect(key.size() == 16, "score key is a fixed-width identifier");
    expect(key == movebeat::makeScoreKey("547fb", "Standard_Expert.mbchart"),
           "score key is deterministic");
    expect(key != movebeat::makeScoreKey("547fb", "Standard_Hard.mbchart"),
           "difficulty receives a distinct score key");

    std::ostringstream rows;
    rows << "broken row\n"
         << key << " 1200 12\n"
         << "0000000000000000 9000 99\n"
         << key << " 2400 20\n";
    std::istringstream input(rows.str());
    movebeat::HighScore best;
    std::string error;
    expect(movebeat::readBestScore(input, key, best, error),
           "score log tolerates malformed unrelated rows");
    expect(best.score == 2400 && best.max_combo == 20,
           "score log returns the best matching run");
}

void testSettings() {
    std::istringstream valid(
        "format=1\ntiming_offset_ms=-130\nblade_length_cm=105\n"
        "invert_hands=1\n");
    movebeat::Settings settings;
    std::string error;
    expect(movebeat::loadSettings(valid, settings, error),
           "timing settings parse");
    expect(settings.timing_offset_ms == -130,
           "signed timing offset is preserved");
    expect(settings.blade_length_cm == 105,
           "blade length is preserved");
    expect(settings.invert_hands,
           "manual hand inversion is preserved");

    std::istringstream legacy("format=1\ntiming_offset_ms=20\n");
    expect(movebeat::loadSettings(legacy, settings, error),
           "settings without blade length remain compatible");
    expect(settings.blade_length_cm == movebeat::Settings::DEFAULT_BLADE_LENGTH_CM,
           "legacy settings use the default blade length");
    expect(!settings.invert_hands,
           "legacy settings retain automatic hand assignment");

    std::istringstream unsafe("format=1\ntiming_offset_ms=900\n");
    expect(!movebeat::loadSettings(unsafe, settings, error),
           "out-of-range timing offset is rejected");
    std::istringstream unsafe_blade(
        "format=1\ntiming_offset_ms=0\nblade_length_cm=200\n");
    expect(!movebeat::loadSettings(unsafe_blade, settings, error),
           "out-of-range blade length is rejected");
    std::istringstream unsafe_hands(
        "format=1\ntiming_offset_ms=0\ninvert_hands=maybe\n");
    expect(!movebeat::loadSettings(unsafe_hands, settings, error),
           "invalid hand inversion setting is rejected");
    std::istringstream future("format=2\ntiming_offset_ms=0\n");
    expect(!movebeat::loadSettings(future, settings, error),
           "unknown settings format is rejected");
}

void testInputInvalidation() {
    movebeat::Chart chart;
    movebeat::Note note;
    note.time_ms = 1000;
    note.lane = 1;
    note.row = 1;
    note.hand = movebeat::HAND_LEFT;
    note.direction = movebeat::CUT_ANY;
    chart.notes.push_back(note);
    movebeat::Game normal;
    movebeat::Game recovered;
    normal.load(chart);
    recovered.load(chart);
    const movebeat::Vec3 center = normal.notePosition(note, 1000);
    const movebeat::SaberPose before(
        center + movebeat::Vec3(-0.2f, -0.2f, 0.0f),
        center + movebeat::Vec3(-0.2f, 0.0f, 0.0f), true);
    const movebeat::SaberPose after(
        center + movebeat::Vec3(0.2f, -0.2f, 0.0f),
        center + movebeat::Vec3(0.2f, 0.0f, 0.0f), true);
    normal.update(900, before, movebeat::SaberPose());
    recovered.update(900, before, movebeat::SaberPose());
    recovered.invalidateInput();
    normal.update(1000, after, movebeat::SaberPose());
    recovered.update(1000, after, movebeat::SaberPose());
    expect(normal.score().hits == 1 && recovered.score().hits == 0 &&
           recovered.remainingNotes() == 1,
           "resuming after tracking loss cannot turn a pose jump into a cut");
}

#if defined(__PPU__)
void testPpuMoveInputMapping() {
    gemState state = {};
    state.handle_pos = (vec_float4){ 100.0f, 200.0f, 300.0f, 1.0f };
    state.pos = (vec_float4){ 100.0f, 1200.0f, 300.0f, 1.0f };
    state.tracking = GEM_TRACKING_POSITION_TRACKED | GEM_TRACKING_VISIBLE;
    state.paddata.buttons = 0x48;
    state.paddata.ANA_T = 173;
    movebeat::ps3::MoveFrame frame;
    movebeat::ps3::applyGemSample(0, state, 0, 0.8f, frame);
    expect(frame.left.tracked, "PPU Move sample is tracked");
    expect(near(frame.left.handle.x, 0.1f) &&
           near(frame.left.handle.y, 0.2f) &&
           near(frame.left.handle.z, 0.3f),
           "PPU libgem axes retain x-right, y-up and z-away orientation");
    expect(near(frame.left.tip.x, 0.1f) &&
           near(frame.left.tip.y, 2.0f) &&
           near(frame.left.tip.z, 0.3f),
           "PPU blade extends through the sphere");
    expect(frame.left_buttons == 0x48 && frame.left_trigger == 173,
           "PPU buttons and trigger share the pose sample");
    expect(movebeat::ps3::gemResultNeedsCalibration(
               movebeat::ps3::GEM_STATUS_SPHERE_NOT_CALIBRATED) &&
           movebeat::ps3::gemResultNeedsCalibration(
               movebeat::ps3::GEM_STATUS_NOT_CALIBRATED) &&
           !movebeat::ps3::gemResultNeedsCalibration(
               movebeat::ps3::GEM_STATUS_SPHERE_CALIBRATING),
           "PPU runtime status classification starts calibration exactly when needed");
    expect(movebeat::ps3::gemResultNeedsHues(
               movebeat::ps3::GEM_STATUS_HUE_NOT_SET),
           "PPU runtime status classification requests automatic hues");
    expect(movebeat::ps3::gemSlotReady(movebeat::ps3::GEM_CONNECTION_READY) &&
           !movebeat::ps3::gemSlotReady(
               movebeat::ps3::GEM_CONNECTION_DISCONNECTED),
           "PPU connection status is evaluated per Move slot");
}
#endif

} // namespace

int main() {
    checkpoint("tracking pause");
    movebeat::ps3::TrackingPauseGate tracking_gate;
    expect(!tracking_gate.observe(1000u, false) &&
           !tracking_gate.observe(1100u, false) &&
           !tracking_gate.observe(1200u, true) &&
           !tracking_gate.observe(1300u, false) &&
           !tracking_gate.observe(1549u, false) &&
           tracking_gate.observe(1550u, false) &&
           !tracking_gate.observe(1600u, false),
           "tracking loss pauses only after a sustained gap, once");
    tracking_gate.reset();
    expect(!tracking_gate.observe(2000u, true) &&
           !tracking_gate.observe(2100u, false),
           "a recovered session can detect a later loss");
    expect(movebeat::ps3::unpausedElapsedMs(3500000ull, 1000000ull,
                                           1500000ull, true) == 500u &&
           movebeat::ps3::unpausedElapsedMs(3500000ull, 3000000ull,
                                           0ull, false) == 500u,
           "non-audio level time freezes at pause onset");
    checkpoint("projection");
    const movebeat::ps3::ScreenPoint middle = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(0, 0, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint upper = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(0, 0.55f, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint strike_origin = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(0, 0, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint far = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(0.825f, 0.55f, movebeat::ps3::kVisibleTrackDepth), 1280, 720);
    expect(middle.x == 640 && middle.y == 360,
           "the strike plane is centered in the camera view");
    const movebeat::ps3::ScreenPoint centerline_far =
        movebeat::ps3::projectWorldPoint(
            movebeat::Vec3(0.0f, movebeat::ps3::kProjectionEyeHeight,
                           movebeat::ps3::kVisibleTrackDepth), 1280, 720);
    expect(centerline_far.x == 640,
           "the straight-ahead vanishing line stays centered across depth");
    const movebeat::ps3::ScreenPoint near_lane_left =
        movebeat::ps3::projectWorldPoint(movebeat::Vec3(-0.55f, 0.0f, 0.0f),
                                        1280, 720);
    const movebeat::ps3::ScreenPoint near_lane_right =
        movebeat::ps3::projectWorldPoint(movebeat::Vec3(0.55f, 0.0f, 0.0f),
                                         1280, 720);
    const movebeat::ps3::ScreenPoint far_lane_left =
        movebeat::ps3::projectWorldPoint(
            movebeat::Vec3(-0.55f, 0.0f, movebeat::ps3::kVisibleTrackDepth),
            1280, 720);
    const movebeat::ps3::ScreenPoint far_lane_right =
        movebeat::ps3::projectWorldPoint(
            movebeat::Vec3(0.55f, 0.0f, movebeat::ps3::kVisibleTrackDepth),
            1280, 720);
    const int near_lane_width = near_lane_right.x - near_lane_left.x;
    const int far_lane_width = far_lane_right.x - far_lane_left.x;
    expect(far_lane_width > 0 && near_lane_width * 4 >= far_lane_width * 5,
           "the playfield visibly widens toward the player like a true perspective view");
    expect(upper.y < strike_origin.y,
           "positive world Y projects upward on screen");
    const movebeat::ps3::ScreenPoint upper_spawn =
        movebeat::ps3::projectNotePoint(movebeat::Vec3(0.0f, 0.55f,
            movebeat::ps3::kVisibleTrackDepth), 1280, 720);
    const movebeat::ps3::ScreenPoint upper_strike =
        movebeat::ps3::projectNotePoint(movebeat::Vec3(0.0f, 0.55f, 0.0f),
                                        1280, 720);
    const movebeat::ps3::ScreenPoint lower_spawn =
        movebeat::ps3::projectNotePoint(movebeat::Vec3(0.0f, -0.55f,
            movebeat::ps3::kVisibleTrackDepth), 1280, 720);
    const movebeat::ps3::ScreenPoint lower_strike =
        movebeat::ps3::projectNotePoint(movebeat::Vec3(0.0f, -0.55f, 0.0f),
                                        1280, 720);
    expect(upper_strike.y < upper_spawn.y &&
           lower_strike.y > lower_spawn.y,
           "upper notes rise toward the player while lower notes descend");
    expect((upper_spawn.y - upper_strike.y) > 40 &&
           (lower_strike.y - lower_spawn.y) > 40 &&
           std::abs((upper_spawn.y - upper_strike.y) -
                    (lower_strike.y - lower_spawn.y)) < 64,
           "upper and lower note trajectories are clearly visible and balanced");
    movebeat::GameConfig trajectory_config;
    expect(movebeat::configureNoteJump(trajectory_config, 120.0f, 18.0f, 0.5f),
           "perspective trajectory test uses a long BeatSaver NJS spawn distance");
    trajectory_config.row_pitch_per_depth =
        -(movebeat::ps3::kProjectionEyeHeight +
          movebeat::ps3::kProjectionCameraHeight) /
        movebeat::ps3::kProjectionDepth;
    movebeat::Game trajectory_game(trajectory_config);
    movebeat::Note upper_note;
    upper_note.row = 2;
    upper_note.time_ms = 1250;
    movebeat::Note middle_note;
    middle_note.row = 1;
    middle_note.time_ms = 1250;
    movebeat::Note lower_note;
    lower_note.row = 0;
    lower_note.time_ms = 1250;
    expect(near(trajectory_game.notePosition(upper_note, 0).y -
                trajectory_game.notePosition(middle_note, 0).y,
                trajectory_config.row_spacing) &&
           near(trajectory_game.notePosition(upper_note, 1250).y -
                trajectory_game.notePosition(middle_note, 1250).y,
                trajectory_config.row_spacing),
           "upper row follows the sightline and stays above the middle row");
    const movebeat::ps3::ScreenPoint upper_note_far =
        movebeat::ps3::projectNotePoint(
            trajectory_game.notePosition(upper_note, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint upper_note_near =
        movebeat::ps3::projectNotePoint(
            trajectory_game.notePosition(upper_note, 1250), 1280, 720);
    const movebeat::ps3::ScreenPoint middle_note_far =
        movebeat::ps3::projectNotePoint(
            trajectory_game.notePosition(middle_note, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint middle_note_near =
        movebeat::ps3::projectNotePoint(
            trajectory_game.notePosition(middle_note, 1250), 1280, 720);
    const movebeat::ps3::ScreenPoint lower_note_far =
        movebeat::ps3::projectNotePoint(
            trajectory_game.notePosition(lower_note, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint lower_note_near =
        movebeat::ps3::projectNotePoint(
            trajectory_game.notePosition(lower_note, 1250), 1280, 720);
    expect(upper_note_near.y < upper_note_far.y &&
           lower_note_near.y > lower_note_far.y,
           "actual cube projection sends upper notes up and lower notes down");
    expect(upper_note_far.y < middle_note_far.y &&
           lower_note_far.y > middle_note_far.y &&
           upper_note_near.y < middle_note_near.y &&
           lower_note_near.y > middle_note_near.y,
           "upper and lower cube paths stay on opposite sides of the strike line");
    expect(upper_note_far.y - upper_note_near.y > 40 &&
           lower_note_near.y - lower_note_far.y > 40,
           "actual upper and lower cube trajectories visibly pass the strike plane");
    bool trajectories_keep_their_direction = true;
    movebeat::ps3::ScreenPoint previous_upper = upper_note_far;
    movebeat::ps3::ScreenPoint previous_lower = lower_note_far;
    for (unsigned int elapsed_ms = 50; elapsed_ms <= 1250; elapsed_ms += 50) {
        const movebeat::ps3::ScreenPoint upper_step =
            movebeat::ps3::projectNotePoint(
                trajectory_game.notePosition(upper_note, elapsed_ms),
                1280, 720);
        const movebeat::ps3::ScreenPoint lower_step =
            movebeat::ps3::projectNotePoint(
                trajectory_game.notePosition(lower_note, elapsed_ms),
                1280, 720);
        if (upper_step.y > previous_upper.y || upper_step.y >= middle.y ||
            lower_step.y < previous_lower.y || lower_step.y <= middle.y)
            trajectories_keep_their_direction = false;
        previous_upper = upper_step;
        previous_lower = lower_step;
    }
    expect(trajectories_keep_their_direction,
           "upper cubes rise and lower cubes descend without crossing the strike line during approach");
    movebeat::Note upper_middle_note;
    upper_middle_note.row = 2;
    upper_middle_note.time_ms = 1250;
    const movebeat::ps3::ScreenPoint upper_middle_far =
        movebeat::ps3::projectNotePoint(
            trajectory_game.notePosition(upper_middle_note, 0), 1280, 720);
    const movebeat::ps3::ScreenPoint upper_middle_near =
        movebeat::ps3::projectNotePoint(
            trajectory_game.notePosition(upper_middle_note, 1250), 1280, 720);
    expect(std::abs(middle_note_far.y - middle.y) <= 1 &&
           std::abs(middle_note_near.y - middle.y) <= 1 &&
           upper_middle_far.y < middle.y && upper_middle_near.y < middle.y,
           "the center row stays level while the upper row remains above the strike plane");
    expect(far.x > 640 && far.y > 0 && far.y < 320,
           "distant geometry remains above the strike origin at the finite spawn plane");
    const movebeat::ps3::ScreenPoint lane_near = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(-0.825f, 0.0f, 0.0f), 1280, 720);
    const movebeat::ps3::ScreenPoint lane_middle = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(-0.825f, 0.0f, movebeat::ps3::kVisibleTrackDepth / 2.0f), 1280, 720);
    const movebeat::ps3::ScreenPoint lane_far = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(-0.825f, 0.0f, movebeat::ps3::kVisibleTrackDepth), 1280, 720);
    expect((lane_near.y > lane_middle.y && lane_middle.y > lane_far.y) ||
           (lane_near.y < lane_middle.y && lane_middle.y < lane_far.y),
           "a note moves monotonically along the projected travel direction");
    expect(std::abs(lane_near.x - lane_far.x) <= 180,
           "a fixed-lane note has bounded lateral screen drift");
    const movebeat::ps3::ScreenPoint lane_left_edge = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(-1.1f, -0.825f, movebeat::ps3::kVisibleTrackDepth), 1280, 720);
    const movebeat::ps3::ScreenPoint lane_right_edge = movebeat::ps3::projectWorldPoint(
        movebeat::Vec3(-0.55f, -0.825f, movebeat::ps3::kVisibleTrackDepth), 1280, 720);
    expect(lane_left_edge.x < lane_far.x && lane_far.x < lane_right_edge.x,
           "spawned notes stay inside their projected track lane");
    const movebeat::ps3::NoteCubeMesh cube = movebeat::ps3::makeNoteCube(
        movebeat::Vec3(0.275f, -0.55f, 0.0f), 0.24f, 1280, 720,
        movebeat::ps3::gameplayNoteDepth(0.24f));
    expect(cube.world[0].z == cube.world[1].z &&
           cube.world[1].z == cube.world[2].z &&
           cube.world[4].z > cube.world[0].z,
           "solid note front faces the movement axis and has a separate rear face");
    expect(cube.world[0].x == cube.world[4].x &&
           cube.world[0].y == cube.world[4].y,
           "solid note depth edges run parallel to the movement axis");
    expect(std::abs((cube.world[4].z - cube.world[0].z) - 0.24f) < 0.0001f,
           "the production gameplay renderer uses true cubic note dimensions");
    expect(std::abs(movebeat::ps3::gameplayNoteDepth(0.24f) - 0.24f) < 0.0001f,
           "all render paths share true cubic gameplay note dimensions");
    expect(cube.world[0].z == cube.world[3].z &&
           cube.world[0].x == cube.world[3].x &&
           cube.world[1].x == cube.world[2].x &&
           cube.world[0].y == cube.world[1].y &&
           cube.world[2].y == cube.world[3].y,
           "cube has one uninterrupted rectangular front face");
    expect(cube.front[0].z == cube.world[0].z &&
           cube.front[0].x == cube.world[0].x &&
           cube.front[0].y == cube.world[0].y &&
           cube.front[2].x == cube.world[2].x &&
           cube.front[2].y == cube.world[2].y,
           "front is the full cube face with no inset frame");
    const int projected_front_width = std::abs(
        cube.screen[1].x - cube.screen[0].x);
    const int projected_front_height = std::abs(
        cube.screen[3].y - cube.screen[0].y);
    const int projected_top_depth = std::abs(
        cube.screen[4].y - cube.screen[0].y);
    const int projected_side_width = std::abs(
        cube.screen[5].x - cube.screen[1].x);
    expect(projected_front_width > 0 && projected_front_height > 0 &&
           projected_front_width * 5 >= projected_front_height * 4 &&
           projected_front_height * 5 >= projected_front_width * 4,
           "cube front remains close to face-on instead of becoming a side view");
    expect(projected_top_depth * 4 >= projected_front_width,
           "a near-frontal true cube exposes a readable top depth");
    expect(projected_side_width < projected_front_width / 3,
           "cube depth does not rotate its front toward a side view");
    const movebeat::ps3::ScreenPoint projected_track_center =
        movebeat::ps3::projectWorldPoint(
            movebeat::Vec3(0.275f, -0.55f, 0.0f), 1280, 720);
    const int projected_cube_center_x = (cube.screen[0].x + cube.screen[1].x +
        cube.screen[2].x + cube.screen[3].x + cube.screen[4].x +
        cube.screen[5].x + cube.screen[6].x + cube.screen[7].x) / 8;
    const int projected_cube_center_y = (cube.screen[0].y + cube.screen[1].y +
        cube.screen[2].y + cube.screen[3].y + cube.screen[4].y +
        cube.screen[5].y + cube.screen[6].y + cube.screen[7].y) / 8;
    expect(std::abs(projected_cube_center_x - projected_track_center.x) <= 2 &&
           std::abs(projected_cube_center_y - projected_track_center.y) <= 2,
           "local volume projection keeps each cube center on its track path");
    const movebeat::ps3::NoteCubeVisibleFaces right_lane_faces =
        movebeat::ps3::visibleNoteCubeFaces(movebeat::ps3::makeNoteCube(
            movebeat::Vec3(0.275f, -0.55f, 0.0f), 0.24f, 1280, 720));
    expect(right_lane_faces.left && !right_lane_faces.right &&
           right_lane_faces.top && !right_lane_faces.bottom,
           "player camera reveals the near side and top on a right-lane cube");
    const movebeat::ps3::NoteCubeMesh left_lane_cube =
        movebeat::ps3::makeNoteCube(movebeat::Vec3(-0.275f, 1.2f, 2.0f),
                                    0.24f, 1280, 720);
    const movebeat::ps3::NoteCubeVisibleFaces left_lane_faces =
        movebeat::ps3::visibleNoteCubeFaces(left_lane_cube);
    expect(!left_lane_faces.left && left_lane_faces.right &&
           !left_lane_faces.top && left_lane_faces.bottom,
           "camera below an upper cube reveals its underside and near side");
    const movebeat::ps3::NoteCubeMesh central_cube =
        movebeat::ps3::makeNoteCube(
            movebeat::Vec3(0.0f, movebeat::ps3::kProjectionEyeHeight, 2.0f),
            0.24f, 1280, 720);
    const movebeat::ps3::NoteCubeVisibleFaces central_faces =
        movebeat::ps3::visibleNoteCubeFaces(central_cube);
    expect(!central_faces.left && !central_faces.right &&
           central_faces.top && !central_faces.bottom,
           "center-lane cube faces the player head-on with its top plane visible");
    checkpoint("software 3D depth and frame clear");
    const int render_width = 320, render_height = 180;
    std::vector<std::uint32_t> pixels(render_width * render_height, 0xff000000u);
    std::vector<float> depth(render_width * render_height, 0.0f);
    const movebeat::ps3::SoftwareNoteTarget target = {
        pixels.data(), depth.data(), render_width, render_height
    };
    const movebeat::Vec3 render_center(
        -1.5f, -0.55f, 0.0f);
    const movebeat::ps3::NoteCubeMesh rear_cube = movebeat::ps3::makeNoteCube(
        movebeat::Vec3(-1.5f, -0.55f, 1.0f),
        0.4f, render_width, render_height);
    const movebeat::ps3::NoteCubeMesh front_cube = movebeat::ps3::makeNoteCube(
        render_center, 0.4f, render_width, render_height);
    const movebeat::Vec3 front_face_center(
        -1.5f, -0.55f, -0.2f);
    const movebeat::ps3::ScreenPoint track_center =
        movebeat::ps3::projectWorldPoint(
            render_center, render_width, render_height);
    const movebeat::ps3::ScreenPoint note_center =
        movebeat::ps3::projectNotePoint(
            render_center, render_width, render_height);
    const movebeat::ps3::ScreenPoint front_point =
        movebeat::ps3::projectNotePoint(
            front_face_center, render_width, render_height);
    const int center_x = front_point.x + track_center.x - note_center.x;
    const int center_y = front_point.y + track_center.y - note_center.y;
    const int center_pixel = center_y * render_width + center_x;
    movebeat::ps3::rasterizeNoteCube(target, front_cube, 0xffe83048u);
    movebeat::ps3::rasterizeNoteCube(target, rear_cube, 0xff2098e8u);
    const std::size_t front_face_pixels = static_cast<std::size_t>(
        std::count(pixels.begin(), pixels.end(), 0xffe83048u));
    const std::size_t top_face_pixels = static_cast<std::size_t>(
        std::count(pixels.begin(), pixels.end(),
                   movebeat::ps3::shadeSolidFace(0xffe83048u, 0.84f)));
    const std::size_t side_face_pixels = static_cast<std::size_t>(
        std::count(pixels.begin(), pixels.end(),
                   movebeat::ps3::shadeSolidFace(0xffe83048u, 0.54f)));
    expect(front_face_pixels > 0 && top_face_pixels > 0 && side_face_pixels > 0,
           "software rasterizer draws separate front, top and side cube planes");
    expect(top_face_pixels * 8 >= front_face_pixels &&
           side_face_pixels * 8 >= front_face_pixels,
           "rasterized top and side faces occupy visible areas, not edge lines");
    expect(pixels[center_pixel] == 0xffe83048u,
           "near 3D front face occludes a farther cube regardless of draw order");
    const float rear_front_depth = movebeat::ps3::toNoteCameraSpace(
        movebeat::Vec3(-1.5f, -0.55f, 1.0f)).depth;
    const float near_front_depth = movebeat::ps3::toNoteCameraSpace(
        movebeat::Vec3(-1.5f, -0.55f, -0.2f)).depth;
    expect(!movebeat::ps3::paintNoteInverseDepthPixel(
               target, center_x, center_y, 1.0f / rear_front_depth,
               0xffffffffu) &&
           pixels[center_pixel] == 0xffe83048u,
           "far cut arrow cannot show through a nearer cube");
    expect(movebeat::ps3::paintNoteInverseDepthPixel(
               target, center_x, center_y, 1.0f / near_front_depth,
               0xffffffffu) &&
           pixels[center_pixel] == 0xffffffffu,
           "a cut arrow remains visible on its own front face");
    std::fill(pixels.begin(), pixels.end(), 0xff000000u);
    std::fill(depth.begin(), depth.end(), 0.0f);
    movebeat::ps3::rasterizeNoteCube(target, rear_cube, 0xff2098e8u);
    movebeat::ps3::rasterizeNoteCube(target, front_cube, 0xffe83048u);
    expect(pixels[center_pixel] == 0xffe83048u,
           "near 3D front face occludes a farther cube in either draw order");
    std::fill(pixels.begin(), pixels.end(), 0xff000000u);
    std::fill(depth.begin(), depth.end(), 0.0f);
    expect(pixels[center_pixel] == 0xff000000u,
           "clearing the color and depth buffers removes the prior cube frame");
    checkpoint("library card window");
    expect(movebeat::ps3::libraryCardVisible(63, 0, 64) &&
           movebeat::ps3::libraryCardVisible(0, 0, 64) &&
           movebeat::ps3::libraryCardVisible(1, 0, 64) &&
           !movebeat::ps3::libraryCardVisible(2, 0, 64),
           "only selected and adjacent library cards remain visible");
    expect(movebeat::ps3::libraryCardVisible(0, 1, 2) &&
           movebeat::ps3::libraryCardVisible(1, 1, 2) &&
           !movebeat::ps3::libraryCardVisible(0, 0, 0),
           "two-track and empty libraries have safe card windows");
    checkpoint("frame timing");
    movebeat::ps3::FrameTimingWindow timing;
    timing.add(1000u);
    timing.add(20000u);
    timing.add(3000u);
    const movebeat::ps3::FrameTimingSample sample = timing.take();
    expect(sample.count == 3u && sample.average_us == 8000u &&
           sample.maximum_us == 20000u && sample.over_60hz_budget == 1u,
           "frame timing reports average, worst frame and 60 Hz misses");
    expect(timing.take().count == 0u,
           "frame timing resets after a diagnostic report");
    checkpoint("chart parser"); testChartParser();
    checkpoint("pose calibration"); testPoseCalibration();
    checkpoint("correct cut"); testCorrectCut();
    checkpoint("whole blade cut"); testWholeBladeCut();
    checkpoint("wrong direction and miss"); testWrongDirectionAndMiss();
    checkpoint("angle offset direction"); testAngleOffsetDirection();
    checkpoint("tracking sample gap"); testLongTrackingGapCannotCut();
    checkpoint("wrong hand"); testWrongHand();
    checkpoint("multiplier"); testMultiplier();
    checkpoint("frame-rate independence"); testFrameRateIndependentSwingAndStacks();
    checkpoint("swing quality"); testPreAndPostSwingQuality();
    checkpoint("follow-through deadline"); testFollowThroughDeadline();
    checkpoint("bombs"); testBombs();
    checkpoint("obstacles"); testObstacles();
    checkpoint("chain links"); testChainLinks();
    checkpoint("arcs"); testArcs();
    checkpoint("level manifest"); testLevelManifest();
    checkpoint("note jump settings"); testNoteJumpConfiguration();
    checkpoint("energy failure"); testEnergyFailure();
    checkpoint("high scores"); testHighScores();
    checkpoint("settings"); testSettings();
    checkpoint("input invalidation"); testInputInvalidation();
#if defined(__PPU__)
    checkpoint("PPU Move input mapping"); testPpuMoveInputMapping();
    checkpoint("PPU GemRuntime physical LED hue assignment");
    runGemRuntimeHueTests();
#endif
    if (failures == 0) {
        std::puts("All MoveBeat core tests passed.");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", failures);
    return EXIT_FAILURE;
}
