#include "run_diagnostics.hpp"

#include <stdio.h>
#include <string>

namespace movebeat {
namespace ps3 {

RunStatus::RunStatus()
    : state("booting"), phase("process_entered"), heartbeat(0), frame(0),
      uptime_ms(0), frame_work_samples(0), frame_work_avg_us(0),
      frame_work_max_us(0), frame_work_over_60hz(0),
      frame_wait_avg_us(0), frame_active_avg_us(0),
      frame_active_max_us(0), frame_active_over_60hz(0),
      frame_render_avg_us(0), frame_render_max_us(0),
      frame_clear_avg_us(0), frame_scene_avg_us(0),
      frame_gpu_notes_avg_us(0),
      gpu_note_draw_calls(0), gpu_note_max_cubes(0),
      gpu_note_fallback_frames(0),
      software_note_draw_calls(0), software_note_max_cubes(0),
      renderer("none"), effects_enabled(0), batch_max_vertices(0),
      batch_dropped_primitives(0), library_scan_ms(0), menu_steps(0),
      menu_step_avg_us(0), menu_step_max_us(0), menu_idle_max_us(0),
      menu_ticks(0), menu_flicks(0), menu_difficulty_changes(0),
      menu_randomizer_spins(0), menu_preview_starts(0), menu_chord_starts(0),
      pose_calibrated(0), playlist_count(0), shelf(0), playlist_songs_played(0),
      playlist_total_score(0), playlist_complete(0), sound_effects(0),
      error_stage(0), error_code(0), level_count(0),
      library_cards_loaded(0), library_card_bytes(0),
      imported_level(0), level_id(""), chart_difficulty(""),
      left_connected(0), right_connected(0),
      left_tracked(0), right_tracked(0), hand_slots_swapped(0),
      hand_assignment_established(0), invert_hands_setting(0),
      hue_values_known(0), hand_hues_applied(0),
      hue_assignment_pending(0), gem_slot0_hue(0), gem_slot1_hue(0),
      hue_error_code(0), tracking_paused(0),
      tracking_recalibrating(0), tracking_pause_count(0),
      tracking_recovery_count(0), tracking_pause_audio_ms(0),
      tracking_resume_audio_ms(0), tracking_pause_score(0),
      tracking_resume_score(0), tracking_pause_hits(0),
      tracking_resume_hits(0), tracking_pause_misses(0),
      tracking_resume_misses(0), gem_update_ok(0),
      gem_update_stage(0), gem_error_code(0), camera_read_count(0),
      gem_connected_count(0), inertial_ready_mask(0), button_ready_mask(0),
      left_buttons(0), right_buttons(0),
      audio_active(0), audio_time_ms(0),
      audio_error_code(0), capture_active(0),
      score(0), hits(0), misses(0), demo_cycles(0),
      last_cycle_score(0), last_cycle_hits(0), last_cycle_misses(0) {}

bool writeRunStatus(const char* path, const RunStatus& status) {
    if (!path || !path[0]) return false;
    const std::string temporary = std::string(path) + ".tmp";
    FILE* output = fopen(temporary.c_str(), "w");
    if (!output) return false;
    fprintf(output,
            "movebeat_run_status=1\n"
            "state=%s\nphase=%s\nheartbeat=%u\nframe=%u\nuptime_ms=%u\n"
            "frame_work_samples=%u\nframe_work_avg_us=%u\n"
            "frame_work_max_us=%u\nframe_work_over_60hz=%u\n"
            "frame_wait_avg_us=%u\nframe_active_avg_us=%u\n"
            "frame_active_max_us=%u\nframe_active_over_60hz=%u\n"
            "frame_render_avg_us=%u\nframe_render_max_us=%u\n"
            "frame_clear_avg_us=%u\nframe_scene_avg_us=%u\n"
            "frame_gpu_notes_avg_us=%u\n"
            "gpu_note_draw_calls=%u\ngpu_note_max_cubes=%u\n"
            "gpu_note_fallback_frames=%u\n"
            "software_note_draw_calls=%u\nsoftware_note_max_cubes=%u\n"
            "renderer=%s\neffects_enabled=%d\nbatch_max_vertices=%u\n"
            "batch_dropped_primitives=%u\n"
            "library_scan_ms=%u\nmenu_steps=%u\nmenu_step_avg_us=%u\n"
            "menu_step_max_us=%u\nmenu_idle_max_us=%u\n"
            "menu_ticks=%u\nmenu_flicks=%u\nmenu_difficulty_changes=%u\n"
            "menu_randomizer_spins=%u\nmenu_preview_starts=%u\n"
            "menu_chord_starts=%u\npose_calibrated=%d\n"
            "playlist_count=%u\nshelf=%u\nplaylist_songs_played=%u\n"
            "playlist_total_score=%u\nplaylist_complete=%d\n"
            "sound_effects=%u\n"
            "error_stage=%u\nerror_code=0x%08X\nlevel_count=%d\n"
            "library_cards_loaded=%u\nlibrary_card_bytes=%u\n"
            "imported_level=%d\nlevel_id=%s\nchart_difficulty=%s\n"
            "left_connected=%d\nright_connected=%d\n"
            "left_tracked=%d\nright_tracked=%d\nhand_slots_swapped=%d\n"
            "hand_assignment_established=%d\n"
            "invert_hands_setting=%d\n"
            "hue_values_known=%d\nhand_hues_applied=%d\n"
            "hue_assignment_pending=%d\n"
            "gem_slot0_hue=%u\ngem_slot1_hue=%u\n"
            "hue_error_code=0x%08X\n"
            "tracking_paused=%d\ntracking_recalibrating=%d\n"
            "tracking_pause_count=%u\ntracking_recovery_count=%u\n"
            "tracking_pause_audio_ms=%u\ntracking_resume_audio_ms=%u\n"
            "tracking_pause_score=%u\ntracking_resume_score=%u\n"
            "tracking_pause_hits=%u\ntracking_resume_hits=%u\n"
            "tracking_pause_misses=%u\ntracking_resume_misses=%u\n"
            "gem_update_ok=%d\ngem_update_stage=%u\n"
            "gem_error_code=0x%08X\ncamera_read_count=%u\n"
            "gem_connected_count=%u\ninertial_ready_mask=%u\n"
            "button_ready_mask=%u\n"
            "left_buttons=%u\nright_buttons=%u\naudio_active=%d\n"
            "audio_time_ms=%u\naudio_error_code=0x%08X\n"
            "capture_active=%d\nscore=%u\nhits=%u\nmisses=%u\n"
            "demo_cycles=%u\nlast_cycle_score=%u\nlast_cycle_hits=%u\n"
            "last_cycle_misses=%u\n",
            status.state, status.phase, status.heartbeat, status.frame,
            status.uptime_ms, status.frame_work_samples,
            status.frame_work_avg_us, status.frame_work_max_us,
            status.frame_work_over_60hz, status.frame_wait_avg_us,
            status.frame_active_avg_us, status.frame_active_max_us,
            status.frame_active_over_60hz, status.frame_render_avg_us,
            status.frame_render_max_us, status.frame_clear_avg_us,
            status.frame_scene_avg_us, status.frame_gpu_notes_avg_us,
            status.gpu_note_draw_calls,
            status.gpu_note_max_cubes, status.gpu_note_fallback_frames,
            status.software_note_draw_calls, status.software_note_max_cubes,
            status.renderer, status.effects_enabled, status.batch_max_vertices,
            status.batch_dropped_primitives,
            status.library_scan_ms, status.menu_steps, status.menu_step_avg_us,
            status.menu_step_max_us, status.menu_idle_max_us,
            status.menu_ticks, status.menu_flicks, status.menu_difficulty_changes,
            status.menu_randomizer_spins, status.menu_preview_starts,
            status.menu_chord_starts, status.pose_calibrated,
            status.playlist_count, status.shelf, status.playlist_songs_played,
            status.playlist_total_score, status.playlist_complete,
            status.sound_effects,
            status.error_stage, status.error_code,
            status.level_count, status.library_cards_loaded,
            status.library_card_bytes, status.imported_level,
            status.level_id.c_str(), status.chart_difficulty.c_str(),
            status.left_connected,
            status.right_connected, status.left_tracked, status.right_tracked,
            status.hand_slots_swapped, status.hand_assignment_established,
            status.invert_hands_setting, status.hue_values_known,
            status.hand_hues_applied, status.hue_assignment_pending,
            status.gem_slot0_hue, status.gem_slot1_hue,
            status.hue_error_code,
            status.tracking_paused, status.tracking_recalibrating,
            status.tracking_pause_count, status.tracking_recovery_count,
            status.tracking_pause_audio_ms, status.tracking_resume_audio_ms,
            status.tracking_pause_score, status.tracking_resume_score,
            status.tracking_pause_hits, status.tracking_resume_hits,
            status.tracking_pause_misses, status.tracking_resume_misses,
            status.gem_update_ok, status.gem_update_stage,
            status.gem_error_code, status.camera_read_count,
            status.gem_connected_count, status.inertial_ready_mask,
            status.button_ready_mask,
            status.left_buttons, status.right_buttons,
            status.audio_active, status.audio_time_ms, status.audio_error_code,
            status.capture_active, status.score,
            status.hits, status.misses, status.demo_cycles,
            status.last_cycle_score, status.last_cycle_hits,
            status.last_cycle_misses);
    const bool flushed = fflush(output) == 0;
    const bool closed = fclose(output) == 0;
    const bool complete = flushed && closed;
    if (!complete) {
        remove(temporary.c_str());
        return false;
    }
    if (rename(temporary.c_str(), path) == 0) return true;
    // Some newlib/filesystem combinations do not replace an existing file.
    // The fallback has a short missing-file window, but never exposes a partial.
    remove(path);
    if (rename(temporary.c_str(), path) == 0) return true;
    remove(temporary.c_str());
    return false;
}

} // namespace ps3
} // namespace movebeat
