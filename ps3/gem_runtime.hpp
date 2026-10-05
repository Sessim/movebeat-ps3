#ifndef MOVEBEAT_PS3_GEM_RUNTIME_HPP
#define MOVEBEAT_PS3_GEM_RUNTIME_HPP

#include "move_input.hpp"

namespace movebeat {
namespace ps3 {

class GemRuntime {
public:
    enum InitStage {
        INIT_NONE = 0,
        INIT_CAMERA_MODULE = 1,
        INIT_GEM_MODULE = 2,
        INIT_SPU = 3,
        INIT_SPURS_MEMORY = 4,
        INIT_PPU_THREAD = 5,
        INIT_SPURS_ATTRIBUTE = 6,
        INIT_SPURS = 7,
        INIT_GEM_MEMORY_SIZE = 8,
        INIT_GEM_MEMORY = 9,
        INIT_GEM = 10,
        INIT_MEMORY_CONTAINER = 11,
        INIT_CAMERA = 12,
        INIT_CAMERA_TYPE = 13,
        INIT_CAMERA_OPEN = 14,
        INIT_GEM_CAMERA = 15,
        INIT_CAMERA_START = 16,
        INIT_READY = 17
    };
    enum UpdateStage {
        UPDATE_NONE = 0,
        UPDATE_CAMERA_READ = 1,
        UPDATE_CAMERA_FRAME = 2,
        UPDATE_GEM_START = 3,
        UPDATE_GEM_FINISH = 4,
        UPDATE_GEM_INFO = 5,
        UPDATE_READY = 6
    };

    GemRuntime();
    ~GemRuntime();

    bool initialize();
    void shutdown();
    bool update(MoveFrame& frame, float blade_length_m, bool read_image = false);
    void setHandSlotsSwapped(bool slots_swapped);
    // Paint the spheres in the hand colors (blue left, red right) once the
    // hands are known. Off by default: libgem then keeps choosing hues the
    // camera can tell apart from the room, and a fixed hue that blends in
    // (red against skin or warm lamps) loses tracking.
    void setHandHuesEnabled(bool enabled);
    bool startCapture(const char* path);
    void stopCapture();
    bool captureActive() const;
    void setRumble(unsigned int index, unsigned char intensity);
    int lastError() const;
    InitStage lastStage() const;
    UpdateStage lastUpdateStage() const;
    unsigned int lastCameraReadCount() const;
    unsigned int lastGemConnectedCount() const;
    unsigned int lastInertialReadyMask() const;
    unsigned int lastButtonReadyMask() const;
    bool hueValuesKnown() const;
    bool handHueAssignmentApplied() const;
    bool hueAssignmentPending() const;
    unsigned int hueForPhysicalSlot(unsigned int slot) const;
    int lastHueError() const;
    // gemGetState result (GEM_STATUS_*, -1 when not read) and gemState
    // tracking flags of a physical slot in the latest update.
    int lastResult(unsigned int slot) const;
    unsigned int lastTrackingFlags(unsigned int slot) const;

private:
    struct Impl;
    Impl* impl_;

    GemRuntime(const GemRuntime&);
    GemRuntime& operator=(const GemRuntime&);
};

} // namespace ps3
} // namespace movebeat

#endif
