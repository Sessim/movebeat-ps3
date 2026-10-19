#include "move_input.hpp"
#include "demo_input.hpp"
#include "menu_buttons.hpp"
#include "hand_assignment.hpp"
#include "imu_pose.hpp"
#include "audio_cache.hpp"
#include "usb_import.hpp"
#include "tracking_loss_log.hpp"

#include <algorithm>
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
    expect(frame.right.tracked && frame.partial[1],
           "a position fix without a visible sphere plays, as a partial fix");
    expect(frame.right_buttons == 0xffff && frame.right_trigger == 255,
           "buttons remain available while optical tracking is lost");

    state.tracking = GEM_TRACKING_VISIBLE;
    movebeat::ps3::applyGemSample(1, state, 0, 0.9f, frame);
    expect(frame.right.tracked && frame.partial[1],
           "a visible sphere without a position fix plays, as a partial fix");
    state.tracking = 0;
    movebeat::ps3::applyGemSample(1, state, 0, 0.9f, frame);
    expect(!frame.right.tracked && !frame.partial[1],
           "neither flag is not tracked");
    state.tracking = GEM_TRACKING_POSITION_TRACKED;

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
    // Circle is "next track" (the name below is historical).
    const unsigned short cross = movebeat::ps3::GEM_BUTTON_CIRCLE;
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

// A quaternion turning `degrees` about the vertical axis, stored as libgem
// would in one of the two component orders.
movebeat::ps3::RawQuat yawQuat(float degrees, bool w_first) {
    const float half = degrees * 0.00872664626f;
    const float s = std::sin(half), c = std::cos(half);
    return w_first ? movebeat::ps3::RawQuat(c, 0.0f, s, 0.0f)
                   : movebeat::ps3::RawQuat(0.0f, s, 0.0f, c);
}

