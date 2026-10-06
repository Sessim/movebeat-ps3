#include "app.hpp"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <sys/systime.h>
#include <sysutil/sysutil.h>

#include "app_paths.hpp"
#include "demo_input.hpp"
#include "hand_assignment.hpp"
#include "playfield_draw.hpp"
#include "projection.hpp"
#include "screens_draw.hpp"
#include "stage_layout.hpp"
#include "text_draw.hpp"

namespace movebeat {
namespace ps3 {
namespace {

volatile bool g_running = true;
volatile bool g_pause_requested = false;

const unsigned short kButtonStart = 1u << 3;
const unsigned short kButtonSelect = 1u << 0;
const unsigned short kButtonTriangle = 1u << 4;
const unsigned short kButtonCircle = 1u << 5;
const unsigned short kButtonCross = 1u << 6;
const unsigned short kButtonSquare = 1u << 7;

void systemEvent(u64 status, u64, void*) {
    if (status == SYSUTIL_EXIT_GAME) g_running = false;
    else if (status == SYSUTIL_MENU_OPEN)
        g_pause_requested = true;
    else if (status == SYSUTIL_MENU_CLOSE)
        g_pause_requested = false;
}

int clamp(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

unsigned int adjustedTime(unsigned int raw_time_ms, int offset_ms) {
    const long long adjusted = static_cast<long long>(raw_time_ms) + offset_ms;
    if (adjusted <= 0) return 0;
    const unsigned int maximum = ~0u;
    if (adjusted > static_cast<long long>(maximum)) return maximum;
    return static_cast<unsigned int>(adjusted);
}

unsigned long long nowMicroseconds() {
    u64 seconds = 0;
    u64 nanoseconds = 0;
    sysGetCurrentTime(&seconds, &nanoseconds);
    return seconds * 1000000ull + nanoseconds / 1000ull;
}

unsigned int nowMilliseconds() {
    return static_cast<unsigned int>(nowMicroseconds() / 1000ull);
}

void writeStartupFailure(GemRuntime::InitStage stage, int error) {
    FILE* output = 0;
#if defined(_MSC_VER)
    if (fopen_s(&output, kStartupReportPath, "w") != 0) output = 0;
#else
    output = fopen(kStartupReportPath, "w");
#endif
    if (!output) return;
    fprintf(output, "movebeat_startup_report=1\nstage=%u\nerror=0x%08X\n",
            static_cast<unsigned int>(stage), static_cast<unsigned int>(error));
    fclose(output);
}

} // namespace

App::FrameInput::FrameInput()
    : gem_update_ok(false), pointer_start(false),
      calibration_hands_separated(false), selection_pressed(0),
      level_pressed(0), blade_modifier(false), timing_modifier(false),
      start_pressed(false), both_moves_tracked(false), both_moves_optical(false),
      hand_toggle(false),
      cancel_calibration(false), calibrated_start(false),
      move_chord(false), move_tap_slot(-1), raw_level_time_ms(0), level_time_ms(0) {}

App::App()
    : process_start_us_(nowMicroseconds()), demo_mode_(false),
      draw_background_(true), shelf_(0), shelf_pos_(0), next_song_remaining_ms_(0), last_playlist_ms_(0),
      imported_level_(false), selected_level_index_(0),
      selected_chart_index_(0), selected_chart_loaded_(false),
      chart_duration_ms_(0), audio_clock_(false),
      audio_start_error_(0), level_start_us_(0), run_state_(RUN_SELECT),
      hand_slots_swapped_(false), hand_assignment_established_(false),
      calibrating_(false), pose_calibrated_(false), left_rumble_frames_(0), right_rumble_frames_(0),
      left_rumble_power_(0), right_rumble_power_(0), gesture_hand_(-1),
      randomizer_spinning_(false), last_motion_ms_(0), last_tick_row_(0.0f),
      selection_changed_ms_(0), preview_active_(false), halo_flash_ms_(0),
      paused_(false), user_paused_(false), tracking_paused_(false), tracking_recalibrating_(false),
      resume_since_ms_(0),
      pause_started_us_(0), diagnostic_frame_(0),
      previous_diagnostic_phase_(""), first_frame_saved_(false),
      demo_frame_saved_(false), demo_scored_frame_saved_(false),
      demo_timed_frame_saved_(false),
      demo_cycles_(0), demo_loss_started_us_(0), demo_loss_finished_(false),
      menu_test_frame_(0), playlist_test_results_(0), menu_test_done_(false), menu_capture_path_(0), last_frame_active_us_(0),
      menu_step_total_us_(0) {}

int App::run() {
    int exit_code = 0;
    if (!initializeSystems(exit_code)) return exit_code;
    if (!selectInitialLevel(exit_code)) return exit_code;
    loadSettings();
    if (demo_mode_ && !scriptedMenuRun()) startDemo();

    diagnostic_.state = "running";
    diagnostic_.phase = run_state_ == RUN_AUDIO_ERROR ? "audio_error" :
        (demo_mode_ && !demo_options_.menu_test ? "demo_play" : "select");
    diagnostic_.level_count = static_cast<int>(level_choices_.size());
    diagnostic_.imported_level = imported_level_ ? 1 : 0;
    diagnostic_.level_id = imported_level_ ? selected_level_.source_key : "builtin_tutorial";
    diagnostic_.chart_difficulty = imported_level_ && !selected_level_.charts.empty()
        ? selected_level_.charts[selected_chart_index_].difficulty : "Easy";
    writeRunStatus(kRunStatusPath, diagnostic_);

    while (g_running) runFrame();

    shutdown();
    return 0;
}

bool App::initializeSystems(int& exit_code) {
    if (sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, systemEvent, 0) != 0) {
        printf("Could not register SYSUTIL callback\n");
        diagnostic_.state = "fatal";
        diagnostic_.phase = "sysutil_callback";
        diagnostic_.error_stage = 101;
        writeRunStatus(kRunStatusPath, diagnostic_);
        exit_code = 1;
        return false;
    }
    const RenderOptions render_options = readRenderOptions();
    draw_background_ = render_options.background;
    screen_.setEffectsEnabled(render_options.effects);
    if (!screen_.initialize(render_options.software ? RENDERER_FORCE_SOFTWARE
                                                    : RENDERER_PREFER_RSX)) {
        printf("RSX framebuffer initialization failed\n");
        diagnostic_.state = "fatal";
        diagnostic_.phase = "framebuffer";
        diagnostic_.error_stage = 102;
        writeRunStatus(kRunStatusPath, diagnostic_);
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        exit_code = 2;
        return false;
    }
    printf("MoveBeat PS3 0.1 (%s renderer)\n", screen_.rendererName());
    diagnostic_.renderer = screen_.rendererName();
    diagnostic_.effects_enabled = screen_.effectsEnabled() ? 1 : 0;
    diagnostic_.phase = "framebuffer_ready";
    writeRunStatus(kRunStatusPath, diagnostic_);

