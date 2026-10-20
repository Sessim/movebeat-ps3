#include "gem_runtime.hpp"

#include "imu_pose.hpp"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <io/camera.h>
#include <io/move.h>
#include <spurs/spurs.h>
#include <sys/memory.h>
#include <sys/spu.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>

namespace movebeat {
namespace ps3 {
namespace {

const unsigned int kCaptureFrames = 60u * 30u;
// About two seconds at 60 Hz before an unanswered hue request is repeated.
const unsigned int kHueRetryFrames = 120u;
// No hue override for a slot.
const u32 kNoHue = 0xffffffffu;

float vectorComponent(const vec_float4& value, unsigned int index) {
    union VectorAccess {
        vec_float4 vector;
        float values[4];
    } access;
    access.vector = value;
    return index < 4 ? access.values[index] : 0.0f;
}

void writeVector(FILE* output, const vec_float4& value) {
    for (unsigned int index = 0; index < 4; ++index)
        fprintf(output, ",%.7g", static_cast<double>(vectorComponent(value, index)));
}

bool moduleLoaded(int result) {
    return result == SYSMODULE_OK || result == static_cast<int>(SYSMODULE_ERR_DUPLICATE);
}

} // namespace

struct GemRuntime::Impl {
    bool camera_module;
    bool gem_module;
    bool container_created;
    bool camera_initialized;
    bool camera_open;
    bool camera_started;
    bool spurs_initialized;
    bool gem_initialized;
    bool hues_requested;
    bool hand_assignment_known;
    bool hand_slots_swapped;
    bool hue_assignment_dirty;
    bool hue_values_known;
    bool hand_hues_enabled;
    // Frames a hue request has gone unanswered (still HUE_NOT_SET).
    unsigned int hue_wait_frames;
    int last_results[2];
    unsigned int last_tracking[2];
    ImuSaberTracker imu[2];
    unsigned int imu_held_frames;
    int camera_exposure;
    float camera_quality;
    u32 left_hue;
    u32 right_hue;
    bool gem_filter;
    unsigned int update_count;
    u32 assigned_hues[4];
    int last_hue_error;
    // Keeping the two spheres' colors apart: the hues asked for instead of
    // the hand or automatic ones (kNoHue: none), the requests made, and the
    // widest pair seen.
    unsigned int min_hue_gap;
    u32 hue_override[2];
    unsigned int hue_spread_attempts;
    unsigned int best_hue_gap;
    u32 best_hues[2];
    bool best_hues_requested;
    bool calibration_requested[2];
    unsigned int connection_mask;
    int last_error;
    GemRuntime::InitStage last_stage;
    GemRuntime::UpdateStage last_update_stage;
    unsigned int last_camera_read_count;
    unsigned int last_gem_connected_count;
    unsigned int last_inertial_ready_mask;
    unsigned int last_button_ready_mask;
    sys_mem_container_t container;
    Spurs* spurs;
    void* gem_memory;
    cameraInfoEx camera_info;
    cameraReadInfo camera_read;
    unsigned short previous_buttons[2];
    int previous_results[2];
    unsigned char rumble[2];
    FILE* capture_file;
    unsigned int capture_frame;
    bool pitch_correction = true;
    int pitch_correction_result = 0;
    unsigned long long status_flags_seen = 0;
    unsigned long long last_status_flags[2] = {0, 0};
    gemCameraState camera_state;
    bool camera_state_valid = false;