void testImuHold() {
    using movebeat::ps3::ImuSaberTracker;
    using movebeat::ps3::RawQuat;
    for (int order = 0; order < 2; ++order) {
        const bool w_first = order == 1;
        ImuSaberTracker tracker;
        const movebeat::Vec3 handle(0.1f, 1.2f, 1.5f);
        const movebeat::Vec3 forward(0.0f, 0.0f, -1.0f);
        unsigned int time = 1000;
        float degrees = 0.0f;
        // The camera sees the blade swing to and fro about the vertical.
        for (int frame = 0; frame < 120; ++frame, time += 17) {
            degrees = 40.0f * std::sin(frame * 0.15f);
            const RawQuat quat = yawQuat(degrees, w_first);
            const movebeat::Vec3 direction = ImuSaberTracker::turn(
                w_first ? 2 : 0, quat, yawQuat(0.0f, w_first), forward);
            const movebeat::SaberPose seen(handle, handle + direction * 1.2f, true);
            const movebeat::SaberPose out = tracker.update(time, seen, true, quat);
            expect(out.tracked && !tracker.held(), "a camera pose passes through");
        }
        expect(tracker.scoredSamples() > 5 && tracker.bestVariant() == (w_first ? 2 : 0) &&
               tracker.errorDegrees(tracker.bestVariant()) < 1.0f,
               "the quaternion reading that matches the camera is chosen");
        const int wrong = w_first ? 3 : 1;
        expect(tracker.errorDegrees(wrong) > 5.0f, "the opposite reading is rejected");

        // The camera loses the sphere: the hand stays, the blade turns on.
        const RawQuat last = yawQuat(degrees, w_first);
        const RawQuat turned = yawQuat(degrees + 30.0f, w_first);
        movebeat::SaberPose held = tracker.update(time, movebeat::SaberPose(), true, turned);
        expect(held.tracked && tracker.held(), "a lost sphere is held by the sensors");
        const movebeat::Vec3 before = ImuSaberTracker::turn(
            w_first ? 2 : 0, last, yawQuat(0.0f, w_first), forward);
        const movebeat::Vec3 after = normalized(held.tip - held.handle);
        const float angle = std::acos(std::max(-1.0f, std::min(1.0f, dot(before, after)))) *
                            57.2957795f;
        expect(near(held.handle.x, handle.x) && std::fabs(angle - 30.0f) < 1.0f &&
               near(length(held.tip - held.handle), 1.2f),
               "the held blade turns with the quaternion, at full length");
        held = tracker.update(time + ImuSaberTracker::holdMs() + 1u,
                              movebeat::SaberPose(), true, turned);
        expect(!held.tracked && !tracker.held(), "after the hold the hand counts as lost");
        expect(!tracker.update(time, movebeat::SaberPose(), false, turned).tracked,
               "without a quaternion nothing is held");
    }

    // A partial fix keeps the depth of the last full fix; a jump no hand
    // makes is held through, unless the new place persists.
    ImuSaberTracker tracker;
    const RawQuat still = yawQuat(0.0f, false);
    unsigned int time = 5000;
    const movebeat::Vec3 blade(0.0f, 0.0f, -1.0f);
    const movebeat::Vec3 at(0.0f, 1.2f, 2.0f);
    tracker.update(time, movebeat::SaberPose(at, at + blade, true), true, still);
    time += 17;
    const movebeat::Vec3 drifted(0.05f, 1.25f, 3.5f);
    movebeat::SaberPose out = tracker.update(
        time, movebeat::SaberPose(drifted, drifted + blade, true), true, still, true);
    expect(out.tracked && near(out.handle.x, 0.05f) && near(out.handle.y, 1.25f) &&
           near(out.handle.z, 2.0f) && near(out.tip.z, 1.0f),
           "a partial fix follows across and up but keeps the full fix's depth");
    const movebeat::Vec3 far(1.5f, 1.2f, 2.0f);
    for (unsigned int frame = 0; frame + 1u < ImuSaberTracker::jumpFrames(); ++frame) {
        time += 17;
        out = tracker.update(time, movebeat::SaberPose(far, far + blade, true), true, still);
        expect(out.tracked && tracker.held() && near(out.handle.x, 0.05f),
               "a sudden 1.5 m jump is held as a glitch");
    }
    time += 17;
    out = tracker.update(time, movebeat::SaberPose(far, far + blade, true), true, still);
    expect(out.tracked && !tracker.held() && near(out.handle.x, 1.5f),
           "a jump that persists is taken as the real place");

    // Depth jitter: a 30 cm depth jump moves the hand by at most 3 cm per
    // frame and settles on the new depth; across and up are not delayed.
    ImuSaberTracker depth;
    time = 9000;
    depth.update(time, movebeat::SaberPose(at, at + blade, true), true, still);
    const movebeat::Vec3 deeper(0.1f, 1.3f, 2.3f);
    time += 17;
    out = depth.update(time, movebeat::SaberPose(deeper, deeper + blade, true), true, still);
    expect(near(out.handle.z, 2.03f) && near(out.handle.x, 0.1f) && near(out.handle.y, 1.3f) &&
           near(out.tip.z - out.handle.z, -1.0f),
           "depth moves at most 3 cm per frame, blade intact, across and up at once");
    for (int frame = 0; frame < 60; ++frame) {
        time += 17;
        out = depth.update(time, movebeat::SaberPose(deeper, deeper + blade, true), true, still);
    }
    expect(std::fabs(out.handle.z - 2.3f) < 0.01f, "depth settles on the new value");

    // A depth reading far from the smoothed depth is not a jump: the hand
    // follows across and up every frame instead of freezing.
    ImuSaberTracker swing;
    time = 12000;
    swing.update(time, movebeat::SaberPose(at, at + blade, true), true, still);
    bool froze = false;
    for (int frame = 0; frame < 30; ++frame) {
        time += 17;
        const movebeat::Vec3 moved(0.01f * frame, 1.2f, frame % 2 ? 2.6f : 1.5f);
        out = swing.update(time, movebeat::SaberPose(moved, moved + blade, true), true, still);
        if (swing.held() || !near(out.handle.x, 0.01f * frame)) froze = true;
    }
    expect(!froze, "depth noise never holds the hand as a jump");
}

