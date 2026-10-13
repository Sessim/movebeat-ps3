#pragma once

#include "cut_feedback.hpp"
#include "framebuffer.hpp"
#include "saber_trail.hpp"
#include "movebeat/game.hpp"

namespace movebeat {
namespace ps3 {

// Gameplay scene layers, drawn back to front by the caller. Effects (floor,
// translucent walls, glow, trails, flashes) appear only when
// screen.effectsEnabled(); otherwise the scene looks as before.
// Sky, drifting neon frames and the portal notes emerge from; beat_pulse is
// 1 on the beat and decays before the next one. Draw first.
void drawBackground(Framebuffer& screen, float far_z, unsigned int now_ms,
                    float beat_pulse);
void drawFloor(Framebuffer& screen, float far_z);
void drawGrid(Framebuffer& screen, float far_z);
void drawObstacles(Framebuffer& screen, const movebeat::Game& game,
                   unsigned int now_ms);
void drawArcs(Framebuffer& screen, const movebeat::Game& game,
              unsigned int now_ms);
// Arrows, dots and bombs; cube faces only when draw_software_faces is set
// (the solid-note pass already drew them otherwise).
void drawNotes(Framebuffer& screen, const movebeat::Game& game,
               unsigned int now_ms, bool draw_software_faces);
void drawCutFeedback(Framebuffer& screen, const CutFeedback& feedback,
                     unsigned int now_ms);
void drawSaber(Framebuffer& screen, const movebeat::SaberPose& saber,
               u32 core, u32 glow);
void drawSaberTrail(Framebuffer& screen, const SaberTrail& trail,
                    unsigned int now_ms, u32 color);
// `combo_flash` (1 → 0) fades the red flash of a just broken combo. The
// energy bar is shown only when running out of energy fails the level.
void drawHud(Framebuffer& screen, const movebeat::ScoreState& score,
             unsigned int elapsed_ms, unsigned int duration_ms,
             float combo_flash = 0.0f, bool show_energy = true);

} // namespace ps3
} // namespace movebeat
