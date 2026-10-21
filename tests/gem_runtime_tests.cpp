#include "gem_runtime.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <io/camera.h>
#include <spurs/spurs.h>
#include <sys/memory.h>
#include <sys/spu.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>

using namespace movebeat::ps3;

namespace {

u32 g_last_requested_hues[4] = {};
unsigned int g_hue_call_count = 0;
int g_next_hue_error = 0;
unsigned int g_state_result = GEM_STATUS_HUE_NOT_SET;
unsigned int g_ready_mask = 3;
unsigned int g_camera_read_count = 1;
unsigned int g_calibrate_count = 0;
unsigned short g_state_buttons = 0;
bool g_state_tracked = false;
// libgem moving a requested hue: `from` comes back as `to`; a slot-0 hue
// forced whatever is asked (kNone: off).
const u32 kNone = 0xffffffffu;
u32 g_hue_from = kNone;
u32 g_hue_to = kNone;
u32 g_slot0_forced = kNone;

void fail(const char* message) {
    std::fprintf(stderr, "GemRuntime test failed: %s\n", message);
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) fail(message);
}

void resetFakes() {
    std::memset(g_last_requested_hues, 0, sizeof(g_last_requested_hues));
    g_hue_call_count = 0;
    g_next_hue_error = 0;
    g_state_result = GEM_STATUS_HUE_NOT_SET;
    g_ready_mask = 3;
    g_camera_read_count = 1;
    g_calibrate_count = 0;
    g_state_buttons = 0;
    g_state_tracked = false;
    g_hue_from = g_hue_to = g_slot0_forced = kNone;
}

} // namespace

#if defined(_MSC_VER)
extern "C" void* memalign(std::size_t, std::size_t size) {
    return std::malloc(size);
}
#endif

void setVector(vec_float4& vector, float x, float y, float z) {
    union VectorAccess {
        vec_float4 vector;
        float values[4];
    } access;
    access.values[0] = x;
    access.values[1] = y;
    access.values[2] = z;
    access.values[3] = 1.0f;
    vector = access.vector;
}

s32 gemGetState(u32, u32, system_time_t, gemState* state) {
    if (g_state_result == GEM_STATUS_OK) {
        std::memset(state, 0, sizeof(*state));
        state->paddata.buttons = g_state_buttons;
        if (g_state_tracked) {
            state->tracking = GEM_TRACKING_POSITION_TRACKED | GEM_TRACKING_VISIBLE;
            setVector(state->handle_pos, 0.0f, -100.0f, 0.0f);
            setVector(state->pos, 0.0f, 0.0f, 0.0f);
        }
    }
    return g_state_result;
}
s32 gemGetImageState(u32, gemImageState*) { return -1; }
s32 gemGetInertialState(u32, u32, system_time_t, gemInertialState*) { return -1; }
s32 gemGetInfo(gemInfo* info) {
    std::memset(info, 0, sizeof(*info));
    info->connected = ((g_ready_mask & 1u) ? 1u : 0u) +
                      ((g_ready_mask & 2u) ? 1u : 0u);
    if ((g_ready_mask & 1u) != 0) info->status[0] = GEM_CONNECTION_READY;
    if ((g_ready_mask & 2u) != 0) info->status[1] = GEM_CONNECTION_READY;
    return 0;
}
s32 gemGetMemorySize(s32) { return 4096; }
s32 gemInit(const gemAttribute*) { return 0; }
s32 gemEnd() { return 0; }
s32 gemPrepareCamera(s32, f32) { return 0; }
s32 gemReset(u32) { return 0; }
s32 gemUpdateStart(const void*, system_time_t) { return 0; }
s32 gemUpdateFinish() { return 0; }
s32 gemTrackHues(const u32* requested, u32* assigned) {
    ++g_hue_call_count;
    std::memcpy(g_last_requested_hues, requested, sizeof(g_last_requested_hues));
    if (g_next_hue_error != 0) {
        const int error = g_next_hue_error;
        g_next_hue_error = 0;
        return error;
    }
    std::memcpy(assigned, requested, sizeof(g_last_requested_hues));
    for (int slot = 0; slot < 2; ++slot)
        if (assigned[slot] == g_hue_from) assigned[slot] = g_hue_to;
    if (g_slot0_forced != kNone) assigned[0] = g_slot0_forced;
    return 0;
}
s32 gemCalibrate(u32) { ++g_calibrate_count; return 0; }
s32 gemFilterState(u32, u32) { return 0; }
s32 gemEnableCameraPitchAngleCorrection(s32) { return 0; }
s32 gemGetStatusFlags(u32, u64* flags) { *flags = 0; return 0; }
s32 gemClearStatusFlags(u32, u64) { return 0; }
s32 gemGetCameraState(gemCameraState* state) {
    std::memset(state, 0, sizeof(*state));
    return 0;
}
s32 gemSetRumble(u32, u8) { return 0; }