void testAudioCache() {
    using movebeat::ps3::AudioCache;
    expect(AudioCache::needsCache("/dev_usb000/MOVEBEAT/levels/x/audio.egg") &&
           !AudioCache::needsCache("/dev_hdd0/game/MOVEBEAT1/USRDIR/levels/x/audio.egg"),
           "only USB paths go through the cache");
    const std::string source = "movebeat_cache_source.tmp";
    const std::string target = "movebeat_cache_target.tmp";
    std::string payload;
    for (int index = 0; index < 300000; ++index)
        payload.push_back(static_cast<char>((index * 7 + 3) & 0xff));
    FILE* file = fopen(source.c_str(), "wb");
    fwrite(payload.data(), 1, payload.size(), file);
    fclose(file);

    AudioCache cache;
    expect(cache.playable(source) == source, "an uncached file plays from its source");
    cache.begin(source, target);
    int steps = 0;
    while (!cache.step(64 * 1024) && steps < 100) ++steps;
    expect(cache.ready() && steps >= 4 && cache.copiedBytes() == payload.size(),
           "the copy goes a piece per step");
    std::string copied;
    file = fopen(target.c_str(), "rb");
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) copied.append(buffer, count);
    fclose(file);
    expect(copied == payload && cache.playable(source) == target,
           "the disk copy is exact and plays instead of the source");
    cache.begin(source, target);
    expect(cache.ready() && !cache.busy(), "the same source is not copied again");
    expect(cache.playable("other.ogg") == "other.ogg", "another file plays from its source");

    AudioCache missing;
    missing.begin("movebeat_cache_missing.tmp", target);
    expect(missing.failed() && !missing.finish() &&
           missing.playable("movebeat_cache_missing.tmp") == "movebeat_cache_missing.tmp",
           "a failed copy falls back to the source");
    remove(source.c_str());
    remove(target.c_str());
}

namespace {

void writeTestFile(const std::string& path, const std::string& data) {
    FILE* file = fopen(path.c_str(), "wb");
    fwrite(data.data(), 1, data.size(), file);
    fclose(file);
}

std::string readTestFile(const std::string& path) {
    std::string data;
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return data;
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) data.append(buffer, count);
    fclose(file);
    return data;
}

} // namespace

void testUsbImport() {
    using movebeat::ps3::UsbImport;
    using movebeat::ps3::UsbImportJob;
    const std::string source = "movebeat_usb_source.tmp";
    const std::string target = "movebeat_usb_target.tmp";
    movebeat::ps3::makeDirectory(source);
    remove((target + "/audio.egg").c_str());
    remove((target + "/level.ini").c_str());
    std::string audio;
    for (int index = 0; index < 200000; ++index)
        audio.push_back(static_cast<char>((index * 13 + 5) & 0xff));
    writeTestFile(source + "/audio.egg", audio);
    writeTestFile(source + "/level.ini", "format=1\n");

    UsbImportJob job;
    job.source = source;
    job.target = target;
    job.files.push_back("audio.egg");
    job.files.push_back("level.ini");
    UsbImport import;
    import.add(job);
    import.add(job);
    expect(import.busy() && import.total() == 1, "a bundle is queued once");
    std::string from, to;
    int steps = 0;
    bool completed = false;
    while (import.busy() && steps < 100) {
        if (import.step(64 * 1024, from, to)) completed = true;
        ++steps;
        if (steps == 2)
            expect(readTestFile(target + "/level.ini").empty(),
                   "the manifest is not there while the bundle is copied");
    }
    expect(completed && from == source && to == target && steps >= 4 &&
           import.done() == 1 && import.failed() == 0,
           "the bundle is copied a piece per step and reported once");
    expect(readTestFile(target + "/audio.egg") == audio &&
           readTestFile(target + "/level.ini") == "format=1\n" &&
           readTestFile(target + "/audio.egg.part").empty(),
           "the copy is exact, without leftover parts");
    import.clearFinished();
    expect(import.total() == 0 && import.done() == 0, "finished jobs are forgotten when idle");

    UsbImportJob missing = job;
    missing.source = "movebeat_usb_missing.tmp";
    import.add(missing);
    while (import.busy()) import.step(64 * 1024, from, to);
    expect(import.failed() == 1 && import.done() == 0, "a missing bundle fails without a result");

    remove((source + "/audio.egg").c_str());
    remove((source + "/level.ini").c_str());
    remove((target + "/audio.egg").c_str());
    remove((target + "/level.ini").c_str());
}

