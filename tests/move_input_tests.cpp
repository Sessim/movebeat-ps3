#include "move_input.hpp"
#include "demo_input.hpp"
#include "menu_buttons.hpp"
#include "hand_assignment.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

bool near(float left, float right) {
    return std::fabs(left - right) < 0.0001f;
}

void expect(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

void setVector(vec_float4& value, float x, float y, float z) {
    value.value[0] = x;
    value.value[1] = y;
    value.value[2] = z;
    value.value[3] = 1.0f;
}

void testCoordinateConversion() {
    gemState state;
    std::memset(&state, 0, sizeof(state));
    setVector(state.handle_pos, 100.0f, 200.0f, 300.0f);
    setVector(state.pos, 100.0f, 1200.0f, 300.0f);
    state.tracking = GEM_TRACKING_POSITION_TRACKED | GEM_TRACKING_VISIBLE;
    state.paddata.buttons = 0x48;
    state.paddata.ANA_T = 173;

    movebeat::ps3::MoveFrame frame;
    movebeat::ps3::applyGemSample(0, state, 0, 0.8f, frame);
    expect(frame.left.tracked, "visible, position-tracked sample is usable");
    expect(near(frame.left.handle.x, 0.1f) &&
           near(frame.left.handle.y, 0.2f) &&
           near(frame.left.handle.z, 0.3f),
           "libgem millimetres convert to metres without flipping axes");
    expect(near(frame.left.tip.x, 0.1f) &&
           near(frame.left.tip.y, 2.0f) &&
           near(frame.left.tip.z, 0.3f),
           "blade extends from handle through the sphere");
    expect(frame.left_buttons == 0x48 && frame.left_trigger == 173,
           "buttons and analog trigger come from the same sample");
}

void testRejectedAndUntrackedSamples() {
    gemState state;
    std::memset(&state, 0, sizeof(state));
    setVector(state.handle_pos, -200.0f, 500.0f, 900.0f);
    setVector(state.pos, -200.0f, 700.0f, 900.0f);
    state.tracking = GEM_TRACKING_POSITION_TRACKED;
    state.paddata.buttons = 0xffff;
    state.paddata.ANA_T = 255;

    movebeat::ps3::MoveFrame frame;
    movebeat::ps3::applyGemSample(1, state, 0, 0.9f, frame);
    expect(!frame.right.tracked, "position without a visible sphere is not tracked");
    expect(frame.right_buttons == 0xffff && frame.right_trigger == 255,
           "buttons remain available while optical tracking is lost");

    movebeat::ps3::applyGemSample(1, state, 5, 0.9f, frame);
    expect(!frame.right.tracked && frame.right_buttons == 0 &&
           frame.right_trigger == 0,
           "nonzero gemGetState result clears the sample");

    state.tracking = GEM_TRACKING_POSITION_TRACKED | GEM_TRACKING_VISIBLE;
    setVector(state.pos, std::nanf(""), 700.0f, 900.0f);
    movebeat::ps3::applyGemSample(1, state, 0, 0.9f, frame);
    expect(!frame.right.tracked &&
           std::isfinite(frame.right.tip.x) && std::isfinite(frame.right.tip.y),
           "non-finite GEM coordinates cannot become a tracked blade");
    setVector(state.pos, -200.0f, 500.0f, 900.0f);
    movebeat::ps3::applyGemSample(1, state, 0, 0.9f, frame);
    expect(!frame.right.tracked,
           "coincident handle and sphere cannot define a blade direction");
}

void testInertialButtonsWithoutCameraPose() {
    gemInertialState state;
    std::memset(&state, 0, sizeof(state));
    state.pad.buttons = movebeat::ps3::GEM_BUTTON_CROSS |
                        movebeat::ps3::GEM_BUTTON_START;
    state.pad.ANA_T = 183;
    movebeat::ps3::MoveFrame frame;
    movebeat::ps3::applyInertialSample(0, state, 0, frame);
    expect(frame.left_buttons == state.pad.buttons && frame.left_trigger == 183,
           "inertial sample retains menu buttons and trigger without a camera frame");
    expect(!frame.left.tracked && !frame.right.tracked,
           "inertial buttons never synthesize a tracked blade");
    movebeat::ps3::applyInertialSample(1, state, 4, frame);
    expect(frame.right_buttons == 0 && frame.right_trigger == 0,
           "failed inertial sample does not reuse stale buttons");
}

void testMenuButtonEdges() {
    movebeat::ps3::MenuButtonEdges edges;
    movebeat::ps3::MoveFrame frame;
    frame.left_connected = true;
    frame.left_buttons = movebeat::ps3::GEM_BUTTON_CROSS;
    movebeat::ps3::MenuButtonSample sample = edges.update(frame, 1u);
    expect(sample.pressed == 0,
           "a button already held on connection cannot navigate the menu");
    frame.left_buttons = 0;
    edges.update(frame, 1u);
    frame.left_buttons = movebeat::ps3::GEM_BUTTON_CROSS;
    frame.left_trigger = 200;
    sample = edges.update(frame, 1u);
    expect(sample.pressed == movebeat::ps3::GEM_BUTTON_CROSS &&
           sample.left_trigger == 200,
           "a fresh press and modifier are accepted from the first Move");

    frame.right_connected = true;
    frame.right_buttons = 0;
    edges.update(frame, 3u);
    frame.right_buttons = movebeat::ps3::GEM_BUTTON_CROSS;
    sample = edges.update(frame, 3u);
    expect((sample.pressed & movebeat::ps3::GEM_BUTTON_CROSS) != 0,
           "the second Move can press a button held by the first Move");

    frame.left_buttons = 0;
    frame.left_trigger = 0;
    frame.right_buttons = 0;
    sample = edges.update(frame, 0u);
    expect(sample.pressed == 0 &&
           (sample.held & movebeat::ps3::GEM_BUTTON_CROSS) != 0 &&
           sample.left_trigger == 200,
           "one missing sample does not synthesize a release or lose a trigger chord");
    frame.left_buttons = frame.right_buttons = movebeat::ps3::GEM_BUTTON_CROSS;
    sample = edges.update(frame, 3u);
    expect(sample.pressed == 0,
           "reacquiring buttons still held across a camera gap is not a new press");
    expect((sample.left_held & movebeat::ps3::GEM_BUTTON_CROSS) != 0 &&
           (sample.right_held & movebeat::ps3::GEM_BUTTON_CROSS) != 0,
           "held buttons are reported per Move");
    for (int i = 0; i < 4; ++i) sample = edges.update(frame, 0u);
    expect(sample.held == 0 && sample.left_trigger == 0,
           "a prolonged button-sample outage releases stale modifiers safely");
    sample = edges.update(frame, 3u);
    expect(sample.pressed == 0,
           "a held button cannot fire when samples return after an outage");

    frame.left_connected = false;
    edges.update(frame, 2u);
    frame.left_connected = true;
    sample = edges.update(frame, 3u);
    expect(sample.pressed == 0,
           "reconnecting with a held button does not navigate unexpectedly");
}

void testMenuNavigationRepeat() {
    movebeat::ps3::MenuNavigationRepeat repeat;
    const unsigned short cross = movebeat::ps3::GEM_BUTTON_CROSS;
    const unsigned short triangle = movebeat::ps3::GEM_BUTTON_TRIANGLE;
    expect(repeat.update(cross, 0, 1000) == 0 &&
           repeat.update(cross, 0, 2000) == 0,
           "a button held during connection never starts navigation repeat");
    repeat.update(0, 0, 2010);
    expect(repeat.update(cross, cross, 2020) == 0 &&
           repeat.update(cross, 0, 2469) == 0 &&
           repeat.update(cross, 0, 2470) == cross,
           "navigation repeats only after a 450 ms initial hold");
    expect(repeat.update(cross, 0, 2600) == 0 &&
           repeat.update(cross, 0, 2620) == cross,
           "held navigation repeats at a controlled 150 ms interval");
    expect(repeat.update(cross, 0, 5000) == cross &&
           repeat.update(cross, 0, 5001) == 0,
           "a long frame gap does not release a burst of queued repeats");
    expect(repeat.update(cross | triangle, 0, 5100) == 0 &&
           repeat.update(cross | triangle, 0, 6000) == 0,
           "opposing navigation buttons cannot auto-repeat together");
    expect(repeat.update(triangle, triangle, 6010) == 0 &&
           repeat.update(triangle, 0, 6460) == triangle,
           "releasing the opposite button allows a new deliberate hold");
    repeat.reset();
    expect(repeat.update(triangle, 0, 7000) == 0,
           "leaving the selection screen cancels a pending repeat");
}

void testPhysicalHandAssignment() {
    movebeat::ps3::MoveFrame frame;
    frame.left = movebeat::SaberPose(
        movebeat::Vec3(0.30f, 0.8f, 1.0f),
        movebeat::Vec3(0.30f, 1.3f, 1.0f), true);
    frame.right = movebeat::SaberPose(
        movebeat::Vec3(-0.30f, 0.8f, 1.0f),
        movebeat::Vec3(-0.30f, 1.3f, 1.0f), true);
    frame.left_connected = frame.right_connected = true;
    frame.left_buttons = movebeat::ps3::GEM_BUTTON_CROSS;
    frame.right_buttons = movebeat::ps3::GEM_BUTTON_CIRCLE;
    frame.left_trigger = 40;
    frame.right_trigger = 180;
    expect(movebeat::ps3::handsSeparatedForCalibration(frame.left, frame.right) &&
           movebeat::ps3::shouldSwapHandSlots(frame.left, frame.right),
           "starting pose identifies physically reversed GEM slots");
    movebeat::ps3::swapMoveFrameHands(frame);
    expect(frame.left.handle.x < frame.right.handle.x &&
           frame.left_buttons == movebeat::ps3::GEM_BUTTON_CIRCLE &&
           frame.right_buttons == movebeat::ps3::GEM_BUTTON_CROSS &&
           frame.left_trigger == 180 && frame.right_trigger == 40 &&
           movebeat::ps3::physicalSlotForHand(0u, true) == 1u,
           "logical hands, buttons, triggers and rumble slots swap together");
    frame.right.handle.x = frame.left.handle.x + 0.05f;
    expect(!movebeat::ps3::handsSeparatedForCalibration(frame.left, frame.right),
           "near-overlapping Move handles cannot establish handedness");
    frame.right.tracked = false;
    expect(!movebeat::ps3::handsSeparatedForCalibration(frame.left, frame.right),
           "tracking loss cannot establish handedness");
}

void testRuntimeRecoveryClassification() {
    expect(movebeat::ps3::gemHueForPhysicalSlot(0, false) ==
               movebeat::ps3::GEM_HUE_LEFT_HAND &&
           movebeat::ps3::gemHueForPhysicalSlot(1, false) ==
               movebeat::ps3::GEM_HUE_RIGHT_HAND,
           "default physical slots receive their logical hand hues");
    expect(movebeat::ps3::gemHueForPhysicalSlot(0, true) ==
               movebeat::ps3::GEM_HUE_RIGHT_HAND &&
           movebeat::ps3::gemHueForPhysicalSlot(1, true) ==
               movebeat::ps3::GEM_HUE_LEFT_HAND,
           "swapped physical slots keep the left-blue/right-red hue mapping");
    expect(movebeat::ps3::GEM_HUE_DONT_TRACK == (2u << 24) &&
           movebeat::ps3::GEM_HUE_DONT_CARE == (4u << 24) &&
           movebeat::ps3::GEM_HUE_DONT_CHANGE == (8u << 24),
           "cellGemTrackHues special modes retain their ABI encodings");
    expect(movebeat::ps3::gemResultNeedsCalibration(
               movebeat::ps3::GEM_STATUS_SPHERE_NOT_CALIBRATED),
           "uncalibrated sphere requests gem calibration");
    expect(movebeat::ps3::gemResultNeedsCalibration(
               movebeat::ps3::GEM_STATUS_NOT_CALIBRATED),
           "uncalibrated controller requests gem calibration");
    expect(!movebeat::ps3::gemResultNeedsCalibration(
               movebeat::ps3::GEM_STATUS_SPHERE_CALIBRATING),
           "active calibration is not restarted every frame");
    expect(movebeat::ps3::gemResultNeedsHues(
               movebeat::ps3::GEM_STATUS_HUE_NOT_SET),
           "missing hue requests automatic hue tracking");
    expect(!movebeat::ps3::gemResultNeedsHues(movebeat::ps3::GEM_STATUS_OK),
           "usable sample does not retrigger hue selection");
    expect(movebeat::ps3::gemSlotReady(
               movebeat::ps3::GEM_CONNECTION_READY),
           "ready gemInfo slot is independently connected");
    expect(!movebeat::ps3::gemSlotReady(
               movebeat::ps3::GEM_CONNECTION_DISCONNECTED),
           "disconnected gemInfo slot stays independent of total count");
}

void testScriptedDemoCuts() {
    movebeat::Chart chart;
    const movebeat::CutDirection directions[] = {
        movebeat::CUT_ANY, movebeat::CUT_UP, movebeat::CUT_DOWN,
        movebeat::CUT_LEFT, movebeat::CUT_RIGHT
    };
    for (unsigned int index = 0; index < 5; ++index) {
        movebeat::Note note;
        note.time_ms = 1000u + index * 500u;
        note.lane = index % 2u == 0u ? 1 : 2;
        note.row = 1;
        note.hand = index % 2u == 0u
            ? movebeat::HAND_LEFT : movebeat::HAND_RIGHT;
        note.direction = directions[index];
        chart.notes.push_back(note);
    }
    movebeat::Game game;
    game.load(chart);
    for (unsigned int time_ms = 0; time_ms <= 3300; time_ms += 16) {
        const movebeat::ps3::MoveFrame frame =
            movebeat::ps3::scriptedDemoFrame(game, time_ms);
        game.update(time_ms, frame.left, frame.right);
    }
    expect(game.score().hits == 5 && game.score().misses == 0,
           "emulator script exercises cuts with both hands and directions");
    expect(game.score().score > 0,
           "scripted cuts flow through the normal scoring path");
}

void testMoveButtonChord() {
    using movebeat::ps3::MoveButtonChord;
    using movebeat::ps3::MoveChordEvents;
    {
        // Same frame: a chord, and neither release is a tap.
        MoveButtonChord chord;
        expect(chord.update(true, true, 1000u).chord, "simultaneous Move presses are a chord");
        MoveChordEvents events = chord.update(false, true, 1100u);
        expect(!events.chord && events.tap_slot < 0, "a chord release is not a tap");
        events = chord.update(false, false, 1150u);
        expect(events.tap_slot < 0, "the second chord release is not a tap");
    }
    {
        // Presses 250 ms apart still form a chord; no tap fires first.
        MoveButtonChord chord;
        MoveChordEvents events = chord.update(true, false, 1000u);
        expect(!events.chord && events.tap_slot < 0, "a lone press waits for its release");
        events = chord.update(true, true, 1250u);
        expect(events.chord, "presses within the window form a chord");
        expect(!chord.update(true, true, 1300u).chord, "a held chord fires once");
    }
    {
        // A lone tap is reported on release, for its slot.
        MoveButtonChord chord;
        chord.update(false, true, 1000u);
        MoveChordEvents events = chord.update(false, false, 1120u);
        expect(!events.chord && events.tap_slot == 1, "a lone short press is a tap of its slot");
    }
    {
        // Too far apart: neither a chord nor taps.
        MoveButtonChord chord;
        chord.update(true, false, 1000u);
        MoveChordEvents events = chord.update(true, true, 1500u);
        expect(!events.chord, "presses 500 ms apart are not a chord");
        events = chord.update(false, true, 1600u);
        expect(events.tap_slot < 0, "an overlapped press is not a tap");
        events = chord.update(false, false, 1650u);
        expect(events.tap_slot < 0, "the late overlapped press is not a tap either");
    }
    {
        // A long hold is not a tap; a tap after a chord works again.
        MoveButtonChord chord;
        chord.update(true, false, 1000u);
        expect(chord.update(false, false, 2000u).tap_slot < 0, "a long hold is not a tap");
        chord.update(true, false, 3000u);
        expect(chord.update(false, false, 3100u).tap_slot == 0, "a later tap still counts");
    }
}

} // namespace

int main() {
    testCoordinateConversion();
    testRejectedAndUntrackedSamples();
    testInertialButtonsWithoutCameraPose();
    testMenuButtonEdges();
    testMoveButtonChord();
    testMenuNavigationRepeat();
    testPhysicalHandAssignment();
    testRuntimeRecoveryClassification();
    testScriptedDemoCuts();
    if (failures == 0) std::puts("All Move input tests passed.");
    return failures == 0 ? 0 : 1;
}