    Impl()
        : camera_module(false), gem_module(false), container_created(false),
          camera_initialized(false), camera_open(false), camera_started(false),
          spurs_initialized(false), gem_initialized(false), hues_requested(false),
          hand_assignment_known(false), hand_slots_swapped(false),
          hue_assignment_dirty(false), hue_values_known(false),
          hand_hues_enabled(false), hue_wait_frames(0), imu_held_frames(0),
          camera_exposure(128), camera_quality(0.5f),
          left_hue(GEM_HUE_LEFT_HAND), right_hue(GEM_HUE_RIGHT_HAND), gem_filter(true),
          update_count(0),
          last_hue_error(0), min_hue_gap(120u), hue_spread_attempts(0),
          best_hue_gap(0), best_hues_requested(false),
          connection_mask(0), last_error(0),
          last_stage(GemRuntime::INIT_NONE),
          last_update_stage(GemRuntime::UPDATE_NONE),
          last_camera_read_count(0), last_gem_connected_count(0),
          last_inertial_ready_mask(0), last_button_ready_mask(0),
          container(0), spurs(0), gem_memory(0),
          capture_file(0), capture_frame(0) {
        memset(&camera_info, 0, sizeof(camera_info));
        memset(&camera_read, 0, sizeof(camera_read));
        memset(&camera_state, 0, sizeof(camera_state));
        previous_buttons[0] = previous_buttons[1] = 0;
        previous_results[0] = previous_results[1] = -1;
        rumble[0] = rumble[1] = 0;
        memset(assigned_hues, 0, sizeof(assigned_hues));
        hue_override[0] = hue_override[1] = kNoHue;
        best_hues[0] = best_hues[1] = kNoHue;
        calibration_requested[0] = calibration_requested[1] = false;
        last_results[0] = last_results[1] = -1;
        last_tracking[0] = last_tracking[1] = 0;
    }
};

GemRuntime::GemRuntime() : impl_(new Impl()) {}

GemRuntime::~GemRuntime() {
    shutdown();
    delete impl_;
}

bool GemRuntime::initialize() {
    shutdown();
    impl_->last_stage = INIT_CAMERA_MODULE;
    impl_->last_error = sysModuleLoad(SYSMODULE_CAMERA);
    if (!moduleLoaded(impl_->last_error)) return false;
    impl_->camera_module = true;
    impl_->last_stage = INIT_GEM_MODULE;
    impl_->last_error = sysModuleLoad(SYSMODULE_GEM);
    if (!moduleLoaded(impl_->last_error)) return false;
    impl_->gem_module = true;

    impl_->last_stage = INIT_SPU;
    impl_->last_error = sysSpuInitialize(6, 0);
    if (impl_->last_error != 0) return false;
    impl_->last_stage = INIT_SPURS_MEMORY;
    impl_->spurs = static_cast<Spurs*>(memalign(SPURS_ALIGN, sizeof(Spurs)));
    if (!impl_->spurs) { impl_->last_error = -1001; return false; }

    sys_ppu_thread_t thread_id;
    int ppu_priority = 0;
    impl_->last_stage = INIT_PPU_THREAD;
    if ((impl_->last_error = sysThreadGetId(&thread_id)) != 0 ||
        (impl_->last_error = sysThreadGetPriority(thread_id, &ppu_priority)) != 0) return false;
    SpursAttribute spurs_attribute;
    impl_->last_stage = INIT_SPURS_ATTRIBUTE;
    if ((impl_->last_error = spursAttributeInitialize(
            &spurs_attribute, 5, 250, ppu_priority - 1, true)) != 0) return false;
    const char prefix[] = "movebeat";
    if ((impl_->last_error = spursAttributeSetNamePrefix(
            &spurs_attribute, prefix, sizeof(prefix) - 1)) != 0) return false;
    impl_->last_stage = INIT_SPURS;
    if ((impl_->last_error = spursInitializeWithAttribute(
            impl_->spurs, &spurs_attribute)) != 0) return false;
    impl_->spurs_initialized = true;

    impl_->last_stage = INIT_GEM_MEMORY_SIZE;
    const int memory_size = gemGetMemorySize(2);
    if (memory_size <= 0) { impl_->last_error = memory_size; return false; }
    impl_->last_stage = INIT_GEM_MEMORY;
    impl_->gem_memory = memalign(128, memory_size);
    if (!impl_->gem_memory) { impl_->last_error = -1002; return false; }
    memset(impl_->gem_memory, 0, memory_size);

    gemAttribute gem_attribute;
    memset(&gem_attribute, 0, sizeof(gem_attribute));
    gem_attribute.version = MOVE_VERSION;
    gem_attribute.max = 2;
    gem_attribute.memory = impl_->gem_memory;
    gem_attribute.spurs = impl_->spurs;
    for (int i = 0; i < 5; ++i) gem_attribute.spu_priorities[i] = 1;
    impl_->last_stage = INIT_GEM;
    if ((impl_->last_error = gemInit(&gem_attribute)) != 0) return false;
    impl_->gem_initialized = true;

    impl_->last_stage = INIT_MEMORY_CONTAINER;
    if ((impl_->last_error = sysMemContainerCreate(&impl_->container, 0x200000)) != 0) return false;
    impl_->container_created = true;
    impl_->last_stage = INIT_CAMERA;
    if ((impl_->last_error = cameraInit()) != 0) return false;
    impl_->camera_initialized = true;

    cameraType camera_type = CAM_TYPE_UNKNOWN;
    impl_->last_stage = INIT_CAMERA_TYPE;
    if ((impl_->last_error = cameraGetType(0, &camera_type)) != 0) return false;
    if (camera_type != CAM_TYPE_PLAYSTATION_EYE) { impl_->last_error = -1003; return false; }

    impl_->camera_info.format = CAM_FORM_RAW8;
    impl_->camera_info.framerate = 60;
    impl_->camera_info.resolution = CAM_RESO_VGA;
    impl_->camera_info.info_ver = 0x0101;
    impl_->camera_info.container = impl_->container;
    impl_->last_stage = INIT_CAMERA_OPEN;
    if ((impl_->last_error = cameraOpenEx(0, &impl_->camera_info)) != 0) return false;
    impl_->camera_open = true;
    impl_->camera_read.version = 0x0100;
    impl_->camera_read.buffer = impl_->camera_info.buffer;

    impl_->last_stage = INIT_GEM_CAMERA;
    if ((impl_->last_error = gemPrepareCamera(impl_->camera_exposure,
                                              impl_->camera_quality)) != 0) return false;
    gemReset(0);
    gemReset(1);
    // libgem's own state filter smooths positions it reports.
    gemFilterState(0, impl_->gem_filter ? 1u : 0u);
    gemFilterState(1, impl_->gem_filter ? 1u : 0u);
    // The PS Eye usually sits tilted (under or on top of the TV); without
    // the correction, raising a hand also moves it in depth.
    impl_->pitch_correction_result =
        gemEnableCameraPitchAngleCorrection(impl_->pitch_correction ? 1 : 0);
    cameraReset(0);
    impl_->last_stage = INIT_CAMERA_START;
    if ((impl_->last_error = cameraStart(0)) != 0) return false;
    impl_->camera_started = true;
    impl_->last_error = 0;
    impl_->last_stage = INIT_READY;
    return true;
}

void GemRuntime::shutdown() {
    if (!impl_) return;
    stopCapture();
    if (impl_->gem_initialized) {
        gemSetRumble(0, 0);
        gemSetRumble(1, 0);
    }
    // libgem owns SPURS-backed tracking state and may retain references to the
    // camera buffers/container until gemEnd. Tear it down before releasing the
    // camera resources it was prepared against.
    if (impl_->gem_initialized) { gemEnd(); impl_->gem_initialized = false; }
    if (impl_->camera_started) { cameraStop(0); impl_->camera_started = false; }
    if (impl_->camera_open) { cameraClose(0); impl_->camera_open = false; }
    if (impl_->camera_initialized) { cameraEnd(); impl_->camera_initialized = false; }
    if (impl_->container_created) {
        sysMemContainerDestroy(impl_->container);
        impl_->container_created = false;
    }
    if (impl_->spurs_initialized) { spursFinalize(impl_->spurs); impl_->spurs_initialized = false; }
    if (impl_->gem_memory) { free(impl_->gem_memory); impl_->gem_memory = 0; }
    if (impl_->spurs) { free(impl_->spurs); impl_->spurs = 0; }
    if (impl_->gem_module) { sysModuleUnload(SYSMODULE_GEM); impl_->gem_module = false; }
    if (impl_->camera_module) { sysModuleUnload(SYSMODULE_CAMERA); impl_->camera_module = false; }
    impl_->hues_requested = false;
    impl_->hand_assignment_known = false;
    impl_->hand_slots_swapped = false;
    impl_->hue_assignment_dirty = false;
    impl_->hue_values_known = false;
    memset(impl_->assigned_hues, 0, sizeof(impl_->assigned_hues));
    impl_->last_hue_error = 0;
    clearHueSpread();
    impl_->calibration_requested[0] = impl_->calibration_requested[1] = false;
    impl_->connection_mask = 0;
    impl_->previous_buttons[0] = impl_->previous_buttons[1] = 0;
    impl_->previous_results[0] = impl_->previous_results[1] = -1;
    impl_->rumble[0] = impl_->rumble[1] = 0;
    impl_->hue_wait_frames = 0;
    impl_->imu[0].reset();
    impl_->imu[1].reset();
    impl_->last_results[0] = impl_->last_results[1] = -1;
    impl_->last_tracking[0] = impl_->last_tracking[1] = 0;
}

void GemRuntime::setPitchCorrection(bool enabled) { impl_->pitch_correction = enabled; }
int GemRuntime::pitchCorrectionResult() const { return impl_->pitch_correction_result; }
unsigned long long GemRuntime::statusFlagsSeen() const { return impl_->status_flags_seen; }
unsigned long long GemRuntime::lastStatusFlags(unsigned int slot) const {
    return slot < 2 ? impl_->last_status_flags[slot] : 0ull;
}
bool GemRuntime::cameraState(int& exposure, float& exposure_time, float& gain,
                             float& pitch, float& pitch_estimate) const {
    exposure = impl_->camera_state.exposure;
    exposure_time = impl_->camera_state.exposure_time;
    gain = impl_->camera_state.gain;
    pitch = impl_->camera_state.pitch_angle;
    pitch_estimate = impl_->camera_state.pitch_angle_estimate;
    return impl_->camera_state_valid;
}

bool GemRuntime::update(MoveFrame& frame, float blade_length_m,
                        bool read_image, unsigned int now_ms) {
    frame = MoveFrame();
    ++impl_->update_count;
    if (!now_ms) now_ms = impl_->update_count * 17u;
    impl_->last_update_stage = UPDATE_NONE;
    impl_->last_camera_read_count = 0;
    impl_->last_gem_connected_count = 0;
    impl_->last_inertial_ready_mask = 0;
    impl_->last_button_ready_mask = 0;
    if (!impl_->camera_started || !impl_->gem_initialized) return false;

    // Connection state does not require a new camera frame. Keep the menu and
    // remote diagnostics useful if video delivery pauses or fails.
    gemInfo info;
    memset(&info, 0, sizeof(info));
    impl_->last_update_stage = UPDATE_GEM_INFO;
    if ((impl_->last_error = gemGetInfo(&info)) != 0) return false;
    impl_->last_gem_connected_count = info.connected;
    unsigned int connection_mask = 0;
    for (unsigned int index = 0; index < 2; ++index) {
        if (gemSlotReady(info.status[index])) connection_mask |= 1u << index;
    }
    if (connection_mask != impl_->connection_mask) {
        impl_->hues_requested = false;
        clearHueSpread();
        if (impl_->hand_assignment_known && impl_->hand_hues_enabled) {
            impl_->hue_assignment_dirty = true;
            impl_->hue_values_known = false;
            impl_->last_hue_error = 0;
        }
        for (unsigned int index = 0; index < 2; ++index) {
            if ((connection_mask & (1u << index)) == 0) {
                impl_->calibration_requested[index] = false;
                impl_->previous_buttons[index] = 0;
                impl_->previous_results[index] = -1;
                impl_->rumble[index] = 0;
                impl_->imu[index].reset();
            }
        }
        impl_->connection_mask = connection_mask;
    }
    frame.left_connected = (connection_mask & 1u) != 0;
    frame.right_connected = (connection_mask & 2u) != 0;

    // Camera delivery can pause while the Move controller still supplies
    // button/trigger samples. Keep menu navigation responsive, but never
    // fabricate a tracked 3D pose without an optical frame.
    for (unsigned int index = 0; index < 2; ++index) {
        if ((connection_mask & (1u << index)) == 0) continue;
        gemInertialState inertial;
        memset(&inertial, 0, sizeof(inertial));
        const int inertial_result = gemGetInertialState(
            index, GEM_INERTIAL_LATEST, 0, &inertial);
        if (inertial_result == 0) {
            impl_->last_inertial_ready_mask |= 1u << index;
            impl_->last_button_ready_mask |= 1u << index;
        }
        applyInertialSample(index, inertial, inertial_result, frame);
    }

    impl_->last_update_stage = UPDATE_CAMERA_READ;
    impl_->last_error = cameraReadEx(0, &impl_->camera_read);
    if (impl_->last_error != 0) return false;
    impl_->last_update_stage = UPDATE_CAMERA_FRAME;
    impl_->last_camera_read_count = impl_->camera_read.readcount;
    if (impl_->last_camera_read_count == 0) return false;
    impl_->last_update_stage = UPDATE_GEM_START;
    if ((impl_->last_error = gemUpdateStart(
            reinterpret_cast<void*>(static_cast<u64>(impl_->camera_read.buffer)),
            impl_->camera_read.timestamp)) != 0) return false;
    impl_->last_update_stage = UPDATE_GEM_FINISH;
    if ((impl_->last_error = gemUpdateFinish()) != 0) return false;

    impl_->last_update_stage = UPDATE_READY;
    // Exposure, gain and the camera's pitch, once a second.
    if (impl_->update_count % 60u == 1u)
        impl_->camera_state_valid = gemGetCameraState(&impl_->camera_state) == 0;

    gemState states[2];
    memset(states, 0, sizeof(states));
    int results[2] = { -1, -1 };
    gemImageState images[2];
    memset(images, 0, sizeof(images));
    int image_results[2] = { -1, -1 };
    const unsigned short inertial_buttons[2] = {
        frame.left_buttons, frame.right_buttons
    };
    const unsigned short inertial_triggers[2] = {
        frame.left_trigger, frame.right_trigger
    };
    int recovery_error = 0;
    for (unsigned int index = 0; index < 2; ++index) {
        if ((connection_mask & (1u << index)) == 0) continue;
        results[index] = gemGetState(index, STATE_CURRENT_TIME, 0, &states[index]);
        applyGemSample(index, states[index], results[index], blade_length_m, frame);
        {
            // The camera's pose, or the last one turned by the Move's own
            // orientation while the camera has lost the sphere.
            SaberPose& pose = index == 0 ? frame.left : frame.right;
            const RawQuat quat(vectorComponent(states[index].quat, 0),
                               vectorComponent(states[index].quat, 1),
                               vectorComponent(states[index].quat, 2),
                               vectorComponent(states[index].quat, 3));
            pose = impl_->imu[index].update(now_ms, pose, results[index] == 0, quat,
                                            frame.partial[index]);
            frame.held[index] = impl_->imu[index].held();
            if (frame.held[index]) ++impl_->imu_held_frames;
        }
        // The image position is read every frame: where a sphere was last
        // seen tells whether it left the frame when tracking fails.
        (void)read_image;
        if (results[index] == 0) {
            image_results[index] = gemGetImageState(index, &images[index]);
            const int camera_width = impl_->camera_info.width > 0
                ? impl_->camera_info.width : 640;
            const int camera_height = impl_->camera_info.height > 0
                ? impl_->camera_info.height : 480;
            const gemImageState& image = images[index];
            // The radius is often not valid when the place is: the console
            // log kept one place for seconds while it waited for both.
            if (image_results[index] == 0 && image.visible &&
                std::isfinite(image.u) && std::isfinite(image.v) &&
                image.u >= 0.0f && image.u < camera_width &&
                image.v >= 0.0f && image.v < camera_height) {
                frame.image[index].x = image.u / camera_width;
                frame.image[index].y = image.v / camera_height;
                frame.image[index].radius = image.r_valid ? image.r : 0.0f;
                frame.image[index].valid = true;
            }
        }
        // What libgem noticed (lighting, colours, calibration trouble):
        // collected for the session and cleared once read.
        u64 status_flags = 0;
        if (gemGetStatusFlags(index, &status_flags) == 0 && status_flags) {
            impl_->last_status_flags[index] = status_flags;
            impl_->status_flags_seen |= status_flags;
            gemClearStatusFlags(index, status_flags);
        }
        if (results[index] == 0) impl_->last_button_ready_mask |= 1u << index;
        if (results[index] != 0) {
            if (index == 0) {
                frame.left_buttons = inertial_buttons[index];
                frame.left_trigger = inertial_triggers[index];
            } else {
                frame.right_buttons = inertial_buttons[index];
                frame.right_trigger = inertial_triggers[index];
            }
        }
        const unsigned short buttons = index == 0
            ? frame.left_buttons : frame.right_buttons;
        impl_->previous_buttons[index] = buttons;
        if (results[index] == GEM_STATUS_OK)
            impl_->calibration_requested[index] = false;
        const bool calibration_finished_without_success =
            gemResultNeedsCalibration(results[index]) &&
            impl_->previous_results[index] == GEM_STATUS_SPHERE_CALIBRATING;
        if (gemResultNeedsCalibration(results[index]) &&
            (!impl_->calibration_requested[index] ||
             calibration_finished_without_success)) {
            const int calibration_error = gemCalibrate(index);
            if (calibration_error == 0) {
                impl_->calibration_requested[index] = true;
                impl_->hues_requested = false;
            } else {
                recovery_error = calibration_error;
            }
        }
        // The Move button never recalibrates by itself: a calibration also
        // sets the orientation reference, which needs the Move pointed at
        // the camera; pressed any other way (the randomizer, say) it left a
        // blade turned round. The game asks for it (calibrateSlot) where
        // the player is told to point at the camera.
        impl_->previous_results[index] = results[index];
        impl_->last_results[index] = results[index];
        impl_->last_tracking[index] = results[index] == 0 ? states[index].tracking : 0u;
    }

    if (impl_->capture_file) {
        for (unsigned int index = 0; index < 2; ++index) {
            const gemState& state = states[index];
            const gemImageState& image = images[index];
            const int image_result = image_results[index];
            fprintf(impl_->capture_file, "%u,%u,%d,%lld,%lld,%u,%u,%u",
                    impl_->capture_frame, index, results[index],
                    static_cast<long long>(impl_->camera_read.timestamp),
                    static_cast<long long>(state.time), state.tracking,
                    static_cast<unsigned int>(state.paddata.buttons),
                    static_cast<unsigned int>(state.paddata.ANA_T));
            writeVector(impl_->capture_file, state.pos);
            writeVector(impl_->capture_file, state.handle_pos);
            writeVector(impl_->capture_file, state.vel);
            writeVector(impl_->capture_file, state.accel);
            writeVector(impl_->capture_file, state.quat);
            fprintf(impl_->capture_file, ",%d,%.9g,%.9g,%.9g,%.9g,%u,%u",
                    image_result, static_cast<double>(image.u),
                    static_cast<double>(image.v), static_cast<double>(image.r),
                    static_cast<double>(image.distance),
                    static_cast<unsigned int>(image.visible),
                    static_cast<unsigned int>(image.r_valid));
            fputc('\n', impl_->capture_file);
        }
        ++impl_->capture_frame;
        if ((impl_->capture_frame % 60u) == 0u) fflush(impl_->capture_file);
        if (impl_->capture_frame >= kCaptureFrames) stopCapture();
    }

    bool needs_hues = false;
    bool all_ready = connection_mask != 0;
    for (unsigned int i = 0; i < 2; ++i) {
        needs_hues = needs_hues || gemResultNeedsHues(results[i]);
        if ((connection_mask & (1u << i)) != 0 &&
            results[i] != GEM_STATUS_OK) all_ready = false;
    }
    if (impl_->hue_assignment_dirty ||
        (needs_hues && !impl_->hues_requested)) {
        const int hue_error = requestHues(connection_mask);
        if (hue_error != 0) recovery_error = hue_error;
    }

    // A later HUE_NOT_SET result should be able to start a fresh request after
    // a controller has been fully usable or after the connection count changes.
    if (all_ready) impl_->hues_requested = false;
    // libgem may answer a request and still report HUE_NOT_SET; never wait
    // for "all ready" forever (it cannot come without a hue): ask again.
    if (needs_hues && impl_->hues_requested) {
        if (++impl_->hue_wait_frames >= kHueRetryFrames) {
            impl_->hues_requested = false;
            impl_->hue_wait_frames = 0;
        }
    } else {
        impl_->hue_wait_frames = 0;
    }

    impl_->last_error = recovery_error;
    return true;
}

u32 GemRuntime::requestedHue(unsigned int slot) const {
    if (impl_->hue_override[slot] != kNoHue) return impl_->hue_override[slot];
    if (impl_->hand_assignment_known && impl_->hand_hues_enabled)
        return gemHueForPhysicalSlot(slot, impl_->hand_slots_swapped,
                                     impl_->left_hue, impl_->right_hue);
    return GEM_HUE_DONT_CARE;
}

void GemRuntime::clearHueSpread() {
    impl_->hue_override[0] = impl_->hue_override[1] = kNoHue;
    impl_->hue_spread_attempts = 0;
    impl_->best_hue_gap = 0;
    impl_->best_hues[0] = impl_->best_hues[1] = kNoHue;
    impl_->best_hues_requested = false;
}

int GemRuntime::requestHues(unsigned int connection_mask) {
    const u32 requested_hues[4] = {
        requestedHue(0), requestedHue(1), GEM_HUE_DONT_CARE, GEM_HUE_DONT_CARE
    };
    u32 assigned_hues[4] = {};
    const int hue_error = gemTrackHues(requested_hues, assigned_hues);
    impl_->last_hue_error = hue_error;
    if (hue_error != 0) return hue_error;
    memcpy(impl_->assigned_hues, assigned_hues, sizeof(impl_->assigned_hues));
    impl_->hue_values_known = true;
    impl_->hue_assignment_dirty = false;
    impl_->hues_requested = true;

    // libgem may move a requested hue the room makes hard to track (blue
    // became purple next to red on a console). Two spheres that end up
    // close are hard to tell apart: ask for another hue for one of them.
    const u32 first = assigned_hues[0], second = assigned_hues[1];
    if (!impl_->min_hue_gap || connection_mask != 3u || first >= 360u || second >= 360u)
        return 0;
    const unsigned int gap = hueGap(first, second);
    if (gap > impl_->best_hue_gap) {
        impl_->best_hue_gap = gap;
        impl_->best_hues[0] = first;
        impl_->best_hues[1] = second;
    }
    if (gap >= impl_->min_hue_gap) return 0;
    if (impl_->hue_spread_attempts >= HUE_SPREAD_TRIES) {
        // No try kept them apart enough: settle on the widest pair seen.
        if (!impl_->best_hues_requested && gap < impl_->best_hue_gap) {
            impl_->hue_override[0] = impl_->best_hues[0];
            impl_->hue_override[1] = impl_->best_hues[1];
            impl_->best_hues_requested = true;
            impl_->hue_assignment_dirty = true;
        }
        return 0;
    }
    // The sphere that got the hue it asked for keeps it (the right hand's
    // when both did); the other one moves away from it.
    const unsigned int anchor = second == requested_hues[1] ? 1u
                              : (first == requested_hues[0] ? 0u : 1u);
    const u32 anchor_hue = assigned_hues[anchor];
    impl_->hue_override[anchor] = anchor_hue;
    impl_->hue_override[1u - anchor] = hueAwayFrom(anchor_hue, impl_->hue_spread_attempts++);
    impl_->hue_assignment_dirty = true;
    printf("Sphere hues %u and %u are %u degrees apart; asking slot %u for %u\n",
           first, second, gap, 1u - anchor, impl_->hue_override[1u - anchor]);
    return 0;
}

void GemRuntime::setMinHueGap(unsigned int degrees) {
    if (impl_) impl_->min_hue_gap = degrees > 180u ? 180u : degrees;
}

unsigned int GemRuntime::hueSpreadAttempts() const {
    return impl_ ? impl_->hue_spread_attempts : 0u;
}

void GemRuntime::setHandSlotsSwapped(bool slots_swapped) {
    if (!impl_) return;
    if (!impl_->hand_assignment_known ||
        impl_->hand_slots_swapped != slots_swapped) {
        impl_->hand_assignment_known = true;
        impl_->hand_slots_swapped = slots_swapped;
        clearHueSpread();
        if (!impl_->hand_hues_enabled) return;
        impl_->hue_assignment_dirty = true;
        impl_->hue_values_known = false;
        impl_->last_hue_error = 0;
    }
}

void GemRuntime::setStateFilter(bool enabled) {
    if (impl_) impl_->gem_filter = enabled;
}

void GemRuntime::setHandHueValues(unsigned int left_hue, unsigned int right_hue) {
    if (!impl_) return;
    impl_->left_hue = left_hue % 360u;
    impl_->right_hue = right_hue % 360u;
}

void GemRuntime::setCameraTuning(int max_exposure, float image_quality) {
    if (!impl_) return;
    impl_->camera_exposure = max_exposure < 40 ? 40 : (max_exposure > 511 ? 511 : max_exposure);
    impl_->camera_quality = !(image_quality >= 0.0f) ? 0.0f
                          : (image_quality > 1.0f ? 1.0f : image_quality);
}

bool GemRuntime::calibrateSlot(unsigned int slot) {
    if (!impl_ || !impl_->gem_initialized || slot > 1 ||
        (impl_->connection_mask & (1u << slot)) == 0)
        return false;
    if (gemCalibrate(slot) != 0) return false;
    gemFilterState(slot, impl_->gem_filter ? 1u : 0u);
    impl_->calibration_requested[slot] = true;
    // A recalibrated sphere loses its hue: ask again as soon as libgem
    // reports it (in the hand color once the hands are known).
    impl_->hues_requested = false;
    return true;
}

void GemRuntime::setHandHuesEnabled(bool enabled) {
    if (impl_) impl_->hand_hues_enabled = enabled;
}

unsigned int GemRuntime::imuHeldFrames() const {
    return impl_ ? impl_->imu_held_frames : 0u;
}

int GemRuntime::imuVariant(unsigned int slot) const {
    return impl_ && slot < 2 ? impl_->imu[slot].bestVariant() : 0;
}

float GemRuntime::imuErrorDegrees(unsigned int slot) const {
    return impl_ && slot < 2
        ? impl_->imu[slot].errorDegrees(impl_->imu[slot].bestVariant()) : 0.0f;
}

unsigned int GemRuntime::imuScored(unsigned int slot) const {
    return impl_ && slot < 2 ? impl_->imu[slot].scoredSamples() : 0u;
}

int GemRuntime::lastResult(unsigned int slot) const {
    return impl_ && slot < 2 ? impl_->last_results[slot] : -1;
}

unsigned int GemRuntime::lastTrackingFlags(unsigned int slot) const {
    return impl_ && slot < 2 ? impl_->last_tracking[slot] : 0u;
}

bool GemRuntime::startCapture(const char* path) {
    if (!impl_ || !path || !path[0]) return false;
    stopCapture();
#if defined(_MSC_VER)
    if (fopen_s(&impl_->capture_file, path, "w") != 0)
        impl_->capture_file = 0;
#else
    impl_->capture_file = fopen(path, "w");
#endif
    if (!impl_->capture_file) return false;
    impl_->capture_frame = 0;
    fputs("frame,controller,result,camera_timestamp,gem_time,tracking,buttons,trigger"
          ",pos_x,pos_y,pos_z,pos_w,handle_x,handle_y,handle_z,handle_w"
          ",vel_x,vel_y,vel_z,vel_w,accel_x,accel_y,accel_z,accel_w"
          ",quat_x,quat_y,quat_z,quat_w"
          ",image_result,image_u,image_v,image_radius,image_distance"
          ",image_visible,image_radius_valid\n", impl_->capture_file);
    return true;
}

void GemRuntime::stopCapture() {
    if (!impl_ || !impl_->capture_file) return;
    fflush(impl_->capture_file);
    fclose(impl_->capture_file);
    impl_->capture_file = 0;
}

bool GemRuntime::captureActive() const {
    return impl_ && impl_->capture_file != 0;
}

int GemRuntime::lastError() const {
    return impl_ ? impl_->last_error : -1;
}

GemRuntime::InitStage GemRuntime::lastStage() const {
    return impl_ ? impl_->last_stage : INIT_NONE;
}

GemRuntime::UpdateStage GemRuntime::lastUpdateStage() const {
    return impl_ ? impl_->last_update_stage : UPDATE_NONE;
}

unsigned int GemRuntime::lastCameraReadCount() const {
    return impl_ ? impl_->last_camera_read_count : 0u;
}

unsigned int GemRuntime::lastGemConnectedCount() const {
    return impl_ ? impl_->last_gem_connected_count : 0u;
}

unsigned int GemRuntime::lastInertialReadyMask() const {
    return impl_ ? impl_->last_inertial_ready_mask : 0u;
}

unsigned int GemRuntime::lastButtonReadyMask() const {
    return impl_ ? impl_->last_button_ready_mask : 0u;
}

bool GemRuntime::hueValuesKnown() const {
    return impl_ && impl_->hue_values_known;
}

bool GemRuntime::handHueAssignmentApplied() const {
    return impl_ && impl_->hand_assignment_known && impl_->hue_values_known &&
           !impl_->hue_assignment_dirty;
}

bool GemRuntime::hueAssignmentPending() const {
    return impl_ && impl_->hue_assignment_dirty;
}

unsigned int GemRuntime::hueForPhysicalSlot(unsigned int slot) const {
    return impl_ && slot < 4u && impl_->hue_values_known
        ? impl_->assigned_hues[slot] : 0u;
}

int GemRuntime::lastHueError() const {
    return impl_ ? impl_->last_hue_error : 0;
}

void GemRuntime::setRumble(unsigned int index, unsigned char intensity) {
    if (impl_ && impl_->gem_initialized && index < 2 &&
        (impl_->connection_mask & (1u << index)) != 0 &&
        impl_->rumble[index] != intensity) {
        gemSetRumble(index, intensity);
        impl_->rumble[index] = intensity;
    }
}

} // namespace ps3
} // namespace movebeat
