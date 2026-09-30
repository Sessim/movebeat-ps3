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
}

} // namespace

#if defined(_MSC_VER)
void* memalign(std::size_t, std::size_t size) {
    return std::malloc(size);
}
#endif

s32 gemGetState(u32, u32, system_time_t, gemState*) { return g_state_result; }
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
    return 0;
}
s32 gemCalibrate(u32) { return 0; }
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
    GemRuntime runtime;
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

    runtime.shutdown();
    std::puts("All GemRuntime physical LED hue tests passed.");
}

#if !defined(__PPU__)
int main() {
    runGemRuntimeHueTests();
    return 0;
}
#endif