s32 cameraInit() { return 0; }
s32 cameraEnd() { return 0; }
s32 cameraGetType(s32, cameraType* type) {
    *type = CAM_TYPE_PLAYSTATION_EYE;
    return 0;
}
s32 cameraOpenEx(s32, cameraInfoEx* info) {
    info->buffer = 0x1000;
    info->width = 640;
    info->height = 480;
    return 0;
}
s32 cameraClose(s32) { return 0; }
s32 cameraStart(s32) { return 0; }
s32 cameraStop(s32) { return 0; }
s32 cameraReset(s32) { return 0; }
s32 cameraReadEx(s32, cameraReadInfo* read) {
    read->readcount = g_camera_read_count;
    read->buffer = 0x1000;
    read->timestamp = 1;
    return 0;
}

s32 spursAttributeInitialize(SpursAttribute*, u8, s32, s32, bool) { return 0; }
#if defined(__PPU__)
s32 spursAttributeSetNamePrefix(SpursAttribute*, const char*, std::size_t) { return 0; }
#else
s32 spursAttributeSetNamePrefix(SpursAttribute*, const char*, u32) { return 0; }
#endif
s32 spursInitializeWithAttribute(Spurs*, const SpursAttribute*) { return 0; }
s32 spursFinalize(Spurs*) { return 0; }
#if !defined(__PPU__)
s32 sysSpuInitialize(u32, u32) { return 0; }
s32 sysThreadGetPriority(sys_ppu_thread_t, s32* priority) { *priority = 1000; return 0; }
s32 sysMemContainerCreate(sys_mem_container_t* container, std::size_t) {
    *container = 1;
    return 0;
}
s32 sysMemContainerDestroy(sys_mem_container_t) { return 0; }
s32 sysThreadGetId(sys_ppu_thread_t* id) { *id = 1; return 0; }
#endif
#if defined(__PPU__)
s32 sysModuleLoad(sysModuleId) { return SYSMODULE_OK; }
s32 sysModuleUnload(sysModuleId) { return 0; }
#else
s32 sysModuleLoad(s32) { return SYSMODULE_OK; }
s32 sysModuleUnload(s32) { return 0; }
#endif

