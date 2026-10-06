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
int runDrawBatchTests();
int runMenuTests();
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

// Confirms the pose with both Move buttons and holds it for one second at
// 60 Hz from `time` (settle 0.5 s, measure 0.3 s).
bool holdBodyPose(movebeat::BodyCalibration& calibration, unsigned int& time,
                  const movebeat::SaberPose& slot0, const movebeat::SaberPose& slot1) {
    calibration.confirm(0);
    calibration.confirm(1);
    bool finished = false;
    for (int frame = 0; frame < 64; ++frame, time += 17)
        finished = calibration.sample(time, slot0, slot1) || finished;
    return finished;
}

movebeat::SaberPose bodyPose(float hx, float hy, float hz, float tx, float ty, float tz) {
    return movebeat::SaberPose(movebeat::Vec3(hx, hy, hz), movebeat::Vec3(tx, ty, tz), true);
}

void testBodyCalibration() {
    using movebeat::BodyCalibration;
    movebeat::BodyCalibration calibration;
    unsigned int time = 1000;
    // Slot 0 is the player's right hand (camera +x).
    const movebeat::SaberPose forward0 = bodyPose(0.2f, 1.3f, 2.0f, 0.2f, 1.3f, 1.4f);
    const movebeat::SaberPose forward1 = bodyPose(-0.2f, 1.3f, 2.0f, -0.2f, 1.3f, 1.4f);

    // Nothing is measured before the Move button says "in the pose".
    for (int frame = 0; frame < 120; ++frame, time += 17)
        calibration.sample(time, forward0, forward1);
    expect(calibration.step() == BodyCalibration::STEP_FORWARD &&
           calibration.handState(0) == BodyCalibration::HAND_WAITING,
           "the calibration waits for the Move buttons");

    // One hand confirmed while the other sphere is lost: the tracked hand
    // is measured on its own; the lost one waits for a steady sphere.
    calibration.confirm(0);
    calibration.confirm(1);
    for (int frame = 0; frame < 64; ++frame, time += 17)
        calibration.sample(time, forward0, movebeat::SaberPose());
    expect(calibration.handState(0) == BodyCalibration::HAND_DONE &&
           calibration.handState(1) == BodyCalibration::HAND_SETTLING &&
           calibration.step() == BodyCalibration::STEP_FORWARD,
           "each hand is measured independently");
    // The second sphere appears; a one-frame dropout is skipped, a long
    // one starts its steady wait over.
    for (int frame = 0; frame < 10; ++frame, time += 17)
        calibration.sample(time, forward0, forward1);
    calibration.sample(time, forward0, movebeat::SaberPose());
    time += 17;
    for (int frame = 0; frame < 10; ++frame, time += 17)
        calibration.sample(time, forward0, forward1);
    expect(calibration.handState(1) == BodyCalibration::HAND_MEASURING,
           "a one-frame dropout does not restart the wait");
    for (int frame = 0; frame < 15; ++frame, time += 17)
        calibration.sample(time, forward0, movebeat::SaberPose());
    calibration.sample(time, forward0, forward1);
    time += 17;
    expect(calibration.handState(1) == BodyCalibration::HAND_SETTLING,
           "a dropout longer than 0.2 s starts the steady wait over");
    for (int frame = 0; frame < 40; ++frame, time += 17)
        calibration.sample(time, forward0, forward1);
    expect(calibration.step() == BodyCalibration::STEP_SIDES &&
           calibration.handState(0) == BodyCalibration::HAND_WAITING,
           "both hands measured move on to the sideways pose, waiting again");

    // Whatever the player confirms is accepted; doubtful values are flagged.
    expect(!holdBodyPose(calibration, time, bodyPose(0.3f, 1.3f, 2.0f, 0.8f, 1.3f, 2.0f),
                         bodyPose(-0.3f, 1.3f, 2.0f, -0.8f, 1.3f, 2.0f)) &&
           calibration.step() == BodyCalibration::STEP_DOWN,
           "a narrow sideways pose is accepted");
    expect(holdBodyPose(calibration, time, bodyPose(0.25f, 1.9f, 2.0f, 0.25f, 2.4f, 2.0f),
                        bodyPose(-0.25f, 1.9f, 2.0f, -0.25f, 2.4f, 2.0f)) &&
           calibration.done(), "arms down completes the calibration");
    expect(calibration.narrowSides() && calibration.invertedUp(),
           "a narrow sideways pose and arms down above arms forward are flagged");
    expect(calibration.slotsSwapped(), "slot 0 further right makes slot 1 the left hand");
    expect(near(calibration.frame().scale, BodyCalibration::maximumScale()),
           "an implausible span keeps the scale within limits");

    // A blade turned round in the forward pose is refused: both hands are
    // asked again; agreeing blades then pass.
    movebeat::BodyCalibration turned_round;
    expect(!holdBodyPose(turned_round, time, forward0,
                         bodyPose(-0.2f, 1.3f, 2.0f, -0.2f, 1.3f, 2.6f)) &&
           turned_round.step() == BodyCalibration::STEP_FORWARD &&
           turned_round.directionsDisagree() &&
           turned_round.handState(0) == BodyCalibration::HAND_WAITING,
           "blades pointing opposite ways refuse the forward pose");
    holdBodyPose(turned_round, time, forward0, forward1);
    expect(turned_round.step() == BodyCalibration::STEP_SIDES &&
           !turned_round.directionsDisagree(),
           "agreeing blades then pass the forward pose");

    // Full fixes are measured; partial ones only as a late fallback.
    movebeat::BodyCalibration fixes;
    fixes.confirm(0);
    fixes.confirm(1);
    for (int frame = 0; frame < 60; ++frame, time += 17)
        fixes.sample(time, forward0, forward1, true, false);
    expect(fixes.handState(0) == BodyCalibration::HAND_DONE &&
           fixes.handState(1) == BodyCalibration::HAND_MEASURING,
           "a hand with partial fixes only keeps waiting for full ones");
    for (int frame = 0; frame < 70; ++frame, time += 17)
        fixes.sample(time, forward0, forward1, true, false);
    expect(fixes.step() == BodyCalibration::STEP_SIDES && fixes.partialMeasures() == 1,
           "after 1.5 s partial fixes are measured as a fallback");

    // A plausible session.
    movebeat::BodyCalibration good;
    holdBodyPose(good, time, forward0, forward1);
    holdBodyPose(good, time, bodyPose(0.65f, 1.35f, 2.0f, 1.1f, 1.35f, 2.0f),
                 bodyPose(-0.65f, 1.35f, 2.0f, -1.1f, 1.35f, 2.0f));
    expect(holdBodyPose(good, time, bodyPose(0.25f, 0.75f, 2.0f, 0.25f, 0.2f, 2.0f),
                        bodyPose(-0.25f, 0.75f, 2.0f, -0.25f, 0.2f, 2.0f)) &&
           good.done() && !good.invertedUp() && !good.narrowSides(),
           "a plausible session completes unflagged");
    expect(near(good.span(), 1.3f) && near(good.drop(), 0.55f),
           "span and drop are measured");
    expect(good.turnApplied() && near(good.turnDegrees(), 0.0f),
           "a player facing the camera needs no turn");

    // The frame: the blades pointed along the camera's -z, so play depth
    // flips (forward is +z); the hit plane is 45 cm along the blades from
    // the handles; a 1.3 m span scales hand positions by 1.6/1.3 about the
    // forward handles, blades unchanged.
    const movebeat::BodyFrame frame = good.frame();
    expect(good.flipDepth() && frame.flip_depth, "a player facing the camera flips depth");
    expect(near(frame.scale, 1.6f / 1.3f), "the scale makes the span the reference span");
    const movebeat::SaberPose rest = frame.apply(forward0);
    expect(near(rest.handle.x, 0.2f * 1.6f / 1.3f) && near(rest.handle.y, 0.0f) &&
           near(rest.handle.z, -0.45f) && near(rest.tip.z, 0.15f),
           "the forward pose's blades cross the hit plane 45 cm from the handles");
    const movebeat::SaberPose reach = frame.apply(
        bodyPose(0.7f, 1.3f, 2.0f, 0.7f, 1.3f, 1.4f));
    expect(near(reach.handle.x, 0.7f * 1.6f / 1.3f) &&
           near(length(reach.tip - reach.handle), 0.6f),
           "hand reach scales, blade length does not");
    expect(!frame.apply(movebeat::SaberPose()).tracked, "untracked poses stay untracked");

    // A player turned 20 degrees: the turn is measured and undone.
    movebeat::BodyCalibration turned;
    const float angle = 20.0f * 0.0174532925f;
    const movebeat::Vec3 blade(std::sin(angle) * 0.6f, 0.0f, -std::cos(angle) * 0.6f);
    const movebeat::Vec3 left_hand(-0.2f, 1.3f, 2.0f), right_hand(0.2f, 1.3f, 2.0f);
    holdBodyPose(turned, time, movebeat::SaberPose(left_hand, left_hand + blade, true),
                 movebeat::SaberPose(right_hand, right_hand + blade, true));
    expect(turned.step() == BodyCalibration::STEP_SIDES &&
           near(turned.turnDegrees(), 20.0f) && turned.turnApplied(),
           "the facing is measured from the forward blades");
    holdBodyPose(turned, time, bodyPose(-0.8f, 1.3f, 2.0f, -1.3f, 1.3f, 2.0f),
                 bodyPose(0.8f, 1.3f, 2.0f, 1.3f, 1.3f, 2.0f));
    holdBodyPose(turned, time, bodyPose(-0.25f, 0.7f, 2.0f, -0.25f, 0.1f, 2.0f),
                 bodyPose(0.25f, 0.7f, 2.0f, 0.25f, 0.1f, 2.0f));
    expect(turned.done() && !turned.slotsSwapped() && near(turned.frame().scale, 1.0f),
           "slot 0 on the left keeps the slots, a 1.6 m span keeps the scale");
    const movebeat::SaberPose aligned = turned.frame().apply(
        movebeat::SaberPose(right_hand, right_hand + blade, true));
    const movebeat::Vec3 direction = normalized(aligned.tip - aligned.handle);
    expect(near(direction.x, 0.0f) && near(direction.z, 1.0f),
           "after the turn the player's forward is the playfield's +z");

    // A wide turn (camera far off to the side) is reported, not applied.
    movebeat::BodyCalibration sideways;
    const movebeat::Vec3 side_blade(0.6f, 0.0f, -0.2f);
    holdBodyPose(sideways, time, movebeat::SaberPose(left_hand, left_hand + side_blade, true),
                 movebeat::SaberPose(right_hand, right_hand + side_blade, true));
    expect(sideways.step() == BodyCalibration::STEP_SIDES && !sideways.turnApplied(),
           "a turn beyond 30 degrees is not applied");

    // Recentering after a tracking loss keeps scale and turn.
    movebeat::BodyFrame moved = frame;
    moved.recenter(movebeat::Vec3(1.0f, 1.3f, 1.4f), movebeat::Vec3(1.0f, 1.3f, 2.0f));
    const movebeat::SaberPose recentered = moved.apply(
        bodyPose(1.2f, 1.3f, 2.0f, 1.2f, 1.3f, 1.4f));
    expect(near(recentered.tip.x, 0.2f * 1.6f / 1.3f) && near(recentered.tip.z, 0.15f) &&
           near(recentered.handle.z, -0.45f) && near(moved.scale, frame.scale),
           "recentering moves the origin and keeps the scale and depth");

    // Blades along +z (the emulator scripts' play-space poses): no flip,
    // and a forward pose with handles 45 cm behind the origin maps as is.
    movebeat::BodyCalibration plain;
    holdBodyPose(plain, time, bodyPose(0.2f, 0.0f, -0.45f, 0.2f, 0.0f, 0.15f),
                 bodyPose(-0.2f, 0.0f, -0.45f, -0.2f, 0.0f, 0.15f));
    holdBodyPose(plain, time, bodyPose(0.8f, 0.0f, 0.6f, 1.3f, 0.0f, 0.6f),
                 bodyPose(-0.8f, 0.0f, 0.6f, -1.3f, 0.0f, 0.6f));
    holdBodyPose(plain, time, bodyPose(0.25f, -0.6f, 0.6f, 0.25f, -1.2f, 0.6f),
                 bodyPose(-0.25f, -0.6f, 0.6f, -0.25f, -1.2f, 0.6f));
    const movebeat::SaberPose same = plain.frame().apply(
        bodyPose(0.3f, 0.2f, -0.4f, 0.5f, 0.3f, 0.5f));
    expect(plain.done() && !plain.flipDepth() && near(same.handle.x, 0.3f) &&
           near(same.handle.z, -0.4f) && near(same.tip.z, 0.5f),
           "play-space calibration poses give the identity frame");
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

void testPlaylistManifest() {
    std::istringstream input(
        "format=1\ntitle=Warmup\nauthor=Someone\nsong_count=3\n"
        "song0_hash=2DF6BE39F87AFF225E86A83E5185AA1DE8264E18\nsong0_key=132ce\n"
        "song0_title=beliver\nsong0_characteristic=Standard\nsong0_difficulty=Hard\n"
        "song1_key=bench02\n"
        "song2_hash=0000000000000000000000000000000000000000\n");
    movebeat::PlaylistManifest playlist;
    std::string error;
    expect(movebeat::loadPlaylistManifest(input, playlist, error), "playlist manifest parses");
    expect(playlist.title == "Warmup" && playlist.author == "Someone" &&
           playlist.songs.size() == 3, "playlist keeps title, author and song order");
    expect(playlist.songs[0].hash == "2df6be39f87aff225e86a83e5185aa1de8264e18" &&
           playlist.songs[0].difficulty == "Hard",
           "playlist hashes are lowercased and difficulties kept");

    movebeat::LevelManifest level;
    level.source_key = "132CE";
    level.source_hash = "2df6be39f87aff225e86a83e5185aa1de8264e18";
    expect(movebeat::playlistSongMatches(playlist.songs[0], level),
           "a playlist song matches its level by hash");
    level.source_hash = "1111111111111111111111111111111111111111";
    expect(!movebeat::playlistSongMatches(playlist.songs[0], level),
           "a different hash (another map version) does not match by key");
    movebeat::LevelManifest bench;
    bench.source_key = "Bench02";
    expect(movebeat::playlistSongMatches(playlist.songs[1], bench),
           "a song without a hash matches by key, ignoring case");
    expect(!movebeat::playlistSongMatches(playlist.songs[2], bench),
           "a hash-only song never matches a level without that hash");

    std::istringstream nameless("format=1\ntitle=X\nsong_count=1\nsong0_title=Y\n");
    expect(!movebeat::loadPlaylistManifest(nameless, playlist, error),
           "a playlist song needs a hash or a key");
    std::istringstream bad_hash("format=1\ntitle=X\nsong_count=1\nsong0_hash=xyz\n");
    expect(!movebeat::loadPlaylistManifest(bad_hash, playlist, error),
           "a playlist rejects malformed hashes");
    std::istringstream empty("format=1\ntitle=X\nsong_count=0\n");
    expect(!movebeat::loadPlaylistManifest(empty, playlist, error),
           "an empty playlist is rejected");

    std::istringstream hashed(
        "format=1\nsource_hash=2DF6BE39F87AFF225E86A83E5185AA1DE8264E18\ntitle=E\n"
        "bpm=120\naudio=a.ogg\nchart_count=1\nchart0_characteristic=Standard\n"
        "chart0_difficulty=Hard\nchart0_file=h.mbchart\nchart0_notes=1\nchart0_skipped=0\n");
    expect(movebeat::loadLevelManifest(hashed, level, error) &&
           level.source_hash == "2df6be39f87aff225e86a83e5185aa1de8264e18",
           "level manifests expose the lowercased content hash");
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

    movebeat::GameConfig no_fail;
    no_fail.energy_failure = false;
    movebeat::Game relaxed;
    relaxed.setConfig(no_fail);
    relaxed.load(chart);
    relaxed.update(2000, untracked, untracked);
    expect(relaxed.score().energy == 0 && !relaxed.score().failed,
           "with energy failure off, empty energy does not fail the level");
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
           !tracking_gate.observe(2299u, false) &&
           tracking_gate.observe(2300u, false) &&
           !tracking_gate.observe(2400u, false),
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
    // Rounded notes are shaded smoothly, so classify pixels by hue: the
    // red note dominates in red, the blue one in blue.
    const auto isRed = [](std::uint32_t color) {
        return color != 0xff000000u && ((color >> 16) & 255u) > (color & 255u);
    };
    const auto isBlue = [](std::uint32_t color) {
        return color != 0xff000000u && (color & 255u) > ((color >> 16) & 255u);
    };
    movebeat::ps3::rasterizeRoundedNote(target, render_center, 0.4f, 0.4f,
                                        0xffe83048u);
    movebeat::ps3::rasterizeRoundedNote(
        target, movebeat::Vec3(-1.5f, -0.55f, 1.0f), 0.4f, 0.4f, 0xff2098e8u);
    std::size_t front_pixels = 0, top_pixels = 0, side_pixels = 0;
    std::size_t shades = 0;
    std::vector<std::uint32_t> seen;
    for (int y = 0; y < render_height; ++y) {
        for (int x = 0; x < render_width; ++x) {
            const std::uint32_t color = pixels[y * render_width + x];
            if (!isRed(color)) continue;
            if (y < front_cube.screen[0].y) ++top_pixels;
            else if (x > front_cube.screen[1].x) ++side_pixels;
            else ++front_pixels;
            if (seen.size() < 64 &&
                std::find(seen.begin(), seen.end(), color) == seen.end())
                seen.push_back(color);
        }
    }
    shades = seen.size();
    expect(front_pixels > 0 && top_pixels > 0 && side_pixels > 0,
           "rounded note shows its front, top and camera-facing side");
    expect(top_pixels * 12 >= front_pixels && side_pixels * 12 >= front_pixels,
           "top and side of a rounded note occupy visible areas");
    expect(shades >= 8, "rounded note is shaded smoothly, not in flat planes");
    expect(isRed(pixels[center_pixel]),
           "near rounded note occludes a farther note regardless of draw order");
    expect(!movebeat::ps3::paintNoteDepthPixel(
               target, center_x, center_y, 1.0f - 0.2f, 0xffffffffu) &&
           isRed(pixels[center_pixel]),
           "far cut arrow cannot show through a nearer note");
    expect(movebeat::ps3::paintNoteDepthPixel(
               target, center_x, center_y, -0.2f, 0xffffffffu) &&
           pixels[center_pixel] == 0xffffffffu,
           "a cut arrow remains visible on its own front face");
    std::fill(pixels.begin(), pixels.end(), 0xff000000u);
    std::fill(depth.begin(), depth.end(), 0.0f);
    movebeat::ps3::rasterizeRoundedNote(
        target, movebeat::Vec3(-1.5f, -0.55f, 1.0f), 0.4f, 0.4f, 0xff2098e8u);
    movebeat::ps3::rasterizeRoundedNote(target, render_center, 0.4f, 0.4f,
                                        0xffe83048u);
    expect(isRed(pixels[center_pixel]) && !isBlue(pixels[center_pixel]),
           "near rounded note occludes a farther note in either draw order");
    (void)rear_cube;
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
    expect(movebeat::ps3::libraryCardDistance(0, 0, 30) == 0 &&
           movebeat::ps3::libraryCardDistance(3, 0, 30) == 3 &&
           movebeat::ps3::libraryCardDistance(27, 0, 30) == 3 &&
           movebeat::ps3::libraryCardDistance(15, 0, 30) == 15 &&
           movebeat::ps3::libraryCardDistance(1, 29, 30) == 2 &&
           movebeat::ps3::libraryCardDistance(5, 0, 0) == 0,
           "card window distance wraps around the library");
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
    checkpoint("body calibration"); testBodyCalibration();
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
    checkpoint("playlist manifest"); testPlaylistManifest();
    checkpoint("note jump settings"); testNoteJumpConfiguration();
    checkpoint("energy failure"); testEnergyFailure();
    checkpoint("high scores"); testHighScores();
    checkpoint("settings"); testSettings();
    checkpoint("input invalidation"); testInputInvalidation();
#if defined(__PPU__)
    checkpoint("PPU Move input mapping"); testPpuMoveInputMapping();
    checkpoint("PPU GemRuntime physical LED hue assignment");
    runGemRuntimeHueTests();
    checkpoint("PPU RSX draw batch geometry");
    failures += runDrawBatchTests();
    checkpoint("PPU library menu, Move gestures and playlists");
    failures += runMenuTests();
#endif
    if (failures == 0) {
        std::puts("All MoveBeat core tests passed.");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", failures);
    return EXIT_FAILURE;
}
