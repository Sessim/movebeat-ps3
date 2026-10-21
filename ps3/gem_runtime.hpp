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
    // `now_ms` drives the inertial hold of a sphere the camera lost (0: one
    // frame counts as 17 ms).
    bool update(MoveFrame& frame, float blade_length_m, bool read_image = false,
                unsigned int now_ms = 0);
    void setHandSlotsSwapped(bool slots_swapped);
    // Paint the spheres in the hand colors (blue left, red right) once the
    // hands are known. Off by default: libgem then keeps choosing hues the
    // camera can tell apart from the room, and a fixed hue that blends in
    // (red against skin or warm lamps) loses tracking.
    void setHandHuesEnabled(bool enabled);
    // Restarts the sphere calibration of a connected physical slot (the
    // controller should point at the camera and stay still meanwhile).
    bool calibrateSlot(unsigned int slot);
    // gemPrepareCamera tuning, applied by the next initialize(): maximum
    // exposure (40..511, higher is brighter but blurs motion) and image
    // quality (0 favors tracking, 1 the picture).
    void setCameraTuning(int max_exposure, float image_quality);
    // Hue angles (0..359) of the left and right hand spheres.
    void setHandHueValues(unsigned int left_hue, unsigned int right_hue);
    // Two spheres whose hues libgem placed closer than this (degrees around
    // the color wheel) get another hue for one of them; 0 (the default)
    // accepts any.
    void setMinHueGap(unsigned int degrees);
    // Hue requests made to keep the spheres apart since the hands or the
    // controllers last changed.
    unsigned int hueSpreadAttempts() const;
    // libgem's state filter (gemFilterState), from the next initialize().
    void setStateFilter(bool enabled);
    // libgem's camera pitch correction (gemEnableCameraPitchAngleCorrection),
    // from the next initialize(): positions come out level even when the
    // PS Eye is tilted, instead of mixing height into depth.
    void setPitchCorrection(bool enabled);
    int pitchCorrectionResult() const;
    // libgem's status flags (CELL_GEM_FLAG_*): every flag raised since
    // start, and the latest ones raised for a physical slot.
    unsigned long long statusFlagsSeen() const;
    unsigned long long lastStatusFlags(unsigned int slot) const;
    // The PS Eye as libgem drives it (gemGetCameraState, every second):
    // exposure, exposure time, gain, pitch angle and its estimate.
    bool cameraState(int& exposure, float& exposure_time, float& gain,
                     float& pitch, float& pitch_estimate) const;
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
    // Inertial hold: frames held so far, and per slot the quaternion
    // reading in use and its smoothed disagreement with the camera.
    unsigned int imuHeldFrames() const;
    int imuVariant(unsigned int slot) const;
    float imuErrorDegrees(unsigned int slot) const;
    unsigned int imuScored(unsigned int slot) const;

private:
    // The hue a physical slot asks libgem for: a spreading override, the
    // hand's color, or libgem's choice.
    u32 requestedHue(unsigned int slot) const;
    // Asks libgem for both slots' hues; keeps the answer and, when the two
    // came out too close, queues another request. Returns libgem's error.
    int requestHues(unsigned int connection_mask);
    void clearHueSpread();
    struct Impl;
    Impl* impl_;

    GemRuntime(const GemRuntime&);
    GemRuntime& operator=(const GemRuntime&);
};

} // namespace ps3
} // namespace movebeat

#endif
