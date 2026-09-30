#pragma once

#include "cut_feedback.hpp"
#include "framebuffer.hpp"
#include "movebeat/game.hpp"

namespace movebeat {
namespace ps3 {

// Gameplay scene layers, drawn back to front by the caller.
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
void drawHud(Framebuffer& screen, const movebeat::ScoreState& score,
             unsigned int elapsed_ms, unsigned int duration_ms);

} // namespace ps3
} // namespace movebeat