void testTrackingLossLog() {
    using movebeat::ps3::TrackingLossLog;
    TrackingLossLog log;
    // Slot 0 seen near the right edge, then lost for 400 ms.
    log.update(0, true, 1000, 5000, true, 0.95f, 0.5f, 12.0f, 0, 3, 0);
    log.update(0, false, 1017, 5017, false, 0, 0, 0, 0, 0, 0x80ull);
    log.update(0, false, 1200, 5200, false, 0, 0, 0, 0, 0, 0x2000ull);
    log.update(0, true, 1417, 5417, true, 0.9f, 0.5f, 12.0f, 0, 3, 0);
    // Slot 1 flickers for 50 ms in the middle of the picture.
    log.update(1, true, 1000, 5000, true, 0.4f, 0.5f, 10.0f, 0, 3, 0);
    log.update(1, false, 1050, 5050, false, 0, 0, 0, 0, 2, 0);
    log.update(1, true, 1100, 5100, true, 0.4f, 0.5f, 10.0f, 0, 3, 0);
    // Slot 1 lost again until the song ends.
    log.update(1, false, 2000, 6000, false, 0, 0, 0, 7, 0, 0);
    log.close(2600);
    expect(log.count(0) == 1 && log.count(1) == 2, "losses are counted per slot");
    const std::vector<movebeat::ps3::TrackingLoss>& events = log.events();
    expect(events.size() == 3 && events[0].duration_ms == 400 && events[0].song_ms == 5017,
           "a loss keeps its start in the song and its length");
    expect(events[0].near_edge && events[0].last_x > 0.94f && log.edgeCount() == 1,
           "a sphere last seen at the frame's edge counts as leaving the view");
    expect(events[0].status_flags == (0x80ull | 0x2000ull),
           "libgem flags raised during a loss are kept");
    expect(log.shortCount() == 1 && log.longestMs() == 600 && log.totalLostMs() == 1050,
           "flicker, the longest loss and the total are summed");
    expect(events[2].result == 7, "the libgem result at the loss is kept");
    const char* path = "movebeat_tracking_log.tmp";
    expect(log.writeCsv(path), "the log is written as CSV");
    FILE* file = fopen(path, "r");
    int lines = 0;
    for (int c = file ? fgetc(file) : EOF; c != EOF; c = fgetc(file)) if (c == 10) ++lines;
    if (file) fclose(file);
    remove(path);
    expect(lines == 4, "a header and one line per loss");
    log.reset();
    expect(log.count(0) == 0 && log.events().empty(), "a new song starts a new log");
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
        // Both held half a second: one long chord (the randomizer).
        MoveButtonChord chord;
        expect(!chord.update(true, true, 1000u).long_chord &&
               !chord.update(true, true, 1400u).long_chord,
               "a short chord is not a long chord");
        expect(chord.update(true, true, 1500u).long_chord &&
               !chord.update(true, true, 1700u).long_chord,
               "both Move held 500 ms fire one long chord");
        MoveChordEvents events = chord.update(false, false, 1800u);
        expect(events.tap_slot < 0, "releasing a long chord is no tap");
        expect(!chord.update(true, true, 2000u).long_chord &&
               chord.update(true, true, 2600u).long_chord,
               "a new hold fires again");
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
    testImuHold();
    testAudioCache();
    testUsbImport();
    testTrackingLossLog();
    testMenuNavigationRepeat();
    testPhysicalHandAssignment();
    testRuntimeRecoveryClassification();
    testScriptedDemoCuts();
    if (failures == 0) std::puts("All Move input tests passed.");
    return failures == 0 ? 0 : 1;
}