void runGemRuntimeHueTests() {
    resetFakes();
    {
        // Default: the hands never repaint the spheres; libgem keeps
        // choosing hues, and an unanswered request is repeated.
        GemRuntime automatic;
        require(automatic.initialize(), "fake libgem should initialize");
        MoveFrame frame;
        require(automatic.update(frame, 0.6f) && g_hue_call_count == 1,
                "the first frame asks libgem for automatic hues");
        automatic.setHandSlotsSwapped(true);
        require(!automatic.hueAssignmentPending(),
                "assigning hands does not repaint the spheres by default");
        for (int i = 0; i < 100; ++i) automatic.update(frame, 0.6f);
        require(g_hue_call_count == 1, "a pending hue request is not spammed");
        for (int i = 0; i < 30; ++i) automatic.update(frame, 0.6f);
        require(g_hue_call_count == 2 &&
                g_last_requested_hues[0] == GEM_HUE_DONT_CARE &&
                g_last_requested_hues[1] == GEM_HUE_DONT_CARE,
                "a hue still not set after two seconds is requested again, automatically");
        require(automatic.lastResult(0) == GEM_STATUS_HUE_NOT_SET &&
                automatic.lastTrackingFlags(0) == 0u,
                "the latest libgem result per slot is reported");
        automatic.shutdown();
    }
    resetFakes();
    GemRuntime runtime;
    runtime.setHandHuesEnabled(true);
    require(runtime.initialize(), "fake PS Eye/libgem initialization should succeed");
    MoveFrame frame;

    require(runtime.update(frame, 0.6f), "initial fake Move frame should update");
    require(g_hue_call_count == 1, "unassigned controllers should get one automatic hue request");
    require(g_last_requested_hues[0] == GEM_HUE_DONT_CARE &&
            g_last_requested_hues[1] == GEM_HUE_DONT_CARE,
            "initial request should leave both physical controller hues automatic");
    require(runtime.hueValuesKnown(), "successful automatic request should record returned hues");

    runtime.setHandSlotsSwapped(true);
    require(runtime.hueAssignmentPending(), "hand calibration should mark the physical LED mapping pending");
    g_next_hue_error = -77;
    require(runtime.update(frame, 0.6f), "a hue API error should not abort Move frame processing");
    require(g_hue_call_count == 2, "calibrated mapping should be submitted immediately");
    require(g_last_requested_hues[0] == GEM_HUE_RIGHT_HAND &&
            g_last_requested_hues[1] == GEM_HUE_LEFT_HAND,
            "swapped physical slots should receive right-red then left-blue hues");
    require(runtime.hueAssignmentPending(), "failed LED update must remain pending for retry");
    require(runtime.lastHueError() == -77, "libgem hue error should remain observable");

    require(runtime.update(frame, 0.6f), "pending hue update should retry on next frame");
    require(g_hue_call_count == 3, "pending hue update should be retried");
    require(!runtime.hueAssignmentPending(), "successful retry should clear pending state");
    require(runtime.handHueAssignmentApplied(), "successful retry should confirm calibrated mapping");
    require(runtime.hueValuesKnown(), "successful retry should expose libgem's returned hues");
    require(runtime.hueForPhysicalSlot(0) == GEM_HUE_RIGHT_HAND &&
            runtime.hueForPhysicalSlot(1) == GEM_HUE_LEFT_HAND,
            "status getters should report actual returned hue values by physical slot");
    require(runtime.lastHueError() == 0, "successful retry should clear previous hue error");

    g_ready_mask = 0;
    g_camera_read_count = 0;
    require(!runtime.update(frame, 0.6f), "disconnected controllers should not fabricate camera frames");
    require(!runtime.hueValuesKnown(), "disconnect should invalidate the prior physical-slot hue report");
    require(runtime.hueAssignmentPending(), "disconnect should mark calibrated LED assignment for replay");

    g_ready_mask = 3;
    g_camera_read_count = 1;
    require(runtime.update(frame, 0.6f), "reconnected fake Move controllers should update");
    require(g_hue_call_count == 4, "reconnection should re-submit the calibrated physical LED mapping");
    require(g_last_requested_hues[0] == GEM_HUE_RIGHT_HAND &&
            g_last_requested_hues[1] == GEM_HUE_LEFT_HAND,
            "reconnection should preserve the calibrated red/right and blue/left mapping");
    require(runtime.handHueAssignmentApplied(), "reconnection retry should reconfirm the physical LED mapping");

    // The Move button is a menu button on a tracked controller; it restarts
    // the sphere calibration only for a controller the camera cannot track.
    g_state_result = GEM_STATUS_OK;
    g_state_tracked = true;
    g_state_buttons = 0;
    require(runtime.update(frame, 0.6f), "tracked fake Move should update");
    require(frame.left.tracked && frame.right.tracked, "fake Move should be tracked");
    const unsigned int calibrations = g_calibrate_count;
    g_state_buttons = GEM_BUTTON_MOVE;
    require(runtime.update(frame, 0.6f), "Move press frame should update");
    require(g_calibrate_count == calibrations,
            "a tracked Move button press must not restart sphere calibration");
    g_state_buttons = 0;
    g_state_tracked = false;
    require(runtime.update(frame, 0.6f), "untracked fake Move should update");
    g_state_buttons = GEM_BUTTON_MOVE;
    require(runtime.update(frame, 0.6f), "untracked Move press frame should update");
    require(g_calibrate_count == calibrations,
            "a Move button press never recalibrates a sphere by itself");
    require(runtime.calibrateSlot(1) && g_calibrate_count == calibrations + 1u,
            "the game recalibrates a sphere only on request");

    runtime.shutdown();

    // libgem turns the left hand's blue (204) into purple (280), 72 degrees
    // from the right hand's red: the left sphere is asked for the hue
    // opposite red, and keeps it.
    resetFakes();
    {
        GemRuntime spread;
        spread.setHandHuesEnabled(true);
        spread.setMinHueGap(120);
        require(spread.initialize(), "fake libgem should initialize");
        g_hue_from = GEM_HUE_LEFT_HAND;
        g_hue_to = 280u;
        g_state_result = GEM_STATUS_OK;
        spread.setHandSlotsSwapped(false);
        require(spread.update(frame, 0.6f) && spread.hueForPhysicalSlot(0) == 280u,
                "libgem moved the blue request to purple");
        require(spread.update(frame, 0.6f) && g_last_requested_hues[0] == 172u &&
                g_last_requested_hues[1] == GEM_HUE_RIGHT_HAND,
                "the sphere close to red asks for the hue opposite it");
        require(spread.hueForPhysicalSlot(0) == 172u && spread.hueSpreadAttempts() == 1u &&
                !spread.hueAssignmentPending(), "far enough apart, the spreading stops");
        const unsigned int calls = g_hue_call_count;
        for (int i = 0; i < 10; ++i) spread.update(frame, 0.6f);
        require(g_hue_call_count == calls, "settled hues are not requested again");
        spread.shutdown();
    }
    // By default the hues stay as libgem sets them, even close together.
    resetFakes();
    {
        GemRuntime plain;
        plain.setHandHuesEnabled(true);
        require(plain.initialize(), "fake libgem should initialize");
        g_hue_from = GEM_HUE_LEFT_HAND;
        g_hue_to = 280u;
        g_state_result = GEM_STATUS_OK;
        plain.setHandSlotsSwapped(false);
        for (int i = 0; i < 10; ++i) plain.update(frame, 0.6f);
        require(plain.hueForPhysicalSlot(0) == 280u && plain.hueSpreadAttempts() == 0u &&
                g_hue_call_count == 1u, "by default close hues are not asked again");
        plain.shutdown();
    }
    // A slot libgem always paints close to the other: a few tries, then stop.
    resetFakes();
    {
        GemRuntime stuck;
        stuck.setHandHuesEnabled(true);
        stuck.setMinHueGap(120);
        require(stuck.initialize(), "fake libgem should initialize");
        g_slot0_forced = 340u;
        g_state_result = GEM_STATUS_OK;
        stuck.setHandSlotsSwapped(false);
        for (int i = 0; i < 20; ++i) stuck.update(frame, 0.6f);
        require(stuck.hueSpreadAttempts() == HUE_SPREAD_TRIES &&
                g_hue_call_count == 1u + HUE_SPREAD_TRIES,
                "hopeless hue spreading gives up after a few requests");
        stuck.setMinHueGap(0);
        stuck.shutdown();
    }
    require(hueGap(350u, 10u) == 20u && hueGap(10u, 190u) == 180u &&
            hueAwayFrom(352u, 0) == 172u, "hue distances wrap around the color wheel");
    require(hueColor(0u) == 0xffff0000u && hueColor(120u) == 0xff00ff00u &&
            hueColor(240u) == 0xff0000ffu, "hues map to screen colors");
    std::puts("All GemRuntime physical LED hue tests passed.");
}

#if !defined(__PPU__)
int main() {
    runGemRuntimeHueTests();
    return 0;
}
#endif
