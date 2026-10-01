#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "audio_player.hpp"
#include "cut_feedback.hpp"
#include "demo_options.hpp"
#include "frame_timing.hpp"
#include "framebuffer.hpp"
#include "gem_runtime.hpp"
#include "level_library.hpp"
#include "menu_buttons.hpp"
#include "menu_pointer.hpp"
#include "move_input.hpp"
#include "run_diagnostics.hpp"
#include "saber_trail.hpp"
#include "tracking_pause.hpp"
#include "movebeat/calibration.hpp"
#include "movebeat/game.hpp"
#include "movebeat/level.hpp"
#include "movebeat/records.hpp"
#include "movebeat/settings.hpp"

namespace movebeat {
namespace ps3 {

// The PS3 application: startup, the per-frame scene state machine
// (library, calibration, play, results, audio error) and shutdown.
class App {
public:
    App();

    // Runs until the XMB requests exit. Returns the process exit code.
    int run();

private:
    enum RunState {
        RUN_SELECT,
        RUN_PLAY,
        RUN_RESULTS,
        RUN_AUDIO_ERROR
    };

    // Everything read or derived from the controllers for one frame.
    struct FrameInput {
        MoveFrame moves;
        bool gem_update_ok;
        MenuButtonSample buttons;
        ScreenPointer move_pointers[2];
        bool pointer_start;
        bool calibration_hands_separated;
        unsigned short selection_pressed;
        unsigned short level_pressed;
        bool blade_modifier;
        bool timing_modifier;
        bool start_pressed;
        bool both_moves_tracked;
        bool hand_toggle;
        bool cancel_calibration;
        bool calibrated_start;
        unsigned int raw_level_time_ms;
        unsigned int level_time_ms;

        FrameInput();
    };

    // Startup; returns false with exit_code set when the app must stop.
    bool initializeSystems(int& exit_code);
    void runStartupFailure(GemRuntime::InitStage stage, int error);
    bool selectInitialLevel(int& exit_code);
    void loadSettings();
    void startDemo();
    void shutdown();

    void runFrame();
    void synchronizePause();
    void failAudio(const char* message);
    void checkAudioStream();
    void readInput(FrameInput& input);
    void readDemoInput(FrameInput& input);
    void readMenuTestInput(FrameInput& input);
    void recordMenuTest(const FrameInput& input);

    // Library scene.
    void handleHandToggle(FrameInput& input);
    void handleCalibrationCancel(FrameInput& input);
    void handleLibraryNavigation(const FrameInput& input);
    void handleLibrarySettings(const FrameInput& input);
    void handleBackButtons(const FrameInput& input);
    void reloadSelectedChart();
    bool ensureSelectedChart();
    void saveSettings();

    // Calibration and tracking recovery.
    void updateTrackingPause(FrameInput& input);
    void updateCalibration(FrameInput& input);
    void assignHands(const MoveFrame& moves);
    void handleStart(const FrameInput& input);
    bool startLevelAudio(const char* failure_message, bool clear_error_on_success);

    // Play scene.
    void updateGameplay(FrameInput& input);
    void finishLevel();
    void restartDemoCycle();

    void renderFrame(const FrameInput& input,
                     unsigned long long frame_work_start_us);
    void renderPlayfield(const FrameInput& input);
    void renderOverlays(const FrameInput& input);
    void saveDiagnosticFrames(const FrameInput& input);
    void publishDiagnostics(const FrameInput& input);
    void storeFrameTiming(bool only_with_samples);

    unsigned long long process_start_us_;
    RunStatus diagnostic_;
    Framebuffer screen_;
    GemRuntime gem_;
    EmulatorDemoOptions demo_options_;
    bool demo_mode_;

    std::vector<LevelChoice> level_choices_;
    movebeat::Chart selected_chart_;
    movebeat::LevelManifest selected_level_;
    std::string level_error_;
    std::string level_directory_;
    bool imported_level_;
    std::size_t selected_level_index_;
    std::size_t selected_chart_index_;
    // selected_chart_ holds the parsed chart of the current selection.
    bool selected_chart_loaded_;
    movebeat::GameConfig game_config_;
    movebeat::Game game_;
    unsigned int chart_duration_ms_;
    CutFeedback cut_feedback_;
    SaberTrail saber_trails_[2];
    AudioPlayer audio_;
    bool audio_clock_;
    int audio_start_error_;
    movebeat::HighScore best_score_;
    movebeat::Settings settings_;
    std::string settings_error_;
    std::string audio_path_;
    unsigned long long level_start_us_;
    RunState run_state_;

    movebeat::Vec3 play_origin_;
    bool hand_slots_swapped_;
    bool hand_assignment_established_;
    movebeat::PoseCalibration pose_calibration_;
    bool calibrating_;
    MenuButtonEdges menu_buttons_;
    MenuNavigationRepeat menu_navigation_repeat_;
    int left_rumble_frames_;
    int right_rumble_frames_;
    bool paused_;
    bool tracking_paused_;
    bool tracking_recalibrating_;
    TrackingPauseGate tracking_gate_;
    movebeat::PoseCalibration tracking_calibration_;
    unsigned long long pause_started_us_;

    unsigned int diagnostic_frame_;
    FrameTimingWindow frame_work_timing_;
    FrameTimingWindow frame_wait_timing_;
    FrameTimingWindow frame_active_timing_;
    FrameTimingWindow frame_render_timing_;
    FrameTimingWindow frame_clear_timing_;
    FrameTimingWindow frame_scene_timing_;
    FrameTimingWindow frame_gpu_notes_timing_;
    const char* previous_diagnostic_phase_;
    bool first_frame_saved_;
    bool demo_frame_saved_;
    bool demo_scored_frame_saved_;
    bool demo_timed_frame_saved_;
    unsigned int demo_cycles_;
    unsigned long long demo_loss_started_us_;
    bool demo_loss_finished_;
    unsigned int menu_test_frame_;
    bool menu_test_done_;
    unsigned int last_frame_active_us_;
    unsigned long long menu_step_total_us_;
};

} // namespace ps3
} // namespace movebeat