    demo_options_ = readEmulatorDemoOptions();
    demo_mode_ = demo_options_.enabled;
    diagnostic_.phase = "gem_initializing";
    writeRunStatus(kRunStatusPath, diagnostic_);
    gem_.setHandHuesEnabled(render_options.hand_colors);
    gem_.setCameraTuning(render_options.camera_exposure, render_options.camera_quality);
    gem_.setStateFilter(render_options.gem_filter);
    gem_.setHandHueValues(static_cast<unsigned int>(render_options.left_hue < 0 ? 0 : render_options.left_hue),
                          static_cast<unsigned int>(render_options.right_hue < 0 ? 0 : render_options.right_hue));
    diagnostic_.camera_exposure = render_options.camera_exposure;
    diagnostic_.camera_quality_pct = static_cast<int>(render_options.camera_quality * 100.0f + 0.5f);
    if (!demo_mode_ && !gem_.initialize()) {
        const GemRuntime::InitStage stage = gem_.lastStage();
        const int error = gem_.lastError();
        printf("Camera/libgem initialization failed at stage %u: %08X\n",
               static_cast<unsigned int>(stage), static_cast<unsigned int>(error));
        writeStartupFailure(stage, error);
        diagnostic_.state = "fatal";
        diagnostic_.phase = "gem_initialization";
        diagnostic_.error_stage = static_cast<unsigned int>(stage);
        diagnostic_.error_code = static_cast<unsigned int>(error);
        writeRunStatus(kRunStatusPath, diagnostic_);
        runStartupFailure(stage, error);
        gem_.shutdown();
        screen_.shutdown();
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        exit_code = 3;
        return false;
    }
    return true;
}

void App::runStartupFailure(GemRuntime::InitStage stage, int error) {
    // Keep the failure visible for about ten seconds unless the XMB exits.
    for (int frame = 0; frame < 600 && g_running; ++frame) {
        screen_.beginFrame(0xff300008u);
        drawStartupFailure(screen_, stage, error);
        if (frame == 0) screen_.savePpm(kFirstFramePath);
        screen_.present();
        sysUtilCheckCallback();
    }
}

bool App::selectInitialLevel(int& exit_code) {
    const unsigned long long scan_start_us = nowMicroseconds();
    level_choices_ = discoverLibrary(playlists_);
    diagnostic_.library_scan_ms = static_cast<unsigned int>(
        (nowMicroseconds() - scan_start_us) / 1000ull);
    diagnostic_.playlist_count = static_cast<unsigned int>(playlists_.size());
    selected_chart_ = tutorialChart();
    if (demo_mode_ && !demo_options_.level_id.empty()) {
        bool found = false;
        for (std::size_t index = 0; index < level_choices_.size(); ++index) {
            const std::string name = level_choices_[index].directory.substr(
                std::string(kLevelRoot).size() + 1);
            if (name == demo_options_.level_id) {
                selected_level_index_ = index;
                found = true;
                break;
            }
        }
        if (!found) {
            printf("Demo level ID not found: %s\n", demo_options_.level_id.c_str());
            diagnostic_.state = "fatal";
            diagnostic_.phase = "demo_level_missing";
            diagnostic_.error_stage = 104;
            diagnostic_.level_count = static_cast<int>(level_choices_.size());
            writeRunStatus(kRunStatusPath, diagnostic_);
            screen_.shutdown();
            sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
            exit_code = 4;
            return false;
        }
    }
    shelf_order_.clear();
    for (std::size_t index = 0; index < level_choices_.size(); ++index)
        shelf_order_.push_back(index);
    shelf_pos_ = selected_level_index_;
    if (!level_choices_.empty()) {
        // Fill the whole card window once at startup.
        updateLibraryCards(level_choices_, shelf_order_, shelf_pos_,
                           static_cast<unsigned int>(kLibraryCardWindow * 2 + 1));
        recordLibraryCardMemory(diagnostic_, level_choices_);
        level_directory_ = level_choices_[selected_level_index_].directory;
        selected_level_ = level_choices_[selected_level_index_].manifest;
    }
    if (!level_directory_.empty() &&
        demo_mode_ && demo_options_.chart_index < selected_level_.charts.size())
        selected_chart_index_ = demo_options_.chart_index;
    else if (demo_mode_ && demo_options_.chart_index != 0)
        printf("Demo chart index %u is unavailable; using chart 0\n",
               demo_options_.chart_index);
    if (!level_directory_.empty() &&
        movebeat::loadLevelChart(level_directory_, selected_level_,
                                 selected_chart_index_,
                                 selected_chart_, level_error_)) {
        imported_level_ = true;
        selected_chart_loaded_ = true;
        printf("Loaded level: %s (%s)\n", selected_level_.title.c_str(),
               selected_level_.charts[selected_chart_index_].difficulty.c_str());
    } else {
        printf("Using built-in tutorial: %s\n", level_error_.c_str());
    }
    // Match vertical note placement to the pitched spectator camera. The
    // upper row remains at its authored height; the middle/lower rows use this
    // slope to keep the center level and the lower row below the sightline.
    game_config_.row_pitch_per_depth =
        -(kProjectionEyeHeight + kProjectionCameraHeight) / kProjectionDepth;
    // No fail for now: tracking on hardware is still being tuned, and a
    // drained energy bar must not end the song.
    game_config_.energy_failure = false;
    reloadSelectedChart();
    chart_duration_ms_ = estimateChartDurationMs(selected_chart_);
    carousel_.reset(static_cast<int>(shelf_pos_));
    last_tick_row_ = carousel_.nearestRow();
    selection_changed_ms_ = nowMilliseconds();
    random_.seed(static_cast<std::uint32_t>(nowMicroseconds()));
    return true;
}

void App::loadSettings() {
    if (!movebeat::loadSettingsFile(kSettingsPath, settings_, settings_error_)) {
        printf("Settings read failed: %s; using zero offset\n",
               settings_error_.c_str());
        settings_ = movebeat::Settings();
    }
    audio_path_ = imported_level_
        ? level_directory_ + "/" + selected_level_.audio
        : std::string();
    level_start_us_ = nowMicroseconds();
    hand_slots_swapped_ = settings_.invert_hands;
}

void App::startDemo() {
    // Explicit, local-only RPCS3 preview. Never infer this from a missing Eye:
    // on hardware, a camera failure must remain visible to the player.
    run_state_ = RUN_PLAY;
    game_.reset();
    cut_feedback_.clear();
    level_start_us_ = nowMicroseconds();
    if (imported_level_) startLevelAudio("Demo audio start failed", false);
    printf("Emulator demo enabled; Move poses are synthetic\n");
}

void App::shutdown() {
    diagnostic_.state = "clean_exit";
    diagnostic_.phase = "shutdown";
    diagnostic_.frame = diagnostic_frame_;
    diagnostic_.uptime_ms = static_cast<unsigned int>(
        (nowMicroseconds() - process_start_us_) / 1000ull);
    storeFrameTiming(true);
    ++diagnostic_.heartbeat;
    writeRunStatus(kRunStatusPath, diagnostic_);
    audio_.close();
    gem_.shutdown();
    screen_.shutdown();
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
}

void App::runFrame() {
    const unsigned long long frame_work_start_us = nowMicroseconds();
    synchronizePause();
    checkAudioStream();
    FrameInput input;
    readInput(input);
    handleHandToggle(input);
    handleShelfSwitch(input);
    handleCalibrationCancel(input);
    handleLibraryNavigation(input);
    handleLibrarySettings(input);
    handleBackButtons(input);
    updateLibraryMotion(input);
    if ((run_state_ == RUN_SELECT || run_state_ == RUN_RESULTS) &&
        !shelf_order_.empty()) {
        // One card read per frame at most keeps the window around the
        // selection filled while scrolling without a long frame. The
        // playlist summary lists the last seven songs: center on them.
        const std::size_t center =
            run_state_ == RUN_RESULTS && playlist_run_.complete() && shelf_pos_ >= 3
                ? shelf_pos_ - 3 : shelf_pos_;
        if (updateLibraryCards(level_choices_, shelf_order_, center, 1u))
            recordLibraryCardMemory(diagnostic_, level_choices_);
    }
    handlePauseMenu(input);
    updateTrackingPause(input);
    updateCalibration(input);
    handleStart(input);
    updatePlaylistRun();
    updateGameplay(input);
    renderFrame(input, frame_work_start_us);
    if (demo_mode_ && demo_options_.menu_test) recordMenuTest(input);
    publishDiagnostics(input);
}

void App::failAudio(const char* message) {
    diagnostic_.audio_error_code = static_cast<unsigned int>(audio_start_error_);
    printf("%s: %08X\n", message, audio_start_error_);
    audio_.shutdown();
    audio_clock_ = false;
    run_state_ = RUN_AUDIO_ERROR;
    tracking_paused_ = false;
    tracking_recalibrating_ = false;
}

void App::synchronizePause() {
    const bool requested = g_pause_requested || tracking_paused_ || user_paused_;
    if (requested == paused_) return;
    const unsigned long long now_us = nowMicroseconds();
    paused_ = requested;
    if (paused_) {
        pause_started_us_ = now_us;
        if (audio_clock_ && !audio_.pause()) {
            audio_start_error_ = audio_.lastError();
            failAudio("Audio pause failed");
        }
        left_rumble_frames_ = 0;
        right_rumble_frames_ = 0;
    } else {
        level_start_us_ += now_us - pause_started_us_;
        if (audio_clock_ && !audio_.resume()) {
            audio_start_error_ = audio_.lastError();
            failAudio("Audio resume failed");
        }
    }
}

void App::checkAudioStream() {
    if (run_state_ == RUN_PLAY && audio_clock_ && audio_.lastError() != 0) {
        audio_start_error_ = audio_.lastError();
        failAudio("Audio stream failed");
    }
}

void App::readMenuTestInput(FrameInput& input) {
    // Scripted library run: every track one press at a time, then a held
    // button (auto-repeat), then difficulty changes. 60 frames per second.
    input.moves.left_connected = true;
    input.moves.right_connected = true;
    const unsigned int count = static_cast<unsigned int>(level_choices_.size());
    const unsigned int presses = count * 2u + 1u;
    const unsigned int frame = menu_test_frame_++;
    const unsigned int single_end = presses * 8u;
    const unsigned int hold_end = single_end + 240u;
    const unsigned int pause_end = hold_end + 10u;
    const unsigned int difficulty_end = pause_end + 12u * 8u;
    // Then the Move gestures on the same track (bench tracks have three
    // difficulties): a 36 cm sideways stroke (two difficulty steps), a
    // 30 cm upward flick with T held (1.5 m/s), time to coast, settle and
    // start the preview, and the Move button randomizer.
    const unsigned int swipe_start = difficulty_end + 10u;
    const unsigned int swipe_end = swipe_start + 20u;
    const unsigned int flick_start = swipe_end + 30u;
    const unsigned int flick_end = flick_start + 12u;
    const unsigned int coast_end = flick_end + 150u;
    const unsigned int random_end = coast_end + 2u;
    const unsigned int final_end = random_end + 150u;
    unsigned short buttons = 0;
    float hand_x = 0.0f;
    float hand_y = 0.0f;
    if (frame < single_end) {
        if (frame % 8u < 2u) buttons = GEM_BUTTON_CROSS;
    } else if (frame < hold_end) {
        buttons = GEM_BUTTON_CROSS;
    } else if (frame >= pause_end && frame < difficulty_end) {
        if ((frame - pause_end) % 8u < 2u) buttons = GEM_BUTTON_CIRCLE;
    } else if (frame >= swipe_start && frame < swipe_end) {
        input.moves.right_trigger = 255;
        hand_x = (frame - swipe_start) * 0.018f;
    } else if (frame >= flick_start && frame < flick_end) {
        input.moves.right_trigger = 255;
        hand_y = (frame - flick_start) * 0.025f;
    } else if (frame >= coast_end && frame < random_end) {
        buttons = GEM_BUTTON_MOVE;
    }
    // Two library frames: resting with the preview after the flick, and
    // right after the sideways stroke while the halo flares.
    if (frame == coast_end - 5u) menu_capture_path_ = kDemoFramePath;
    if (frame == swipe_end + 2u) menu_capture_path_ = kDemoTimedFramePath;
    // Last, both Move buttons with the controllers held crossed over: GEM
    // slot 0 on the player's right. Nothing is calibrated yet, so the chord
    // calibrates (one still second) and must make slot 1 the left hand.
    const unsigned int chord_end = final_end + 6u;
    const unsigned int chord_timeout = chord_end + 600u;
    if (frame >= final_end) {
        scriptCalibrationPose(input, true);
        if (frame < chord_end) {
            input.moves.left_buttons = GEM_BUTTON_MOVE;
            input.moves.right_buttons = GEM_BUTTON_MOVE;
        }
        if (calibrating_) scriptCalibrationPose(input, true);
        // The sideways pose of the guided calibration, for the record.
        if (calibrating_ && !menu_capture_path_ && !demo_scored_frame_saved_ &&
            body_calibration_.step() == movebeat::BodyCalibration::STEP_SIDES &&
            body_calibration_.handState(0) == movebeat::BodyCalibration::HAND_MEASURING) {
            menu_capture_path_ = kDemoScoredFramePath;
            demo_scored_frame_saved_ = true;
        }
        if (run_state_ == RUN_PLAY || frame >= chord_timeout) menu_test_done_ = true;
        return;
    }
    input.moves.right = movebeat::SaberPose(
        movebeat::Vec3(hand_x, hand_y, 0.0f),
        movebeat::Vec3(hand_x, hand_y + 1.0f, 0.0f), true);
    input.moves.right_buttons = buttons;
}

void App::readPlaylistTestInput(FrameInput& input) {
    // Scripted playlist run (play itself uses the scripted cuts): Select
    // shows the first playlist, both Move buttons start it (the first
    // start calibrates), the first countdown runs out, the second results
    // screen is skipped with Start, and the summary ends the test.
    input.moves.left_connected = true;
    input.moves.right_connected = true;
    // The forward pose: its tips meet at the origin and the sideways pose
    // spans the reference, so calibration keeps the scripted cuts as they are.
    scriptCalibrationPose(input, false);
    const unsigned int frame = menu_test_frame_++;
    // One Select press (two frames, then eight released) per shelf step.
    const unsigned int selects_end = 30u + 10u * demo_options_.playlist_shelf;
    if (run_state_ == RUN_SELECT) {
        if (frame >= 30u && frame < selects_end && (frame - 30u) % 10u < 2u)
            input.moves.right_buttons = kButtonSelect;
        if (frame == selects_end + 30u) menu_capture_path_ = kDemoFramePath;
        if (frame >= selects_end + 50u && frame < selects_end + 56u) {
            input.moves.left_buttons = GEM_BUTTON_MOVE;
            input.moves.right_buttons = GEM_BUTTON_MOVE;
        }
        if (calibrating_) scriptCalibrationPose(input, false);
    }
    if (run_state_ != RUN_RESULTS) {
        playlist_test_results_ = 0;
    } else {
        const unsigned int results_frame = playlist_test_results_++;
        const std::size_t played = playlist_run_.results().size();
        if (played == 1u && results_frame == 150u) menu_capture_path_ = kDemoTimedFramePath;
        if (played == 2u && results_frame >= 60u && results_frame < 62u)
            input.moves.right_buttons = kButtonStart;
        if (playlist_run_.complete() && results_frame == 30u)
            menu_capture_path_ = kDemoScoredFramePath;
        if (playlist_run_.complete() && results_frame >= 40u) menu_test_done_ = true;
    }
    // Safety net: a stuck run must still end the test.
    if (frame >= 60u * 300u) menu_test_done_ = true;
}

void App::recordMenuTest(const FrameInput& input) {
    // The chord start (chart parse, calibration, level) is not menu cost.
    if (menu_test_done_ || diagnostic_.menu_chord_starts) return;
    if (input.level_pressed || input.selection_pressed) {
        ++diagnostic_.menu_steps;
        menu_step_total_us_ += last_frame_active_us_;
        diagnostic_.menu_step_avg_us = static_cast<unsigned int>(
            menu_step_total_us_ / diagnostic_.menu_steps);
        diagnostic_.menu_step_max_us = std::max(
            diagnostic_.menu_step_max_us, last_frame_active_us_);
    } else if (menu_test_frame_ > 30u) {
        diagnostic_.menu_idle_max_us = std::max(
            diagnostic_.menu_idle_max_us, last_frame_active_us_);
    }
}

void App::readDemoInput(FrameInput& input) {
    if (demo_options_.menu_test) {
        readMenuTestInput(input);
        return;
    }
    if (demo_options_.pause_test) {
        // Frames of the scripted song: pause, restart, pause, leave.
        const unsigned int frame = menu_test_frame_++;
        unsigned short buttons = 0;
        if ((frame >= 180u && frame < 182u) || (frame >= 420u && frame < 422u))
            buttons = kButtonStart;
        else if (frame >= 240u && frame < 242u)
            buttons = kButtonCircle;
        else if (frame >= 460u && frame < 462u)
            buttons = kButtonTriangle;
        if (frame == 210u) menu_capture_path_ = kDemoTimedFramePath;
        if (frame > 462u && run_state_ == RUN_SELECT) menu_test_done_ = true;
        if (run_state_ != RUN_PLAY || user_paused_) {
            input.moves = MoveFrame();
            input.moves.left_connected = input.moves.right_connected = true;
        } else {
            input.moves = scriptedDemoFrame(
                game_, adjustedTime(audio_clock_ ? audio_.timeMs() : 0u,
                                    settings_.timing_offset_ms),
                demo_options_.automatic_cuts);
        }
        input.moves.right_buttons = buttons;
        return;
    }
    if (demo_options_.playlist_test) {
        if (run_state_ != RUN_PLAY) {
            readPlaylistTestInput(input);
            return;
        }
        // Each results screen counts its frames from zero.
        playlist_test_results_ = 0;
    }
    const unsigned int elapsed_ms = unpausedElapsedMs(
        nowMicroseconds(), level_start_us_, pause_started_us_, paused_);
    input.moves = scriptedDemoFrame(
        game_, adjustedTime(audio_clock_ ? audio_.timeMs() : elapsed_ms,
                            settings_.timing_offset_ms),
        demo_options_.automatic_cuts);
    if (!demo_options_.tracking_loss_test || demo_loss_finished_) return;
    if (!demo_loss_started_us_ && elapsed_ms >= 1200u)
        demo_loss_started_us_ = nowMicroseconds();
    if (!demo_loss_started_us_) return;
    const unsigned int loss_elapsed_ms = static_cast<unsigned int>(
        (nowMicroseconds() - demo_loss_started_us_) / 1000ull);
    if (loss_elapsed_ms < 1250u) {
        input.moves.left.tracked = false;
    } else if (tracking_paused_ || loss_elapsed_ms < 2600u) {
        input.moves.left = movebeat::SaberPose(
            movebeat::Vec3(-0.35f, -0.7f, -0.3f),
            movebeat::Vec3(-0.35f, 0.0f, 0.0f), true);
        input.moves.right = movebeat::SaberPose(
            movebeat::Vec3(0.35f, -0.7f, -0.3f),
            movebeat::Vec3(0.35f, 0.0f, 0.0f), true);
        if (loss_elapsed_ms >= 1300u && loss_elapsed_ms < 1400u)
            input.moves.left_buttons = kButtonStart;
    } else if (diagnostic_.tracking_recovery_count > 0u) {
        demo_loss_finished_ = true;
        game_.invalidateInput();
    }
}

void App::readInput(FrameInput& input) {
    if (demo_mode_) {
        readDemoInput(input);
    } else {
        input.gem_update_ok = gem_.update(
            input.moves, static_cast<float>(settings_.blade_length_cm) * 0.01f,
            run_state_ == RUN_SELECT && !calibrating_ && !paused_,
            nowMilliseconds());
    }
    const MoveFrame& moves = input.moves;
    input.optical = moves;
    // Seen by the camera, fully or partly; not held by the sensors alone.
    if (moves.held[0]) input.optical.left.tracked = false;
    if (moves.held[1]) input.optical.right.tracked = false;
    input.both_moves_optical = input.optical.left.tracked && input.optical.right.tracked;
    input.buttons = menu_buttons_.update(
        moves, demo_mode_ ? 3u : gem_.lastButtonReadyMask());
    const MenuButtonSample& buttons = input.buttons;
    // Library selection uses T-held gestures (updateLibraryMotion); the
    // sphere-position pointer of the old three-card list is gone.
    const unsigned short pointer_navigation = 0;
    unsigned short repeated_navigation = 0;
    if (run_state_ == RUN_SELECT && !calibrating_ && !paused_) {
        repeated_navigation = menu_navigation_repeat_.update(
            buttons.held, buttons.pressed, nowMilliseconds());
    } else {
        menu_navigation_repeat_.reset();
    }
    input.calibration_hands_separated =
        handsSeparatedForCalibration(input.optical.left, input.optical.right);
    input.selection_pressed = static_cast<unsigned short>(
        buttons.pressed & (kButtonCircle | kButtonSquare));
    input.level_pressed = static_cast<unsigned short>(
        (buttons.pressed | repeated_navigation | pointer_navigation) &
        (kButtonTriangle | kButtonCross));
    const bool left_trigger_down = buttons.left_trigger >= 128u;
    const bool right_trigger_down = buttons.right_trigger >= 128u;
    input.blade_modifier = left_trigger_down && right_trigger_down;
    input.timing_modifier = (left_trigger_down || right_trigger_down) &&
                            !input.blade_modifier;
    input.start_pressed = (buttons.pressed & kButtonStart) != 0 ||
                          input.pointer_start;
    input.both_moves_tracked = moves.left.tracked && moves.right.tracked;
    const MoveChordEvents chord = move_chord_.update(
        (buttons.left_held & GEM_BUTTON_MOVE) != 0,
        (buttons.right_held & GEM_BUTTON_MOVE) != 0, nowMilliseconds());
    input.move_chord = chord.chord;
    input.move_tap_slot = chord.tap_slot;
    // A reconnected Move may be held differently: measure the origin again.
    if (!moves.left_connected || !moves.right_connected) pose_calibrated_ = false;
}

void App::saveSettings() {
    if (!movebeat::saveSettingsFile(kSettingsPath, settings_, settings_error_))
        printf("Settings write failed: %s\n", settings_error_.c_str());
}

void App::handleHandToggle(FrameInput& input) {
    // Both T held + Select: Select alone steps through the shelves.
    input.hand_toggle = run_state_ == RUN_SELECT && !calibrating_ &&
        !paused_ && input.blade_modifier &&
        (input.buttons.pressed & kButtonSelect) != 0;
    if (!input.hand_toggle) return;
    settings_.invert_hands = !settings_.invert_hands;
    hand_slots_swapped_ = !hand_slots_swapped_;
    if (hand_assignment_established_)
        gem_.setHandSlotsSwapped(hand_slots_swapped_);
    saveSettings();
    printf("Manual hand inversion: %s\n",
           settings_.invert_hands ? "on" : "off");
}

void App::handleCalibrationCancel(FrameInput& input) {
    input.cancel_calibration = calibrating_ &&
        (input.level_pressed & kButtonTriangle) != 0;
    if (!input.cancel_calibration) return;
    calibrating_ = false;
    body_calibration_.reset();
    printf("Pose calibration cancelled\n");
}

void App::reloadSelectedChart() {
    movebeat::GameConfig chart_config = game_config_;
    if (imported_level_ && selected_chart_index_ < selected_level_.charts.size()) {
        const movebeat::LevelChart& chart_metadata =
            selected_level_.charts[selected_chart_index_];
        if (chart_metadata.note_jump_speed > 0.0f &&
            !movebeat::configureNoteJump(
                chart_config, selected_level_.bpm,
                chart_metadata.note_jump_speed,
                chart_metadata.start_beat_offset)) {
            printf("Unsafe note jump metadata for %s/%s; using default\n",
                   chart_metadata.characteristic.c_str(),
                   chart_metadata.difficulty.c_str());
        }
    }
    game_.setConfig(chart_config);
    game_.load(selected_chart_);
}

void App::handleLibraryNavigation(const FrameInput& input) {
    if (run_state_ != RUN_SELECT || calibrating_ || input.cancel_calibration ||
        input.hand_toggle || !input.level_pressed || shelf_order_.size() <= 1)
        return;
    // The carousel animates the step; updateLibraryMotion selects the
    // track it lands on.
    if (input.level_pressed & kButtonCross) carousel_.step(1);
    else if (input.level_pressed & kButtonTriangle) carousel_.step(-1);
}

void App::selectLevel(std::size_t candidate) {
    // Selection only: the chart is parsed when the level starts
    // (ensureSelectedChart), so scrolling never waits for a chart.
    selected_level_index_ = candidate;
    selected_chart_index_ = 0;
    level_directory_ = level_choices_[candidate].directory;
    selected_level_ = level_choices_[candidate].manifest;
    selected_chart_loaded_ = false;
    imported_level_ = true;
    audio_start_error_ = 0;
    diagnostic_.audio_error_code = 0;
    audio_path_ = level_directory_ + "/" + selected_level_.audio;
    printf("Selected level: %s (%s)\n", selected_level_.title.c_str(),
           selected_level_.charts[0].difficulty.c_str());
}

void App::selectShelfPosition(std::size_t position) {
    if (position >= shelf_order_.size()) return;
    shelf_pos_ = position;
    selectLevel(shelf_order_[position]);
    if (shelf_ == 0) return;
    const PlaylistChoice& playlist = playlists_[shelf_ - 1];
    const movebeat::PlaylistSong& song =
        playlist.manifest.songs[playlist.song_indices[position]];
    if (!song.difficulty.empty())
        selected_chart_index_ = choosePlaylistChart(song, selected_level_, -1);
}

void App::handleShelfSwitch(const FrameInput& input) {
    if (run_state_ != RUN_SELECT || calibrating_ || paused_ || input.hand_toggle ||
        input.blade_modifier || (input.buttons.pressed & kButtonSelect) == 0 ||
        playlists_.empty())
        return;
    showShelf((shelf_ + 1) % (playlists_.size() + 1));
    audio_.playEffect(SFX_DIFFICULTY, 0.6f, 0.0f);
    pulseRumble(0, 3, 140);
    pulseRumble(1, 3, 140);
}

void App::showShelf(std::size_t shelf) {
    if (shelf > playlists_.size()) return;
    if (shelf_ > 0) playlists_[shelf_ - 1].card.clear();
    shelf_ = shelf;
    shelf_order_.clear();
    if (shelf_ == 0) {
        for (std::size_t index = 0; index < level_choices_.size(); ++index)
            shelf_order_.push_back(index);
    } else {
        PlaylistChoice& playlist = playlists_[shelf_ - 1];
        shelf_order_ = playlist.tracks;
        if (!playlist.card.ready() && !playlist.card_missing &&
            !playlist.card.load(playlist.directory))
            playlist.card_missing = true;
    }
    // Stay on the current track when the new shelf has it.
    std::size_t position = 0;
    for (std::size_t place = 0; place < shelf_order_.size(); ++place)
        if (shelf_order_[place] == selected_level_index_) { position = place; break; }
    stopPreview(120);
    carousel_.reset(static_cast<int>(position));
    last_tick_row_ = carousel_.nearestRow();
    randomizer_spinning_ = false;
    selection_changed_ms_ = nowMilliseconds();
    if (!shelf_order_.empty()) selectShelfPosition(position);
    diagnostic_.shelf = static_cast<unsigned int>(shelf_);
    printf("Shelf %u: %s (%u tracks)\n", static_cast<unsigned int>(shelf_),
           shelf_ ? playlists_[shelf_ - 1].manifest.title.c_str() : "all tracks",
           static_cast<unsigned int>(shelf_order_.size()));
}

bool App::ensureSelectedChart() {
    if (!imported_level_ || selected_chart_loaded_) return true;
    movebeat::Chart chart;
    if (!movebeat::loadLevelChart(level_directory_, selected_level_,
                                  selected_chart_index_, chart, level_error_)) {
        printf("Chart load failed: %s\n", level_error_.c_str());
        if (selected_level_index_ < level_choices_.size())
            level_choices_[selected_level_index_].chart_broken = true;
        return false;
    }
    selected_chart_ = chart;
    chart_duration_ms_ = estimateChartDurationMs(selected_chart_);
    reloadSelectedChart();
    selected_chart_loaded_ = true;
    return true;
}

void App::handleLibrarySettings(const FrameInput& input) {
    if (run_state_ != RUN_SELECT || calibrating_ || input.cancel_calibration ||
        input.hand_toggle || !input.selection_pressed)
        return;
    if (input.blade_modifier) {
        if (input.selection_pressed & kButtonCircle)
            settings_.blade_length_cm += 5;
        else if (input.selection_pressed & kButtonSquare)
            settings_.blade_length_cm -= 5;
        settings_.blade_length_cm = clamp(
            settings_.blade_length_cm,
            movebeat::Settings::MIN_BLADE_LENGTH_CM,
            movebeat::Settings::MAX_BLADE_LENGTH_CM);
        saveSettings();
        printf("Blade length: %d cm\n", settings_.blade_length_cm);
    } else if (input.timing_modifier) {
        if (input.selection_pressed & kButtonCircle)
            settings_.timing_offset_ms += 10;
        else if (input.selection_pressed & kButtonSquare)
            settings_.timing_offset_ms -= 10;
        settings_.timing_offset_ms = clamp(settings_.timing_offset_ms, -500, 500);
        saveSettings();
        printf("Timing offset: %+d ms\n", settings_.timing_offset_ms);
    } else if (imported_level_ && !selected_level_.charts.empty()) {
        changeDifficulty((input.selection_pressed & kButtonCircle) ? 1 : -1,
                         true, 1);
    }
}

void App::changeDifficulty(int step, bool wrap, int hand) {
    const std::size_t count = selected_level_.charts.size();
    if (!imported_level_ || count == 0 || step == 0) return;
    std::size_t index = selected_chart_index_;
    if (wrap) {
        index = step > 0 ? (index + 1) % count : (index == 0 ? count - 1 : index - 1);
    } else if (step > 0 && index + 1 < count) {
        ++index;
    } else if (step < 0 && index > 0) {
        --index;
    }
    const float pan = hand == 0 ? -0.4f : (hand == 1 ? 0.4f : 0.0f);
    if (index == selected_chart_index_) {
        // Already the easiest or hardest: a dull tick, no halo flare.
        audio_.playEffect(SFX_TICK, 0.25f, pan);
        return;
    }
    selected_chart_index_ = index;
    selected_chart_loaded_ = false;
    halo_flash_ms_ = nowMilliseconds();
    if (!halo_flash_ms_) halo_flash_ms_ = 1;
    audio_.playEffect(SFX_DIFFICULTY, 0.7f, pan);
    pulseRumble(hand < 0 ? 1 : hand, 3, 140);
    ++diagnostic_.menu_difficulty_changes;
    printf("Selected chart: %s/%s\n",
           selected_level_.charts[selected_chart_index_].characteristic.c_str(),
           selected_level_.charts[selected_chart_index_].difficulty.c_str());
}

void App::pulseRumble(int hand, int frames, int power) {
    if (hand < 0 || hand > 1) return;
    int& slot_frames = hand == 0 ? left_rumble_frames_ : right_rumble_frames_;
    int& slot_power = hand == 0 ? left_rumble_power_ : right_rumble_power_;
    // A stronger pulse takes over; a weaker one never cuts a strong one short.
    if (slot_frames <= 0 || power >= slot_power) slot_power = power;
    if (frames > slot_frames) slot_frames = frames;
}

void App::applyRumble() {
    if (!demo_mode_) {
        gem_.setRumble(physicalSlotForHand(0u, hand_slots_swapped_),
                       static_cast<u8>(left_rumble_frames_ > 0 ? left_rumble_power_ : 0));
        gem_.setRumble(physicalSlotForHand(1u, hand_slots_swapped_),
                       static_cast<u8>(right_rumble_frames_ > 0 ? right_rumble_power_ : 0));
    }
    if (left_rumble_frames_ > 0 && --left_rumble_frames_ == 0) left_rumble_power_ = 0;
    if (right_rumble_frames_ > 0 && --right_rumble_frames_ == 0) right_rumble_power_ = 0;
}

void App::stopPreview(unsigned int fade_ms) {
    if (preview_active_) audio_.fadeOut(fade_ms);
    preview_active_ = false;
}

void App::updateLibraryMotion(const FrameInput& input) {
    const unsigned int now_ms = nowMilliseconds();
    const float seconds = last_motion_ms_
        ? (now_ms - last_motion_ms_) / 1000.0f : 0.0f;
    last_motion_ms_ = now_ms;
    const std::size_t count = shelf_order_.size();
    if (run_state_ != RUN_SELECT || count == 0) {
        gesture_.cancel();
        gesture_hand_ = -1;
        return;
    }
    const MenuButtonSample& buttons = input.buttons;
    if (!calibrating_ && !paused_) {
        // Gestures belong to the Move whose T is held alone; holding both T
        // is the blade-length chord, not a gesture.
        const bool left_t = buttons.left_trigger >= 128u;
        const bool right_t = buttons.right_trigger >= 128u;
        if (gesture_hand_ < 0 && left_t != right_t) {
            gesture_hand_ = right_t ? 1 : 0;
            ++diagnostic_.menu_t_presses;
        }
        const bool held = gesture_hand_ >= 0 && !(left_t && right_t) &&
                          (gesture_hand_ == 1 ? right_t : left_t);
        const movebeat::SaberPose& pose =
            gesture_hand_ == 0 ? input.moves.left : input.moves.right;
        // Follow a point along the blade, so arm motion and wrist turns
        // (pointing the Move up, down or sideways) both count.
        float point_x = 0.0f, point_y = 0.0f;
        const bool pointing = gesturePoint(pose, point_x, point_y);
        const MenuGestureEvents events = gesture_.update(
            held, pointing, point_x, point_y, now_ms);
        if (held && !pointing) ++diagnostic_.menu_t_untracked_frames;
        if (events.drag_started) ++diagnostic_.menu_gesture_drags;
        if (events.drag_started) {
            carousel_.beginDrag();
            randomizer_spinning_ = false;
        }
        if (events.drag_rows != 0.0f) carousel_.drag(events.drag_rows);
        if (events.released) {
            carousel_.release(events.flick_rows_per_second);
            if (std::fabs(events.flick_rows_per_second) > CarouselScroll::settleSpeed())
                ++diagnostic_.menu_flicks;
        }
        if (events.difficulty_step)
            changeDifficulty(events.difficulty_step, false, gesture_hand_);
        if (!held) gesture_hand_ = -1;
        // A Move button tap: spin to a random other track, one extra lap
        // for show. (Both Move buttons together start the level.)
        if (input.move_tap_slot >= 0 && count > 1 && !carousel_.dragging()) {
            const std::size_t pick = random_.pickOther(shelf_pos_, count);
            const std::size_t ahead = (pick + count - shelf_pos_) % count;
            carousel_.step(static_cast<int>(ahead + (count <= 40 ? count : 0)));
            randomizer_spinning_ = true;
            ++diagnostic_.menu_randomizer_spins;
            printf("Randomizer: spinning to %u\n", static_cast<unsigned int>(pick));
        }
    } else if (gesture_hand_ >= 0) {
        gesture_.cancel();
        gesture_hand_ = -1;
        carousel_.release(0.0f);
    }
    carousel_.update(seconds);

    // A click and a tap in the hand for every row that passes the center.
    const float row = carousel_.nearestRow();
    if (row != last_tick_row_) {
        const int hand = gesture_hand_ >= 0 ? gesture_hand_ : 1;
        audio_.playEffect(SFX_TICK, randomizer_spinning_ ? 0.35f : 0.5f,
                          hand == 0 ? -0.3f : 0.3f);
        pulseRumble(hand, 1, randomizer_spinning_ ? 50 : 80);
        last_tick_row_ = row;
        ++diagnostic_.menu_ticks;
    }
    const std::size_t selected = carousel_.selected(count);
    if (selected != shelf_pos_) {
        stopPreview(120);
        selectShelfPosition(selected);
        selection_changed_ms_ = now_ms;
    }
    if (randomizer_spinning_ && carousel_.resting()) {
        randomizer_spinning_ = false;
        audio_.playEffect(SFX_LAND, 0.7f, 0.0f);
        pulseRumble(0, 6, 180);
        pulseRumble(1, 6, 180);
    }
    // Preview the song once the list has rested on it for a moment.
    if (!preview_active_ && !calibrating_ && !paused_ && imported_level_ &&
        carousel_.resting() &&
        static_cast<int>(now_ms - selection_changed_ms_) >= 650) {
        if (audio_.startPreview(audio_path_, 0.35f, 60000u, 450u)) {
            preview_active_ = true;
            ++diagnostic_.menu_preview_starts;
        } else {
            // Do not retry every frame for a track that cannot play.
            selection_changed_ms_ = now_ms + 60000u;
        }
    }
}

void App::handleBackButtons(const FrameInput& input) {
    if (run_state_ == RUN_AUDIO_ERROR && (input.level_pressed & kButtonTriangle)) {
        run_state_ = RUN_SELECT;
        audio_start_error_ = 0;
        diagnostic_.audio_error_code = 0;
    }
    if (run_state_ == RUN_RESULTS && (input.level_pressed & kButtonTriangle)) {
        run_state_ = RUN_SELECT;
        if (playlist_run_.active()) printf("Playlist run stopped\n");
        playlist_run_.stop();
        next_song_remaining_ms_ = 0;
        carousel_.reset(static_cast<int>(shelf_pos_));
        last_tick_row_ = carousel_.nearestRow();
        game_.reset();
        cut_feedback_.clear();
        selection_changed_ms_ = nowMilliseconds();
        printf("Returned to level selection\n");
    }
}

void App::assignHands(const MoveFrame& moves) {
    setHandAssignment(shouldSwapHandSlots(moves.left, moves.right));
}

void App::setHandAssignment(bool slots_swapped) {
    hand_slots_swapped_ = slots_swapped != settings_.invert_hands;
    hand_assignment_established_ = true;
    gem_.setHandSlotsSwapped(hand_slots_swapped_);
}

void App::beginBodyCalibration() {
    stopPreview(250);
    audio_.playEffect(SFX_LAND, 0.6f, 0.0f);
    body_calibration_.reset();
    calibrating_ = true;
    printf("Calibration: arms forward, then to the sides, then down\n");
}

void App::scriptCalibrationPose(FrameInput& input, bool crossed) const {
    // Confirms the pose with each Move button still waiting for it: two
    // frames pressed out of every ten, so each press is a fresh edge.
    if (calibrating_ && (menu_test_frame_ % 10u) < 2u) {
        if (body_calibration_.handState(0) == movebeat::BodyCalibration::HAND_WAITING)
            input.moves.left_buttons = GEM_BUTTON_MOVE;
        if (body_calibration_.handState(1) == movebeat::BodyCalibration::HAND_WAITING)
            input.moves.right_buttons = GEM_BUTTON_MOVE;
    }
    const float slot0_side = crossed ? 1.0f : -1.0f;
    for (int slot = 0; slot < 2; ++slot) {
        const float x = slot == 0 ? slot0_side : -slot0_side;
        movebeat::SaberPose pose;
        switch (body_calibration_.step()) {
        case movebeat::BodyCalibration::STEP_SIDES:
            pose = movebeat::SaberPose(movebeat::Vec3(x * 0.8f, 0.0f, 0.6f),
                                       movebeat::Vec3(x * 1.3f, 0.0f, 0.6f), true);
            break;
        case movebeat::BodyCalibration::STEP_DOWN:
            pose = movebeat::SaberPose(movebeat::Vec3(x * 0.25f, -0.6f, 0.6f),
                                       movebeat::Vec3(x * 0.25f, -1.2f, 0.6f), true);
            break;
        default:
            // Arms forward in play space (+z): handles 45 cm behind the
            // origin, so the frame comes out as the identity and the
            // scripted cuts land where they would without calibration.
            pose = movebeat::SaberPose(movebeat::Vec3(x * 0.2f, 0.0f, -0.45f),
                                       movebeat::Vec3(x * 0.2f, 0.0f, 0.15f), true);
            break;
        }
        if (slot == 0) input.moves.left = pose;
        else input.moves.right = pose;
    }
}

void App::updateTrackingPause(FrameInput& input) {
    if (run_state_ == RUN_PLAY &&
        (!demo_mode_ || demo_options_.tracking_loss_test) && !paused_ &&
        tracking_gate_.observe(nowMilliseconds(), input.both_moves_tracked)) {
        tracking_paused_ = true;
        tracking_recalibrating_ = false;
        resume_since_ms_ = 0;
        tracking_calibration_.reset();
        game_.invalidateInput();
        synchronizePause();
        ++diagnostic_.tracking_pause_count;
        // What libgem said when the level paused, for hardware reports.
        diagnostic_.tracking_pause_slot0_result = gem_.lastResult(0);
        diagnostic_.tracking_pause_slot1_result = gem_.lastResult(1);
        diagnostic_.tracking_pause_slot0_flags = gem_.lastTrackingFlags(0);
        diagnostic_.tracking_pause_slot1_flags = gem_.lastTrackingFlags(1);
        diagnostic_.tracking_pause_audio_ms = audio_clock_ ? audio_.timeMs() : 0u;
        diagnostic_.tracking_pause_score = game_.score().score;
        diagnostic_.tracking_pause_hits = game_.score().hits;
        diagnostic_.tracking_pause_misses = game_.score().misses;
        printf("Tracking lost; audio and chart paused\n");
    }
    if (run_state_ != RUN_PLAY || !tracking_paused_) return;
    if ((input.level_pressed & kButtonTriangle) && !g_pause_requested) {
        quitLevel(true);
        return;
    }
    // Pointed at the camera, a Move button press recalibrates that sphere
    // (and its orientation reference).
    if (!demo_mode_ && !g_pause_requested) {
        const unsigned short pressed[2] = {input.buttons.left_pressed,
                                           input.buttons.right_pressed};
        for (unsigned int slot = 0; slot < 2; ++slot)
            if ((pressed[slot] & GEM_BUTTON_MOVE) && gem_.calibrateSlot(slot))
                printf("Tracking pause: sphere %u recalibrating\n", slot);
    }
    if (!input.both_moves_optical || g_pause_requested) {
        tracking_calibration_.reset();
        tracking_recalibrating_ = false;
        resume_since_ms_ = 0;
        return;
    }
    // Both spheres back in view for half a second: play on where the
    // player is; Start instead re-centers the play space first.
    const unsigned int now_ms = nowMilliseconds();
    if (!tracking_recalibrating_) {
        if (!resume_since_ms_) resume_since_ms_ = now_ms ? now_ms : 1u;
        if (now_ms - resume_since_ms_ >= 500u) {
            resumeAfterTrackingLoss();
            printf("Tracking back; level resumed\n");
            return;
        }
    }
    if (input.start_pressed && !tracking_recalibrating_) {
        tracking_calibration_.reset();
        tracking_recalibrating_ = true;
        printf("Recalibrating both Move after tracking loss\n");
    }
    if (!tracking_recalibrating_) return;
    if (!input.calibration_hands_separated) {
        tracking_calibration_.reset();
    } else if (tracking_calibration_.sample(nowMilliseconds(),
                                            input.optical.left, input.optical.right)) {
        body_frame_.recenter(tracking_calibration_.origin(),
                             tracking_calibration_.handleOrigin());
        pose_calibrated_ = true;
        assignHands(input.optical);
        resumeAfterTrackingLoss();
        printf("Tracking recovered; level resumed\n");
    }
}

void App::resumeAfterTrackingLoss() {
    tracking_recalibrating_ = false;
    tracking_paused_ = false;
    resume_since_ms_ = 0;
    tracking_gate_.reset();
    game_.invalidateInput();
    diagnostic_.tracking_resume_audio_ms = audio_clock_ ? audio_.timeMs() : 0u;
    diagnostic_.tracking_resume_score = game_.score().score;
    diagnostic_.tracking_resume_hits = game_.score().hits;
    diagnostic_.tracking_resume_misses = game_.score().misses;
    synchronizePause();
    ++diagnostic_.tracking_recovery_count;
}

void App::handlePauseMenu(FrameInput& input) {
    if (run_state_ != RUN_PLAY || tracking_paused_ || g_pause_requested) {
        if (run_state_ != RUN_PLAY) user_paused_ = false;
        return;
    }
    const unsigned short pressed = input.buttons.pressed;
    if (!user_paused_) {
        if (!input.start_pressed) return;
        user_paused_ = true;
        synchronizePause();
        input.start_pressed = false;
        ++diagnostic_.pause_menu_opens;
        printf("Paused\n");
        return;
    }
    // The menu consumes the buttons; nothing else acts on them.
    input.start_pressed = false;
    if (pressed & (kButtonStart | kButtonCross)) {
        user_paused_ = false;
        game_.invalidateInput();
        synchronizePause();
        printf("Resumed\n");
    } else if (pressed & kButtonCircle) {
        user_paused_ = false;
        restartLevel();
    } else if (pressed & kButtonTriangle) {
        user_paused_ = false;
        quitLevel(false);
    }
}

void App::restartLevel() {
    ++diagnostic_.level_restarts;
    audio_.shutdown();
    audio_clock_ = false;
    synchronizePause();
    left_rumble_frames_ = right_rumble_frames_ = 0;
    printf("Level restarted\n");
    beginLevel();
}

void App::quitLevel(bool tracking_lost) {
    audio_.shutdown();
    audio_clock_ = false;
    tracking_paused_ = false;
    tracking_recalibrating_ = false;
    tracking_calibration_.reset();
    tracking_gate_.reset();
    synchronizePause();
    run_state_ = RUN_SELECT;
    if (playlist_run_.active()) printf("Playlist run stopped\n");
    playlist_run_.stop();
    next_song_remaining_ms_ = 0;
    game_.reset();
    cut_feedback_.clear();
    left_rumble_frames_ = right_rumble_frames_ = 0;
    carousel_.reset(static_cast<int>(shelf_pos_));
    last_tick_row_ = carousel_.nearestRow();
    selection_changed_ms_ = nowMilliseconds();
    if (tracking_lost) {
        ++diagnostic_.tracking_quit_count;
        printf("Level left while tracking was lost\n");
    } else {
        ++diagnostic_.pause_quits;
        printf("Level left from the pause menu\n");
    }
}

void App::updateCalibration(FrameInput& input) {
    if (calibrating_ && !paused_) {
        const movebeat::BodyCalibration::Step before = body_calibration_.step();
        // The Move button of each controller says "I am in the pose". In
        // the first pose (pointing at the camera, as libgem wants it) the
        // press also recalibrates that controller's sphere.
        const unsigned short pressed[2] = {input.buttons.left_pressed,
                                           input.buttons.right_pressed};
        for (unsigned int slot = 0; slot < 2; ++slot) {
            if ((pressed[slot] & GEM_BUTTON_MOVE) == 0) continue;
            if (before == movebeat::BodyCalibration::STEP_FORWARD && !demo_mode_ &&
                gem_.calibrateSlot(slot))
                printf("Calibration: sphere %u recalibrating\n", slot);
            body_calibration_.confirm(static_cast<int>(slot));
            pulseRumble(physicalSlotForHand(slot, hand_slots_swapped_) == 0u ? 0 : 1,
                        2, 120);
        }
        input.calibrated_start = body_calibration_.sample(
            nowMilliseconds(), input.optical.left, input.optical.right,
            !input.moves.partial[0], !input.moves.partial[1]);
        diagnostic_.calibration_partial_measures = body_calibration_.partialMeasures();
        // The hands are known after the sideways pose: color the spheres
        // now, so the last pose already checks tracking in those colors.
        if (before == movebeat::BodyCalibration::STEP_SIDES &&
            body_calibration_.step() == movebeat::BodyCalibration::STEP_DOWN)
            setHandAssignment(body_calibration_.slotsSwapped());
        if (body_calibration_.step() != before) {
            // Each pose held: a tap in both hands.
            pulseRumble(0, 4, 160);
            pulseRumble(1, 4, 160);
            audio_.playEffect(SFX_DIFFICULTY, 0.6f, 0.0f);
        }
        diagnostic_.calibration_step = static_cast<int>(body_calibration_.step());
        if (input.calibrated_start) {
            diagnostic_.body_up_inverted = body_calibration_.invertedUp() ? 1 : 0;
            diagnostic_.body_narrow_sides = body_calibration_.narrowSides() ? 1 : 0;
            body_frame_ = body_calibration_.frame();
            pose_calibrated_ = true;
            setHandAssignment(body_calibration_.slotsSwapped());
            calibrating_ = false;
            diagnostic_.body_span_cm = static_cast<int>(body_calibration_.span() * 100.0f);
            diagnostic_.body_drop_cm = static_cast<int>(body_calibration_.drop() * 100.0f);
            diagnostic_.body_turn_deg = static_cast<int>(body_calibration_.turnDegrees());
            diagnostic_.body_turn_applied = body_calibration_.turnApplied() ? 1 : 0;
            diagnostic_.body_flip_depth = body_calibration_.flipDepth() ? 1 : 0;
            diagnostic_.body_scale_pct = static_cast<int>(body_frame_.scale * 100.0f + 0.5f);
            printf("Calibrated: GEM slot %u left, span %d cm, scale %d%%, turn %d deg%s\n",
                   physicalSlotForHand(0u, hand_slots_swapped_),
                   diagnostic_.body_span_cm, diagnostic_.body_scale_pct,
                   diagnostic_.body_turn_deg,
                   body_calibration_.turnApplied() ? "" : " (not applied)");
        }
    }
    if (paused_) body_calibration_.reset();
}

bool App::startLevelAudio(const char* failure_message, bool clear_error_on_success) {
    audio_clock_ = audio_.start(audio_path_);
    if (!audio_clock_) {
        audio_start_error_ = audio_.lastError();
        run_state_ = RUN_AUDIO_ERROR;
        diagnostic_.audio_error_code =
            static_cast<unsigned int>(audio_start_error_);
        printf("%s: %08X\n", failure_message, audio_.lastError());
    } else if (clear_error_on_success) {
        audio_start_error_ = 0;
        diagnostic_.audio_error_code = 0;
    }
    return audio_clock_;
}

void App::handleStart(const FrameInput& input) {
    if (input.start_pressed && run_state_ == RUN_SELECT &&
        !input.cancel_calibration && !input.hand_toggle && input.blade_modifier) {
        calibrating_ = false;
        if (gem_.captureActive()) {
            gem_.stopCapture();
            printf("GEM capture stopped: %s\n", kCapturePath);
        } else if (gem_.startCapture(kCapturePath)) {
            printf("GEM capture started (30 second limit): %s\n", kCapturePath);
        } else {
            printf("GEM capture could not open: %s\n", kCapturePath);
        }
    } else if (input.start_pressed && run_state_ == RUN_SELECT && !paused_ &&
               !input.cancel_calibration && !input.hand_toggle) {
        // Parse the chart now; the calibration hides the cost.
        if (!ensureSelectedChart()) return;
        beginBodyCalibration();
    } else if (input.calibrated_start ||
               (input.start_pressed && run_state_ == RUN_RESULTS && !paused_ &&
                input.moves.left.tracked && input.moves.right.tracked)) {
        startOrContinue();
    } else {
        handleMoveChord(input);
    }
}

void App::handleMoveChord(const FrameInput& input) {
    if (!input.move_chord || paused_ || calibrating_ ||
        input.cancel_calibration || input.hand_toggle ||
        (run_state_ != RUN_SELECT && run_state_ != RUN_RESULTS))
        return;
    const bool ready = handsSeparatedForCalibration(input.moves.left,
                                                    input.moves.right);
    // The results screen replays with the measured origin; it needs both
    // spheres in view to tell the hands apart.
    if (run_state_ == RUN_RESULTS && !(ready && pose_calibrated_)) return;
    // The summary after a finished playlist only goes back (Triangle).
    if (run_state_ == RUN_RESULTS && playlist_run_.active() &&
        !playlist_run_.hasNext()) return;
    if (!ensureSelectedChart()) return;
    ++diagnostic_.menu_chord_starts;
    pulseRumble(0, 5, 200);
    pulseRumble(1, 5, 200);
    if (ready && pose_calibrated_) {
        stopPreview(250);
        audio_.playEffect(SFX_LAND, 0.6f, 0.0f);
        // The hands are wherever the two controllers are right now.
        assignHands(input.moves);
        printf("Move chord: GEM slot %u is the left hand\n",
               physicalSlotForHand(0u, hand_slots_swapped_));
        startOrContinue();
        return;
    }
    // First start of the session (or the hands overlap): the guided
    // calibration measures the body and assigns the hands, then starts.
    printf("Move chord: calibrating before the first level\n");
    beginBodyCalibration();
}

void App::startOrContinue() {
    if (run_state_ == RUN_RESULTS && playlist_run_.active()) {
        if (playlist_run_.hasNext()) playNextPlaylistSong();
        return;
    }
    if ((run_state_ != RUN_SELECT && run_state_ != RUN_RESULTS) ||
        !ensureSelectedChart())
        return;
    if (run_state_ == RUN_SELECT) {
        if (shelf_ > 0 && shelf_pos_ < shelf_order_.size()) {
            // The difficulty chosen for the first song guides the songs
            // the playlist names no difficulty for.
            const int rank = selected_chart_index_ < selected_level_.charts.size()
                ? difficultyRank(selected_level_.charts[selected_chart_index_].difficulty)
                : -1;
            playlist_run_.begin(shelf_ - 1, playlists_[shelf_ - 1].tracks,
                                shelf_pos_, rank);
            printf("Playlist run: %s from song %u\n",
                   playlists_[shelf_ - 1].manifest.title.c_str(),
                   static_cast<unsigned int>(shelf_pos_ + 1));
        } else {
            playlist_run_.stop();
        }
    }
    beginLevel();
}

void App::updatePlaylistRun() {
    const unsigned int now_ms = nowMilliseconds();
    const unsigned int elapsed = last_playlist_ms_ ? now_ms - last_playlist_ms_ : 0u;
    last_playlist_ms_ = now_ms;
    if (run_state_ != RUN_RESULTS || !playlist_run_.hasNext() ||
        next_song_remaining_ms_ == 0 || paused_)
        return;
    if (elapsed >= next_song_remaining_ms_) playNextPlaylistSong();
    else next_song_remaining_ms_ -= elapsed;
}

void App::playNextPlaylistSong() {
    next_song_remaining_ms_ = 0;
    while (playlist_run_.hasNext()) {
        const std::size_t level = playlist_run_.advance();
        // The shelf is the run's playlist, so its position is the run's.
        shelf_pos_ = playlist_run_.position();
        carousel_.reset(static_cast<int>(shelf_pos_));
        last_tick_row_ = carousel_.nearestRow();
        selectLevel(level);
        const PlaylistChoice& playlist = playlists_[playlist_run_.playlist()];
        selected_chart_index_ = choosePlaylistChart(
            playlist.manifest.songs[playlist.song_indices[shelf_pos_]],
            selected_level_, playlist_run_.preferredRank());
        if (selected_chart_index_ < selected_level_.charts.size())
            printf("Playlist song %u: %s\n",
                   static_cast<unsigned int>(shelf_pos_ + 1),
                   selected_level_.charts[selected_chart_index_].difficulty.c_str());
        if (ensureSelectedChart()) {
            beginLevel();
            return;
        }
        // A chart that cannot load is skipped and counts as failed.
        PlaylistSongResult skipped;
        skipped.level = level;
        skipped.chart = selected_chart_index_;
        skipped.failed = true;
        playlist_run_.record(skipped);
    }
}

void App::beginLevel() {
    run_state_ = RUN_PLAY;
    tracking_paused_ = false;
    tracking_recalibrating_ = false;
    tracking_gate_.reset();
    game_.reset();
    cut_feedback_.clear();
    best_score_ = movebeat::HighScore();
    level_start_us_ = nowMicroseconds();
    audio_clock_ = false;
    if (imported_level_) startLevelAudio("Audio start failed", true);
    if (run_state_ == RUN_PLAY) printf("Level started\n");
}

void App::updateGameplay(FrameInput& input) {
    MoveFrame& moves = input.moves;
    if (hand_slots_swapped_) swapMoveFrameHands(moves);
    if (run_state_ == RUN_PLAY) {
        moves.left = body_frame_.apply(moves.left);
        moves.right = body_frame_.apply(moves.right);
        // Physical reach limits for real controllers (the emulator's
        // scripted poses are play-space already and may stand anywhere).
        if (!demo_mode_) {
            moves.left = body_frame_.constrain(moves.left);
            moves.right = body_frame_.constrain(moves.right);
        }
    }
    input.raw_level_time_ms = audio_clock_ ? audio_.timeMs() :
        (run_state_ == RUN_PLAY
            ? unpausedElapsedMs(
                nowMicroseconds(), level_start_us_, pause_started_us_, paused_)
            : 0u);
    input.level_time_ms = adjustedTime(input.raw_level_time_ms,
                                       settings_.timing_offset_ms);
    // Trails follow wall-clock time so they also fade while audio is paused.
    if (run_state_ == RUN_PLAY && !paused_ && screen_.effectsEnabled()) {
        const unsigned int now_ms = nowMilliseconds();
        saber_trails_[0].add(moves.left, now_ms);
        saber_trails_[1].add(moves.right, now_ms);
    } else {
        saber_trails_[0].clear();
        saber_trails_[1].clear();
    }
    if (run_state_ == RUN_PLAY && !paused_) {
        game_.update(input.level_time_ms, moves.left, moves.right);
        const movebeat::FrameEvents& events = game_.events();
        cut_feedback_.add(events, input.level_time_ms);
        // Gameplay feedback is haptic only: the song stays the only sound.
        // A cut is felt in the cutting hand.
        if (events.left_hit) pulseRumble(0, 3, 160);
        if (events.right_hit) pulseRumble(1, 3, 160);
        if (events.bomb_hit) {
            pulseRumble(0, 6, 230);
            pulseRumble(1, 6, 230);
        } else if (events.miss) {
            pulseRumble(0, 3, 70);
            pulseRumble(1, 3, 70);
        }
    }
    applyRumble();
    const bool audio_tail_finished = !audio_clock_ || audio_.finished() ||
                                     game_.score().failed;
    if (run_state_ == RUN_PLAY && !paused_ &&
        game_.finished(input.level_time_ms) && audio_tail_finished) {
        if (demo_mode_ && !demo_options_.playlist_test) restartDemoCycle();
        else finishLevel();
    }
}

void App::restartDemoCycle() {
    // Keep the visual preview alive without producing a fake score.
    printf("Demo cycle %u: score=%u hits=%u misses=%u\n",
           demo_cycles_ + 1u, game_.score().score,
           game_.score().hits, game_.score().misses);
    diagnostic_.last_cycle_score = game_.score().score;
    diagnostic_.last_cycle_hits = game_.score().hits;
    diagnostic_.last_cycle_misses = game_.score().misses;
    ++demo_cycles_;
    audio_.shutdown();
    audio_clock_ = false;
    game_.reset();
    cut_feedback_.clear();
    level_start_us_ = nowMicroseconds();
    if (imported_level_) startLevelAudio("Demo audio restart failed", false);
    left_rumble_frames_ = right_rumble_frames_ = 0;
}

void App::finishLevel() {
    audio_.shutdown();
    audio_clock_ = false;
    left_rumble_frames_ = right_rumble_frames_ = 0;
    run_state_ = RUN_RESULTS;
    const std::string level_identity = imported_level_
        ? (selected_level_.source_key.empty()
            ? level_directory_ : selected_level_.source_key)
        : "builtin_tutorial";
    const std::string chart_identity = imported_level_ &&
        selected_chart_index_ < selected_level_.charts.size()
        ? selected_level_.charts[selected_chart_index_].file
        : "tutorial";
    const std::string score_key = movebeat::makeScoreKey(
        level_identity, chart_identity);
    std::string score_error;
    if (!movebeat::loadBestScoreFile(kScoreLog, score_key,
                                     best_score_, score_error)) {
        printf("Score read failed: %s\n", score_error.c_str());
    }
    const movebeat::ScoreState& current = game_.score();
    if (current.score > best_score_.score ||
        (current.score == best_score_.score &&
         current.max_combo > best_score_.max_combo)) {
        best_score_.score = current.score;
        best_score_.max_combo = current.max_combo;
        if (!movebeat::appendScoreFile(kScoreLog, score_key,
                                       best_score_, score_error)) {
            printf("Score write failed: %s\n", score_error.c_str());
        }
    }
    printf("Level finished: score=%u hits=%u misses=%u\n",
           game_.score().score, game_.score().hits, game_.score().misses);
    if (!playlist_run_.active()) return;
    PlaylistSongResult result;
    result.level = selected_level_index_;
    result.chart = selected_chart_index_;
    result.score = current.score;
    result.hits = current.hits;
    result.misses = current.misses;
    result.max_combo = current.max_combo;
    result.failed = current.failed;
    playlist_run_.record(result);
    // Time to see the result, and to breathe, before the next song.
    next_song_remaining_ms_ = playlist_run_.hasNext() ? 8000u : 0u;
    diagnostic_.playlist_songs_played =
        static_cast<unsigned int>(playlist_run_.results().size());
    diagnostic_.playlist_total_score = playlist_run_.totalScore();
    if (playlist_run_.complete()) {
        diagnostic_.playlist_complete = 1;
        printf("Playlist complete: %u songs, score %u\n",
               static_cast<unsigned int>(playlist_run_.results().size()),
               playlist_run_.totalScore());
    }
}

void App::renderPlayfield(const FrameInput& input) {
    const unsigned int level_time_ms = input.level_time_ms;
    const unsigned long long frame_scene_start_us = nowMicroseconds();
    // The built-in tutorial is 120 BPM; imported levels carry their own.
    const float bpm = imported_level_ && selected_level_.bpm > 0.0f
        ? selected_level_.bpm : 120.0f;
    // A failed level keeps the red clear color as its warning.
    if (!game_.score().failed && draw_background_)
        drawBackground(screen_, game_.noteSpawnDepth(), level_time_ms,
                       beatPulse(level_time_ms, bpm));
    drawFloor(screen_, game_.noteSpawnDepth());
    drawGrid(screen_, game_.noteSpawnDepth());
    drawObstacles(screen_, game_, level_time_ms);
    drawArcs(screen_, game_, level_time_ms);
    const unsigned long long frame_scene_end_us = nowMicroseconds();
    frame_scene_timing_.add(static_cast<unsigned int>(
        frame_scene_end_us - frame_scene_start_us));
    const bool solid_notes_drawn = screen_.drawSolidNotes(game_, level_time_ms);
    frame_gpu_notes_timing_.add(static_cast<unsigned int>(
        nowMicroseconds() - frame_scene_end_us));
    if (solid_notes_drawn) {
        const unsigned int cubes = screen_.lastSolidNoteCount();
        if (cubes) {
            if (screen_.usingRsx()) {
                ++diagnostic_.gpu_note_draw_calls;
                diagnostic_.gpu_note_max_cubes = std::max(
                    diagnostic_.gpu_note_max_cubes, cubes);
            } else {
                ++diagnostic_.software_note_draw_calls;
                diagnostic_.software_note_max_cubes = std::max(
                    diagnostic_.software_note_max_cubes, cubes);
            }
        }
    } else {
        ++diagnostic_.gpu_note_fallback_frames;
    }
    drawNotes(screen_, game_, level_time_ms, !solid_notes_drawn);
    if (!solid_notes_drawn) {
        drawMenuText(screen_, "3D RENDER UNAVAILABLE", 24,
                     screen_.height() - 54, 2, 0xffffad8bu);
    }
    drawCutFeedback(screen_, cut_feedback_, level_time_ms);
    const unsigned int trail_now_ms = nowMilliseconds();
    drawSaberTrail(screen_, saber_trails_[0], trail_now_ms, 0xff70d8ffu);
    drawSaberTrail(screen_, saber_trails_[1], trail_now_ms, 0xffff6878u);
    drawSaber(screen_, input.moves.left, 0xff70d8ffu, 0xff174870u);
    drawSaber(screen_, input.moves.right, 0xffff6878u, 0xff701727u);
}

void App::renderOverlays(const FrameInput& input) {
    const MoveFrame& moves = input.moves;
    drawConnection(screen_, 0, moves.left_connected, moves.left.tracked, 0xff40b8ffu);
    drawConnection(screen_, 1, moves.right_connected, moves.right.tracked, 0xffff4058u);
    if (run_state_ == RUN_PLAY && user_paused_ && !tracking_paused_)
        drawPauseMenu(screen_);
    if (run_state_ == RUN_PLAY && tracking_paused_) {
        drawTrackingPauseOverlay(screen_, input.both_moves_optical,
                                 tracking_recalibrating_,
                                 input.calibration_hands_separated,
                                 tracking_recalibrating_
                                     ? tracking_calibration_.progressMs()
                                     : (resume_since_ms_
                                            ? (nowMilliseconds() - resume_since_ms_) * 2u
                                            : 0u));
    }
    if (calibrating_) {
        CalibrationView view;
        view.step = static_cast<int>(body_calibration_.step());
        view.directions_disagree = body_calibration_.directionsDisagree();
        // Live hands around the figure: relative to the forward pose once
        // it is known, otherwise to the midpoint of the two handles.
        const movebeat::SaberPose* poses[2] = {&moves.left, &moves.right};
        movebeat::Vec3 reference = body_calibration_.forwardKnown()
            ? body_calibration_.forwardHandles()
            : (moves.left.handle + moves.right.handle) * 0.5f;
        for (int slot = 0; slot < 2; ++slot) {
            view.hand_state[slot] = static_cast<int>(body_calibration_.handState(slot));
            view.hand_visible[slot] = poses[slot]->tracked;
            view.hand_x[slot] = poses[slot]->handle.x - reference.x;
            view.hand_y[slot] = poses[slot]->handle.y - reference.y;
        }
        drawCalibrationOverlay(screen_, view);
    }
    if (gem_.captureActive()) drawCaptureIndicator(screen_);
    if (run_state_ == RUN_PLAY && imported_level_ && !selected_level_.charts.empty())
        drawChartPips(screen_, selected_level_.charts.size(), selected_chart_index_);
    if (run_state_ == RUN_SELECT) {
        drawHandMode(screen_, settings_.invert_hands);
        if (input.blade_modifier)
            drawBladeLength(screen_, settings_.blade_length_cm);
        else if (input.timing_modifier)
            drawTimingOffset(screen_, settings_.timing_offset_ms);
    }
    drawRunStateIndicator(screen_,
                          run_state_ == RUN_PLAY ? 0xff40e070u :
                          (run_state_ == RUN_RESULTS ? 0xff8060d0u : 0xffffa020u));
    if (run_state_ == RUN_PLAY) {
        const unsigned int song_duration_ms = audio_clock_
            ? audio_.durationMs() : chart_duration_ms_;
        drawHud(screen_, game_.score(), input.level_time_ms, song_duration_ms);
    }
}

void App::renderFrame(const FrameInput& input,
                      unsigned long long frame_work_start_us) {
    const unsigned long long frame_wait_start_us = nowMicroseconds();
    screen_.waitForPreviousFlip();
    const unsigned long long frame_render_start_us = nowMicroseconds();
    const unsigned int frame_wait_us = static_cast<unsigned int>(
        frame_render_start_us - frame_wait_start_us);
    frame_wait_timing_.add(frame_wait_us);
    screen_.beginFrame(game_.score().failed ? 0xff200508u : 0xff050510u);
    frame_clear_timing_.add(static_cast<unsigned int>(
        nowMicroseconds() - frame_render_start_us));
    if (run_state_ == RUN_RESULTS && playlist_run_.complete()) {
        drawPlaylistSummary(screen_, playlist_run_,
                            playlists_[playlist_run_.playlist()], level_choices_);
    } else if (run_state_ == RUN_RESULTS) {
        drawResults(screen_, game_.score(), best_score_);
        if (playlist_run_.hasNext()) {
            const std::size_t next = shelf_order_[(shelf_pos_ + 1) % shelf_order_.size()];
            drawPlaylistNext(screen_, playlist_run_, level_choices_[next],
                             (next_song_remaining_ms_ + 999u) / 1000u);
        }
    } else if (run_state_ == RUN_SELECT) {
        LibraryView view;
        view.position = carousel_.position();
        view.chart_index = selected_chart_index_;
        view.halo_color = selected_chart_index_ < selected_level_.charts.size()
            ? difficultyColor(selected_level_.charts[selected_chart_index_].difficulty)
            : difficultyColor("Easy");
        const unsigned int now_ms = nowMilliseconds();
        const unsigned int since_change = now_ms - halo_flash_ms_;
        view.halo_flash = halo_flash_ms_ && since_change < 400u
            ? 1.0f - since_change / 400.0f : 0.0f;
        view.preview_playing = preview_active_ && audio_.musicActive();
        view.now_ms = now_ms;
        view.playlist = shelf_ > 0 ? &playlists_[shelf_ - 1] : 0;
        view.shelf = shelf_;
        view.shelf_count = playlists_.size() + 1;
        drawLevelLibrary(screen_, level_choices_, shelf_order_, view);
    } else if (run_state_ == RUN_AUDIO_ERROR) {
        drawAudioError(screen_, audio_start_error_);
    } else {
        renderPlayfield(input);
    }
    renderOverlays(input);
    const unsigned long long frame_work_end_us = nowMicroseconds();
    const unsigned int frame_work_us = static_cast<unsigned int>(
        frame_work_end_us - frame_work_start_us);
    frame_work_timing_.add(frame_work_us);
    last_frame_active_us_ = frame_work_us > frame_wait_us
        ? frame_work_us - frame_wait_us : 0u;
    frame_active_timing_.add(frame_work_us > frame_wait_us
        ? frame_work_us - frame_wait_us : 0u);
    frame_render_timing_.add(static_cast<unsigned int>(
        frame_work_end_us - frame_render_start_us));
    saveDiagnosticFrames(input);
    screen_.present();
    diagnostic_.batch_max_vertices = std::max(
        diagnostic_.batch_max_vertices, screen_.lastBatchVertices());
    diagnostic_.batch_dropped_primitives += screen_.lastBatchDropped();
    sysUtilCheckCallback();
}

void App::saveDiagnosticFrames(const FrameInput& input) {
    if (!first_frame_saved_) {
        first_frame_saved_ = screen_.savePpm(kFirstFramePath);
        if (first_frame_saved_)
            printf("First rendered frame saved: %s\n", kFirstFramePath);
    }
    if (menu_capture_path_) {
        // Library frames requested by the scripted menu run.
        if (screen_.savePpm(menu_capture_path_))
            printf("Menu test frame saved: %s\n", menu_capture_path_);
        menu_capture_path_ = 0;
    }
    if (!demo_mode_ || scriptedMenuRun() || run_state_ != RUN_PLAY) return;
    if (!demo_frame_saved_) {
        bool visible_upper_note = false;
        bool visible_lower_note = false;
        for (std::size_t index = 0; index < game_.noteCount(); ++index) {
            if (!game_.notePending(index)) continue;
            const movebeat::Note& note = game_.note(index);
            const movebeat::Vec3 position =
                game_.notePosition(note, input.level_time_ms);
            if (position.z < -0.8f || position.z > game_.noteSpawnDepth())
                continue;
            if (note.row >= 2) visible_upper_note = true;
            else visible_lower_note = true;
        }
        // Prefer a frame that actually demonstrates the vertical spread, not
        // an arbitrary early frame before any chart notes enter the playfield.
        if (input.raw_level_time_ms >= 1200u && visible_upper_note &&
            visible_lower_note) {
            demo_frame_saved_ = screen_.savePpm(kDemoFramePath);
            if (demo_frame_saved_)
                printf("Emulator perspective frame saved: %s\n", kDemoFramePath);
        }
    }
    if (demo_options_.capture_at_ms && !demo_timed_frame_saved_ &&
        input.raw_level_time_ms >= demo_options_.capture_at_ms) {
        demo_timed_frame_saved_ = screen_.savePpm(kDemoTimedFramePath);
        if (demo_timed_frame_saved_)
            printf("Timed demo frame saved at %u ms: %s\n",
                   input.raw_level_time_ms, kDemoTimedFramePath);
    }
    if (!demo_scored_frame_saved_ && game_.score().hits >= 3u) {
        demo_scored_frame_saved_ = screen_.savePpm(kDemoScoredFramePath);
        if (demo_scored_frame_saved_)
            printf("Scored demo frame saved: %s\n", kDemoScoredFramePath);
    }
}

void App::storeFrameTiming(bool only_with_samples) {
    const FrameTimingSample work = frame_work_timing_.take();
    const FrameTimingSample wait = frame_wait_timing_.take();
    const FrameTimingSample active = frame_active_timing_.take();
    const FrameTimingSample render = frame_render_timing_.take();
    const FrameTimingSample clear = frame_clear_timing_.take();
    const FrameTimingSample scene = frame_scene_timing_.take();
    const FrameTimingSample gpu = frame_gpu_notes_timing_.take();
    if (only_with_samples && !work.count) return;
    diagnostic_.frame_work_samples = work.count;
    diagnostic_.frame_work_avg_us = work.average_us;
    diagnostic_.frame_work_max_us = work.maximum_us;
    diagnostic_.frame_work_over_60hz = work.over_60hz_budget;
    diagnostic_.frame_wait_avg_us = wait.average_us;
    diagnostic_.frame_active_avg_us = active.average_us;
    diagnostic_.frame_active_max_us = active.maximum_us;
    diagnostic_.frame_active_over_60hz = active.over_60hz_budget;
    diagnostic_.frame_render_avg_us = render.average_us;
    diagnostic_.frame_render_max_us = render.maximum_us;
    diagnostic_.frame_clear_avg_us = clear.average_us;
    diagnostic_.frame_scene_avg_us = scene.average_us;
    diagnostic_.frame_gpu_notes_avg_us = gpu.average_us;
}

void App::publishDiagnostics(const FrameInput& input) {
    ++diagnostic_frame_;
    const char* diagnostic_phase = run_state_ == RUN_AUDIO_ERROR ? "audio_error" :
        tracking_paused_
            ? (tracking_recalibrating_ ? "tracking_recalibration" : "tracking_pause") :
        demo_mode_ && demo_options_.pause_test
            ? (menu_test_done_ ? "pause_test_done" : "pause_test") :
        demo_mode_ ? (demo_options_.playlist_test
                          ? (menu_test_done_ ? "playlist_test_done" : "playlist_test") :
                      !demo_options_.menu_test ? "demo_play" :
                      (menu_test_done_ ? "menu_test_done" : "menu_test")) :
        paused_ ? "paused" :
        (calibrating_ ? "calibrating" :
         (run_state_ == RUN_PLAY ? "play" :
          (run_state_ == RUN_RESULTS ? "results" : "select")));
    if (diagnostic_frame_ % 120u != 0u &&
        strcmp(diagnostic_phase, previous_diagnostic_phase_) == 0)
        return;
    const MoveFrame& moves = input.moves;
    diagnostic_.state = "running";
    diagnostic_.phase = diagnostic_phase;
    diagnostic_.frame = diagnostic_frame_;
    diagnostic_.uptime_ms = static_cast<unsigned int>(
        (nowMicroseconds() - process_start_us_) / 1000ull);
    storeFrameTiming(false);
    ++diagnostic_.heartbeat;
    diagnostic_.level_count = static_cast<int>(level_choices_.size());
    diagnostic_.imported_level = imported_level_ ? 1 : 0;
    diagnostic_.level_id = imported_level_
        ? selected_level_.source_key : "builtin_tutorial";
    diagnostic_.chart_difficulty = imported_level_ &&
        selected_chart_index_ < selected_level_.charts.size()
        ? selected_level_.charts[selected_chart_index_].difficulty : "Easy";
    diagnostic_.left_connected = moves.left_connected ? 1 : 0;
    diagnostic_.right_connected = moves.right_connected ? 1 : 0;
    diagnostic_.left_tracked = moves.left.tracked ? 1 : 0;
    diagnostic_.right_tracked = moves.right.tracked ? 1 : 0;
    diagnostic_.hand_slots_swapped = hand_slots_swapped_ ? 1 : 0;
    diagnostic_.pose_calibrated = pose_calibrated_ ? 1 : 0;
    diagnostic_.imu_held_frames = gem_.imuHeldFrames();
    diagnostic_.imu_slot0_variant = gem_.imuVariant(0);
    diagnostic_.imu_slot1_variant = gem_.imuVariant(1);
    diagnostic_.imu_slot0_error_deg = static_cast<int>(gem_.imuErrorDegrees(0) + 0.5f);
    diagnostic_.imu_slot1_error_deg = static_cast<int>(gem_.imuErrorDegrees(1) + 0.5f);
    diagnostic_.imu_slot0_scored = gem_.imuScored(0);
    diagnostic_.imu_slot1_scored = gem_.imuScored(1);
    diagnostic_.gem_slot0_result = gem_.lastResult(0);
    diagnostic_.gem_slot1_result = gem_.lastResult(1);
    diagnostic_.gem_slot0_flags = gem_.lastTrackingFlags(0);
    diagnostic_.gem_slot1_flags = gem_.lastTrackingFlags(1);
    diagnostic_.hand_assignment_established =
        hand_assignment_established_ ? 1 : 0;
    diagnostic_.invert_hands_setting = settings_.invert_hands ? 1 : 0;
    diagnostic_.hue_values_known = gem_.hueValuesKnown() ? 1 : 0;
    diagnostic_.hand_hues_applied =
        gem_.handHueAssignmentApplied() ? 1 : 0;
    diagnostic_.hue_assignment_pending =
        gem_.hueAssignmentPending() ? 1 : 0;
    diagnostic_.gem_slot0_hue = gem_.hueForPhysicalSlot(0);
    diagnostic_.gem_slot1_hue = gem_.hueForPhysicalSlot(1);
    diagnostic_.hue_error_code =
        static_cast<unsigned int>(gem_.lastHueError());
    diagnostic_.tracking_paused = tracking_paused_ ? 1 : 0;
    diagnostic_.tracking_recalibrating = tracking_recalibrating_ ? 1 : 0;
    diagnostic_.gem_update_ok = input.gem_update_ok ? 1 : 0;
    diagnostic_.gem_update_stage =
        static_cast<unsigned int>(gem_.lastUpdateStage());
    diagnostic_.gem_error_code =
        static_cast<unsigned int>(gem_.lastError());
    diagnostic_.camera_read_count = gem_.lastCameraReadCount();
    diagnostic_.gem_connected_count = gem_.lastGemConnectedCount();
    diagnostic_.inertial_ready_mask = gem_.lastInertialReadyMask();
    diagnostic_.button_ready_mask = gem_.lastButtonReadyMask();
    diagnostic_.left_buttons = moves.left_buttons;
    diagnostic_.right_buttons = moves.right_buttons;
    diagnostic_.audio_active = audio_clock_ ? 1 : 0;
    diagnostic_.audio_time_ms = audio_clock_ ? audio_.timeMs() : 0u;
    diagnostic_.capture_active = gem_.captureActive() ? 1 : 0;
    diagnostic_.score = game_.score().score;
    diagnostic_.hits = game_.score().hits;
    diagnostic_.misses = game_.score().misses;
    diagnostic_.demo_cycles = demo_cycles_;
    diagnostic_.sound_effects = audio_.effectsPlayed();
    writeRunStatus(kRunStatusPath, diagnostic_);
    previous_diagnostic_phase_ = diagnostic_phase;
}

} // namespace ps3
} // namespace movebeat
