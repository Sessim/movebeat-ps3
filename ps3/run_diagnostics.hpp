#ifndef MOVEBEAT_PS3_RUN_DIAGNOSTICS_HPP
#define MOVEBEAT_PS3_RUN_DIAGNOSTICS_HPP

#include <string>

namespace movebeat {
namespace ps3 {

struct RunStatus {
    const char* state;
    const char* phase;
    unsigned int heartbeat;
    unsigned int frame;
    unsigned int uptime_ms;
    unsigned int frame_work_samples;
    unsigned int frame_work_avg_us;
    unsigned int frame_work_max_us;
    unsigned int frame_work_over_60hz;
    unsigned int frame_wait_avg_us;
    unsigned int frame_active_avg_us;
    unsigned int frame_active_max_us;
    unsigned int frame_active_over_60hz;
    unsigned int frame_render_avg_us;
    unsigned int frame_render_max_us;
    unsigned int frame_clear_avg_us;
    unsigned int frame_scene_avg_us;
    unsigned int frame_gpu_notes_avg_us;
    unsigned int gpu_note_draw_calls;
    unsigned int gpu_note_max_cubes;
    unsigned int gpu_note_fallback_frames;
    unsigned int software_note_draw_calls;
    unsigned int software_note_max_cubes;
    // "rsx" when the RSX draws the frame, "software" for the PPU rasterizer.
    const char* renderer;
    int effects_enabled;
    unsigned int batch_max_vertices;
    unsigned int batch_dropped_primitives;
    // Library: startup scan cost and, in menu_test runs, per-step cost.
    unsigned int library_scan_ms;
    unsigned int menu_steps;
    unsigned int menu_step_avg_us;
    unsigned int menu_step_max_us;
    unsigned int menu_idle_max_us;
    // Library interaction counters (carousel rows passed, flicks, swipes,
    // randomizer spins, song previews) and sound effects played.
    unsigned int menu_ticks;
    unsigned int menu_flicks;
    unsigned int menu_difficulty_changes;
    unsigned int menu_randomizer_spins;
    unsigned int menu_preview_starts;
    // Gestures: T presses that began one, frames with T held while the
    // Move was untracked, and vertical drags recognized.
    unsigned int menu_t_presses;
    unsigned int menu_t_untracked_frames;
    unsigned int menu_gesture_drags;
    // Levels started with both Move buttons; a play origin is measured.
    unsigned int menu_chord_starts;
    int pose_calibrated;
    // Playlists found, the shelf shown (0 all tracks) and the playlist run.
    unsigned int playlist_count;
    unsigned int shelf;
    unsigned int playlist_songs_played;
    unsigned int playlist_total_score;
    int playlist_complete;
    // libgem per physical slot: latest gemGetState result (0 ok, 2/8 not
    // calibrated, 3 calibrating, 4 computing colors, 5 hue not set, 6 no
    // video, -1 not read) and tracking flags (1 position, 2 visible), live
    // and at the last tracking pause; levels left from that pause.
    int gem_slot0_result;
    int gem_slot1_result;
    unsigned int gem_slot0_flags;
    unsigned int gem_slot1_flags;
    int tracking_pause_slot0_result;
    int tracking_pause_slot1_result;
    unsigned int tracking_pause_slot0_flags;
    unsigned int tracking_pause_slot1_flags;
    unsigned int tracking_quit_count;
    // Pause menu: opened, levels restarted, levels left.
    unsigned int pause_menu_opens;
    unsigned int level_restarts;
    unsigned int pause_quits;
    // Inertial hold: frames a pose was held by the Move's sensors, and per
    // slot the quaternion reading chosen (0-3), its disagreement with the
    // camera (degrees) and the turns it was scored on.
    unsigned int imu_held_frames;
    // gemPrepareCamera settings in use (renderer.ini), and whether hand
    // depth comes from the camera.
    int camera_depth;
    int camera_exposure;
    int camera_quality_pct;
    int imu_slot0_variant;
    int imu_slot1_variant;
    int imu_slot0_error_deg;
    int imu_slot1_error_deg;
    unsigned int imu_slot0_scored;
    unsigned int imu_slot1_scored;
    // Guided calibration: current step (0 forward, 1 sides, 2 down, 3
    // done), arms-down pose seen above the forward one, and the results.
    int calibration_step;
    int body_up_inverted;
    // The sideways pose was hardly wider than the forward one.
    int body_narrow_sides;
    // Calibration hands measured from partial fixes (fallback).
    unsigned int calibration_partial_measures;
    int body_span_cm;
    int body_drop_cm;
    int body_turn_deg;
    int body_turn_applied;
    // The forward pose pointed along the camera's -z: play depth flipped.
    int body_flip_depth;
    int body_scale_pct;
    unsigned int sound_effects;
    unsigned int error_stage;
    unsigned int error_code;
    int level_count;
    unsigned int library_cards_loaded;
    unsigned int library_card_bytes;
    int imported_level;
    std::string level_id;
    std::string chart_difficulty;
    int left_connected;
    int right_connected;
    int left_tracked;
    int right_tracked;
    int hand_slots_swapped;
    int hand_assignment_established;
    int invert_hands_setting;
    int hue_values_known;
    int hand_hues_applied;
    int hue_assignment_pending;
    unsigned int gem_slot0_hue;
    unsigned int gem_slot1_hue;
    unsigned int hue_error_code;
    int tracking_paused;
    int tracking_recalibrating;
    unsigned int tracking_pause_count;
    unsigned int tracking_recovery_count;
    unsigned int tracking_pause_audio_ms;
    unsigned int tracking_resume_audio_ms;
    unsigned int tracking_pause_score;
    unsigned int tracking_resume_score;
    unsigned int tracking_pause_hits;
    unsigned int tracking_resume_hits;
    unsigned int tracking_pause_misses;
    unsigned int tracking_resume_misses;
    int gem_update_ok;
    unsigned int gem_update_stage;
    unsigned int gem_error_code;
    unsigned int camera_read_count;
    unsigned int gem_connected_count;
    unsigned int inertial_ready_mask;
    unsigned int button_ready_mask;
    unsigned int left_buttons;
    unsigned int right_buttons;
    int audio_active;
    unsigned int audio_time_ms;
    unsigned int audio_error_code;
    int capture_active;
    unsigned int score;
    unsigned int hits;
    unsigned int misses;
    unsigned int demo_cycles;
    unsigned int last_cycle_score;
    unsigned int last_cycle_hits;
    unsigned int last_cycle_misses;

    RunStatus();
};

// Writes through a temporary sibling so an FTP reader never observes a
// partially-written heartbeat. The previous report is kept if writing fails.
bool writeRunStatus(const char* path, const RunStatus& status);

} // namespace ps3
} // namespace movebeat

#endif
