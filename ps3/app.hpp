#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "audio_cache.hpp"
#include "audio_player.hpp"
#include "cut_feedback.hpp"
#include "demo_options.hpp"
#include "frame_timing.hpp"
#include "framebuffer.hpp"
#include "gem_runtime.hpp"
#include "level_library.hpp"
#include "library_shelf.hpp"
#include "menu_buttons.hpp"
#include "menu_pointer.hpp"
#include "move_input.hpp"
#include "run_diagnostics.hpp"
#include "saber_trail.hpp"
#include "menu_motion.hpp"
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
        // Both spheres seen by the camera (not just held by the sensors).
        bool both_moves_optical;
        // The camera's poses only (full or partial fix, never the sensor
        // hold): what calibration may measure.
        MoveFrame optical;
        bool hand_toggle;
        bool cancel_calibration;
        bool calibrated_start;
        // A lone Move tap (physical slot or -1) and both Move buttons held
        // for half a second.
        bool move_long_chord;
        int move_tap_slot;
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
    void readPlaylistTestInput(FrameInput& input);
    // The library is driven by a script (menu or playlist test).
    bool scriptedMenuRun() const {
        return demo_mode_ && (demo_options_.menu_test || demo_options_.playlist_test);
    }
    void recordMenuTest(const FrameInput& input);

    // Library scene.
    void handleHandToggle(FrameInput& input);
    void handleCalibrationCancel(FrameInput& input);
    void handleLibraryNavigation(const FrameInput& input);
    void handleLibrarySettings(const FrameInput& input);
    void handleBackButtons(FrameInput& input);
    void reloadSelectedChart();
    bool ensureSelectedChart();
    void selectLevel(std::size_t index);
    // Shelves: all tracks (0) or playlist k (k). Select steps through them.
    void handleShelfSwitch(const FrameInput& input);
    void showShelf(std::size_t shelf);
    // Selects the track at `position` of the shelf (playlist difficulty
    // suggestion included).
    void selectShelfPosition(std::size_t position);
    // Playlist run: the next song after a countdown, or at once.
    void updatePlaylistRun();
    void playNextPlaylistSong();
    // step: +1 harder, -1 easier; wrap for buttons, clamp for gestures;
    // hand: 0 left, 1 right, -1 none (feedback side).
    void changeDifficulty(int step, bool wrap, int hand);
    // Carousel motion, Move gestures, randomizer, ticks and song preview.
    void updateLibraryMotion(const FrameInput& input);
    void stopPreview(unsigned int fade_ms);
    void pulseRumble(int hand, int frames, int power);
    void applyRumble();
    void saveSettings();

    // Calibration and tracking recovery.
    void updateTrackingPause(FrameInput& input);
    // Leaves a level for the library (Triangle while tracking is lost).
    // Leaves a level for the library (pause menu or tracking pause).
    void quitLevel(bool tracking_lost);
    // Start during play: pause menu (continue, restart, back to tracks).
    void handlePauseMenu(FrameInput& input);
    void restartLevel();
    void resumeAfterTrackingLoss();
    void updateCalibration(FrameInput& input);
    void assignHands(const MoveFrame& moves);
    void setHandAssignment(bool slots_swapped);
    // Begins the guided three-pose calibration (forward, sides, down).
    void beginBodyCalibration();
    // Emulator scripts: holds the pose the current calibration step asks
    // for; `crossed` puts GEM slot 0 on the player's right.
    void scriptCalibrationPose(FrameInput& input, bool crossed) const;
    void handleStart(const FrameInput& input);
    // Results: continue (Cross, Circle, Move), replay (Triangle), stop a
    // playlist (Square).
    void handleResultsButtons(FrameInput& input);
    void backToLibrary();
    void beginLevel();
    // Start in the library (a playlist shelf begins a run), replay on the
    // results screen, or the next song of a playlist run.
    void startOrContinue();
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
    bool draw_background_;
    // Hand depth from the camera (default; renderer.ini camera_depth=off
    // keeps the hands at the forward pose's depth).
    bool camera_depth_;
    // renderer.ini depth_range_cm, in meters (applied to the body frame).
    float depth_range_;
    std::vector<PlaylistChoice> playlists_;
    std::size_t shelf_;
    // Library indices of the shown shelf, in carousel order, and the
    // carousel position of the selection in it.
    std::vector<std::size_t> shelf_order_;
    std::size_t shelf_pos_;
    PlaylistRun playlist_run_;
    // Countdown to the next song of a run on the results screen (0: none).
    unsigned int next_song_remaining_ms_;
    unsigned int last_playlist_ms_;
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
    // USB levels' music copied to the hard disk before it plays.
    AudioCache audio_cache_;
    // Starts copying the music at `path` if it lives on a USB drive.
    void cacheUsbAudio(const std::string& path);
    bool audio_clock_;
    int audio_start_error_;
    movebeat::HighScore best_score_;
    movebeat::Settings settings_;
    std::string settings_error_;
    std::string audio_path_;
    unsigned long long level_start_us_;
    RunState run_state_;

    // Camera space to play space, from the guided body calibration (and
    // re-centered by the quick recalibration after a tracking loss).
    movebeat::BodyFrame body_frame_;
    movebeat::BodyCalibration body_calibration_;
    bool hand_slots_swapped_;
    bool hand_assignment_established_;
    bool calibrating_;
    // A play origin was measured this session (calibration or recovery).
    bool pose_calibrated_;
    MenuButtonEdges menu_buttons_;
    MoveButtonChord move_chord_;
    MenuNavigationRepeat menu_navigation_repeat_;
    int left_rumble_frames_;
    int right_rumble_frames_;
    int left_rumble_power_;
    int right_rumble_power_;

    // Library carousel.
    CarouselScroll carousel_;
    MenuGesture gesture_;
    int gesture_hand_;
    MenuRandom random_;
    bool randomizer_spinning_;
    unsigned int last_motion_ms_;
    float last_tick_row_;
    unsigned int selection_changed_ms_;
    bool preview_active_;
    unsigned int halo_flash_ms_;
    bool paused_;
    // The player paused the song (Start) and sees the pause menu.
    bool user_paused_;
    bool tracking_paused_;
    bool tracking_recalibrating_;
    TrackingPauseGate tracking_gate_;
    // Since when both spheres are back in view during a tracking pause.
    unsigned int resume_since_ms_;
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
    unsigned int playlist_test_results_;
    bool menu_test_done_;
    const char* menu_capture_path_;
    unsigned int last_frame_active_us_;
    unsigned long long menu_step_total_us_;
};

} // namespace ps3
} // namespace movebeat
