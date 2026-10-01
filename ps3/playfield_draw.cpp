#include "playfield_draw.hpp"

#include <algorithm>
#include <cmath>

#include "note_cube.hpp"
#include "projection.hpp"
#include "text_draw.hpp"

namespace movebeat {
namespace ps3 {
namespace {

int roundPixel(float value) {
    return static_cast<int>(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

ScreenPoint projectTrackPoint(const movebeat::Vec3& point,
                              const Framebuffer& screen) {
    return projectWorldPoint(point, screen.width(), screen.height());
}

void drawObstacleFace(Framebuffer& screen,
                      const movebeat::Vec3& minimum,
                      const movebeat::Vec3& maximum,
                      float z, u32 color) {
    const ScreenPoint bottom_left = projectTrackPoint(
        movebeat::Vec3(minimum.x, minimum.y, z), screen);
    const ScreenPoint bottom_right = projectTrackPoint(
        movebeat::Vec3(maximum.x, minimum.y, z), screen);
    const ScreenPoint top_left = projectTrackPoint(
        movebeat::Vec3(minimum.x, maximum.y, z), screen);
    const ScreenPoint top_right = projectTrackPoint(
        movebeat::Vec3(maximum.x, maximum.y, z), screen);
    screen.line(bottom_left.x, bottom_left.y, bottom_right.x, bottom_right.y, color, 4);
    screen.line(bottom_right.x, bottom_right.y, top_right.x, top_right.y, color, 4);
    screen.line(top_right.x, top_right.y, top_left.x, top_left.y, color, 4);
    screen.line(top_left.x, top_left.y, bottom_left.x, bottom_left.y, color, 4);
}

// Effect sizes are tuned at 720p and scale with the output height.
float effectScale(const Framebuffer& screen) {
    return screen.height() / 720.0f;
}

u32 withAlpha(u32 color, float alpha) {
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    return (static_cast<u32>(alpha * 255.0f + 0.5f) << 24) |
           (color & 0x00ffffffu);
}

void projectFloat(const movebeat::Vec3& point, const Framebuffer& screen,
                  float& x, float& y) {
    const ScreenPoint projected = projectTrackPoint(point, screen);
    x = projected.x + 0.5f;
    y = projected.y + 0.5f;
}

// Translucent fill of one wall face; edges are drawn separately.
void fillObstacleFace(Framebuffer& screen, const movebeat::Vec3& minimum,
                      const movebeat::Vec3& maximum, float z, u32 color) {
    float x[4];
    float y[4];
    projectFloat(movebeat::Vec3(minimum.x, maximum.y, z), screen, x[0], y[0]);
    projectFloat(movebeat::Vec3(maximum.x, maximum.y, z), screen, x[1], y[1]);
    projectFloat(movebeat::Vec3(maximum.x, minimum.y, z), screen, x[2], y[2]);
    projectFloat(movebeat::Vec3(minimum.x, minimum.y, z), screen, x[3], y[3]);
    const u32 colors[4] = {color, color, color, color};
    screen.blendQuad(x, y, colors, false);
}

void glowObstacleFace(Framebuffer& screen, const movebeat::Vec3& minimum,
                      const movebeat::Vec3& maximum, float z, u32 color) {
    float x[4];
    float y[4];
    projectFloat(movebeat::Vec3(minimum.x, maximum.y, z), screen, x[0], y[0]);
    projectFloat(movebeat::Vec3(maximum.x, maximum.y, z), screen, x[1], y[1]);
    projectFloat(movebeat::Vec3(maximum.x, minimum.y, z), screen, x[2], y[2]);
    projectFloat(movebeat::Vec3(minimum.x, minimum.y, z), screen, x[3], y[3]);
    const float width = 9.0f * effectScale(screen);
    for (int edge = 0; edge < 4; ++edge) {
        const int next = (edge + 1) % 4;
        screen.glowLine(x[edge], y[edge], x[next], y[next], width, color);
    }
}

u32 shadeNote(u32 color, int numerator, int denominator) {
    const unsigned int red = std::min(255u, ((color >> 16) & 255u) * numerator / denominator);
    const unsigned int green = std::min(255u, ((color >> 8) & 255u) * numerator / denominator);
    const unsigned int blue = std::min(255u, (color & 255u) * numerator / denominator);
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

void drawNoteCube(Framebuffer& screen,
                  const NoteCubeMesh& cube, u32 color) {
    const ScreenPoint* v = cube.screen;
    // Project and shade the real camera-facing cube planes; don't draw a
    // rear-face rim that makes the note look like a framed flat square.
    const NoteCubeVisibleFaces faces =
        visibleNoteCubeFaces(cube);
    if (faces.top)
        screen.quadrilateral(v[0].x, v[0].y, v[1].x, v[1].y,
                             v[5].x, v[5].y, v[4].x, v[4].y,
                             shadeNote(color, 3, 4));
    else if (faces.bottom)
        screen.quadrilateral(v[3].x, v[3].y, v[2].x, v[2].y,
                             v[6].x, v[6].y, v[7].x, v[7].y,
                             shadeNote(color, 1, 3));
    if (faces.left)
        screen.quadrilateral(v[4].x, v[4].y, v[0].x, v[0].y,
                             v[3].x, v[3].y, v[7].x, v[7].y,
                             shadeNote(color, 1, 3));
    else if (faces.right)
        screen.quadrilateral(v[1].x, v[1].y, v[5].x, v[5].y,
                             v[6].x, v[6].y, v[2].x, v[2].y,
                             shadeNote(color, 2, 5));
    screen.quadrilateral(v[0].x, v[0].y, v[1].x, v[1].y,
                         v[2].x, v[2].y, v[3].x, v[3].y, color);
    // Face shading carries the depth cue; no border is painted over the front.
}

} // namespace

void drawFloor(Framebuffer& screen, float far_z) {
    if (!screen.effectsEnabled()) return;
    const float ground_y = -0.825f;
    const float outer_x = 1.1f;
    float x[4];
    float y[4];
    projectFloat(movebeat::Vec3(-outer_x, ground_y, far_z), screen, x[0], y[0]);
    projectFloat(movebeat::Vec3(outer_x, ground_y, far_z), screen, x[1], y[1]);
    projectFloat(movebeat::Vec3(outer_x, ground_y, 0.0f), screen, x[2], y[2]);
    projectFloat(movebeat::Vec3(-outer_x, ground_y, 0.0f), screen, x[3], y[3]);
    // A faint blue floor that fades into the distance: one translucent quad.
    const u32 near = 0x5c182850u;
    const u32 far = 0x00182850u;
    const u32 colors[4] = {far, far, near, near};
    screen.blendQuad(x, y, colors, false);
    // Glowing side rails and strike line, the track's only light sources.
    const float width = 7.0f * effectScale(screen);
    screen.glowLine(x[0], y[0], x[3], y[3], width, 0x50406cd0u);
    screen.glowLine(x[1], y[1], x[2], y[2], width, 0x50406cd0u);
    screen.glowLine(x[3], y[3], x[2], y[2], width, 0x38506cc0u);
}

void drawGrid(Framebuffer& screen, float far_z) {
    const u32 grid = 0xff20283cu;
    const float ground_y = -0.825f;
    const float lane_spacing = 0.55f;
    for (int lane = 0; lane <= 4; ++lane) {
        const float x = (lane - 2) * lane_spacing;
        const ScreenPoint near = projectWorldPoint(
            movebeat::Vec3(x, ground_y, 0.0f), screen.width(), screen.height());
        const ScreenPoint far = projectWorldPoint(
            movebeat::Vec3(x, ground_y, far_z), screen.width(), screen.height());
        screen.line(near.x, near.y, far.x, far.y, grid, 2);
    }
    for (int step = 0; step <= 4; ++step) {
        const float z = far_z * step / 4.0f;
        const ScreenPoint left = projectWorldPoint(
            movebeat::Vec3(-2.0f * lane_spacing, ground_y, z),
            screen.width(), screen.height());
        const ScreenPoint right = projectWorldPoint(
            movebeat::Vec3(2.0f * lane_spacing, ground_y, z),
            screen.width(), screen.height());
        const u32 color = step == 4 ? 0xff506080u : grid;
        screen.line(left.x, left.y, right.x, right.y, color, step == 4 ? 3 : 2);
    }
    // A visible far frame marks the finite note spawn plane.
    const float outer_x = 2.0f * lane_spacing;
    const float top_y = 1.1f;
    const ScreenPoint far_top_left = projectWorldPoint(
        movebeat::Vec3(-outer_x, top_y, far_z), screen.width(), screen.height());
    const ScreenPoint far_top_right = projectWorldPoint(
        movebeat::Vec3(outer_x, top_y, far_z), screen.width(), screen.height());
    const ScreenPoint far_bottom_left = projectWorldPoint(
        movebeat::Vec3(-outer_x, ground_y, far_z), screen.width(), screen.height());
    const ScreenPoint far_bottom_right = projectWorldPoint(
        movebeat::Vec3(outer_x, ground_y, far_z), screen.width(), screen.height());
    screen.line(far_bottom_left.x, far_bottom_left.y,
                far_top_left.x, far_top_left.y, 0xff303c58u, 2);
    screen.line(far_top_left.x, far_top_left.y,
                far_top_right.x, far_top_right.y, 0xff303c58u, 2);
    screen.line(far_top_right.x, far_top_right.y,
                far_bottom_right.x, far_bottom_right.y, 0xff303c58u, 2);
}

void drawObstacles(Framebuffer& screen, const movebeat::Game& game,
                   unsigned int now_ms) {
    const float far_z = game.noteSpawnDepth();
    for (std::size_t index = 0; index < game.obstacleCount(); ++index) {
        if (!game.obstaclePending(index)) continue;
        const movebeat::Obstacle& obstacle = game.obstacle(index);
        movebeat::Vec3 minimum = game.obstacleMinimum(obstacle, now_ms);
        movebeat::Vec3 maximum = game.obstacleMaximum(obstacle, now_ms);
        if (maximum.z < 0.0f || minimum.z > far_z) continue;
        if (minimum.z < 0.0f) minimum.z = 0.0f;
        if (maximum.z > far_z) maximum.z = far_z;
        const u32 color = minimum.z <= 0.01f ? 0xffff5088u : 0xffa040d0u;
        // Two translucent layers at most per wall keep fill cost bounded
        // even when a wall covers much of the screen near the player.
        fillObstacleFace(screen, minimum, maximum, maximum.z, 0x24a040d0u);
        fillObstacleFace(screen, minimum, maximum, minimum.z,
                         withAlpha(color, 0.22f));
        glowObstacleFace(screen, minimum, maximum, minimum.z,
                         withAlpha(color, 0.45f));
        drawObstacleFace(screen, minimum, maximum, minimum.z, color);
        drawObstacleFace(screen, minimum, maximum, maximum.z, 0xff603080u);
        const ScreenPoint near_bottom_left = projectTrackPoint(
            movebeat::Vec3(minimum.x, minimum.y, minimum.z), screen);
        const ScreenPoint near_bottom_right = projectTrackPoint(
            movebeat::Vec3(maximum.x, minimum.y, minimum.z), screen);
        const ScreenPoint near_top_left = projectTrackPoint(
            movebeat::Vec3(minimum.x, maximum.y, minimum.z), screen);
        const ScreenPoint near_top_right = projectTrackPoint(
            movebeat::Vec3(maximum.x, maximum.y, minimum.z), screen);
        const ScreenPoint far_bottom_left = projectTrackPoint(
            movebeat::Vec3(minimum.x, minimum.y, maximum.z), screen);
        const ScreenPoint far_bottom_right = projectTrackPoint(
            movebeat::Vec3(maximum.x, minimum.y, maximum.z), screen);
        const ScreenPoint far_top_left = projectTrackPoint(
            movebeat::Vec3(minimum.x, maximum.y, maximum.z), screen);
        const ScreenPoint far_top_right = projectTrackPoint(
            movebeat::Vec3(maximum.x, maximum.y, maximum.z), screen);
        screen.line(near_bottom_left.x, near_bottom_left.y,
                    far_bottom_left.x, far_bottom_left.y, color, 3);
        screen.line(near_bottom_right.x, near_bottom_right.y,
                    far_bottom_right.x, far_bottom_right.y, color, 3);
        screen.line(near_top_left.x, near_top_left.y,
                    far_top_left.x, far_top_left.y, color, 3);
        screen.line(near_top_right.x, near_top_right.y,
                    far_top_right.x, far_top_right.y, color, 3);
    }
}

void drawArcs(Framebuffer& screen, const movebeat::Game& game,
              unsigned int now_ms) {
    const unsigned int segments = 16;
    for (std::size_t index = 0; index < game.arcCount(); ++index) {
        const movebeat::Arc& arc = game.arc(index);
        movebeat::Vec3 previous = game.arcPoint(arc, now_ms, 0.0f);
        const u32 core = arc.hand == movebeat::HAND_LEFT
            ? 0xff70d8ffu : 0xffff6878u;
        const u32 glow = arc.hand == movebeat::HAND_LEFT
            ? 0xff174870u : 0xff701727u;
        for (unsigned int segment = 1; segment <= segments; ++segment) {
            const float progress = static_cast<float>(segment) /
                static_cast<float>(segments);
            const movebeat::Vec3 current = game.arcPoint(arc, now_ms, progress);
            if (previous.z >= -0.8f && previous.z <= game.noteSpawnDepth() &&
                current.z >= -0.8f && current.z <= game.noteSpawnDepth()) {
                const ScreenPoint from = projectTrackPoint(previous, screen);
                const ScreenPoint to = projectTrackPoint(current, screen);
                screen.line(from.x, from.y, to.x, to.y, glow, 7);
                screen.line(from.x, from.y, to.x, to.y, core, 2);
            }
            previous = current;
        }
    }
}

void drawSaber(Framebuffer& screen, const movebeat::SaberPose& saber,
               u32 core, u32 glow) {
    if (!saber.tracked) return;
    const ScreenPoint handle = projectTrackPoint(saber.handle, screen);
    const ScreenPoint tip = projectTrackPoint(saber.tip, screen);
    if (screen.effectsEnabled()) {
        // Additive halo instead of the opaque dark band, then a colored
        // blade with a white-hot center.
        const float scale = effectScale(screen);
        screen.glowLine(handle.x + 0.5f, handle.y + 0.5f,
                        tip.x + 0.5f, tip.y + 0.5f, 20.0f * scale,
                        withAlpha(core, 0.55f));
        screen.line(handle.x, handle.y, tip.x, tip.y, core, 5);
        screen.line(handle.x, handle.y, tip.x, tip.y, 0xfff2f8ffu, 1);
    } else {
        screen.line(handle.x, handle.y, tip.x, tip.y, glow, 15);
        screen.line(handle.x, handle.y, tip.x, tip.y, core, 5);
    }
    screen.circle(handle.x, handle.y, 11, 0xffe8e8f0u);
}

void drawSaberTrail(Framebuffer& screen, const SaberTrail& trail,
                    unsigned int now_ms, u32 color) {
    if (!screen.effectsEnabled() || trail.size() < 2u) return;
    // Swept quads between consecutive blade positions: bright at the tip,
    // transparent toward the blade middle, fading with age. At most
    // kCapacity - 1 quads per saber.
    for (unsigned int index = 0; index + 1u < trail.size(); ++index) {
        const TrailSample& newer = trail.sample(index);
        const TrailSample& older = trail.sample(index + 1u);
        const float newer_fade = SaberTrail::fade(newer, now_ms);
        const float older_fade = SaberTrail::fade(older, now_ms);
        if (newer_fade <= 0.0f) break;
        float x[4];
        float y[4];
        projectFloat(newer.base, screen, x[0], y[0]);
        projectFloat(newer.tip, screen, x[1], y[1]);
        projectFloat(older.tip, screen, x[2], y[2]);
        projectFloat(older.base, screen, x[3], y[3]);
        const u32 clear = color & 0x00ffffffu;
        const u32 colors[4] = {
            clear, withAlpha(color, 0.6f * newer_fade),
            withAlpha(color, 0.6f * older_fade), clear
        };
        screen.blendQuad(x, y, colors, true);
    }
}

void drawNotes(Framebuffer& screen, const movebeat::Game& game,
               unsigned int now_ms, bool draw_software_faces) {
    const float far_z = game.noteSpawnDepth();
    // The chart is time-ordered, so draw distant notes first for correct overlap.
    for (std::size_t index = game.noteCount(); index-- > 0;) {
        if (!game.notePending(index)) continue;
        const movebeat::Note& note = game.note(index);
        const movebeat::Vec3 position = game.notePosition(note, now_ms);
        if (position.z < -0.8f || position.z > far_z) continue;
        float depth_scale = 1.0f - position.z / kProjectionDepth;
        if (depth_scale < 0.0f) depth_scale = 0.0f;
        if (depth_scale > 1.0f) depth_scale = 1.0f;
        const ScreenPoint projected = projectTrackPoint(position, screen);
        int x = projected.x;
        int y = projected.y;
        int size = std::max(8, static_cast<int>(
            screen.height() * 0.30f * 0.32f * depth_scale));
        const u32 color = note.kind == movebeat::NOTE_BOMB ? 0xff303038u :
            (note.hand == movebeat::HAND_LEFT ? 0xff2098e8u : 0xffe83048u);
        if (note.kind == movebeat::NOTE_BOMB) {
            const float bomb_front_z = position.z - 0.16f;
        const float bomb_front_depth =
                toNoteCameraSpace(movebeat::Vec3(
                    position.x, position.y, bomb_front_z)).depth;
            screen.noteCircle(x, y, size / 2, 0xff60606cu, bomb_front_depth);
            screen.noteCircle(x, y, size / 2 - 2, color, bomb_front_depth);
            screen.noteCircle(x, y, size / 5, 0xffd0d0d8u, bomb_front_depth);
            continue;
        }
        const float side = note.kind == movebeat::NOTE_CHAIN ? 0.16f : 0.32f;
        const float depth = gameplayNoteDepth(side);
        const float face_z = position.z - depth * 0.5f;
        const float face_depth = toNoteCameraSpace(
            movebeat::Vec3(position.x, position.y, face_z)).depth;
        const NoteCubeMesh cube = makeNoteCube(
            position, side, screen.width(), screen.height(), depth);
        if (draw_software_faces) drawNoteCube(screen, cube, color);
        x = cube.front_center.x;
        y = cube.front_center.y;
        const int face_width = cube.screen[1].x - cube.screen[0].x;
        const int face_height = cube.screen[3].y - cube.screen[0].y;
        size = std::max(4, std::min(face_width, face_height));
        if (note.kind == movebeat::NOTE_CHAIN) {
            screen.noteCircle(x, y, size / 3 + 1, 0xfff0f0f0u, face_depth);
        } else if (note.direction == movebeat::CUT_ANY) {
            screen.noteCircle(x, y, size / 8 + 1, 0xfff0f0f0u, face_depth);
        } else {
            int dx = 0;
            int dy = 0;
            switch (note.direction) {
                case movebeat::CUT_UP: dy = -1; break;
                case movebeat::CUT_DOWN: dy = 1; break;
                case movebeat::CUT_LEFT: dx = -1; break;
                case movebeat::CUT_RIGHT: dx = 1; break;
                case movebeat::CUT_UP_LEFT: dx = -1; dy = -1; break;
                case movebeat::CUT_UP_RIGHT: dx = 1; dy = -1; break;
                case movebeat::CUT_DOWN_LEFT: dx = -1; dy = 1; break;
                case movebeat::CUT_DOWN_RIGHT: dx = 1; dy = 1; break;
                case movebeat::CUT_ANY: break;
            }
            const float radians = std::fmod(note.angle_offset_degrees, 360.0f) *
                                  0.01745329251994329577f;
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            float direction_x = dx * cosine + dy * sine;
            float direction_y = -dx * sine + dy * cosine;
            const float magnitude = std::sqrt(direction_x * direction_x +
                                              direction_y * direction_y);
            if (magnitude > 0.0f) {
                direction_x /= magnitude;
                direction_y /= magnitude;
            }
            const int arrow_length = size / 3;
            const int tail_x = roundPixel(
                x - direction_x * arrow_length / 2.0f);
            const int tail_y = roundPixel(
                y - direction_y * arrow_length / 2.0f);
            const int head_x = roundPixel(
                x + direction_x * arrow_length / 2.0f);
            const int head_y = roundPixel(
                y + direction_y * arrow_length / 2.0f);
            const float side_x = -direction_y;
            const float side_y = direction_x;
            const int wing = size / 7 + 1;
            screen.noteLine(tail_x, tail_y, head_x, head_y,
                        0xfff0f0f0u, 3, face_depth);
            screen.noteLine(head_x, head_y,
                        roundPixel(head_x - direction_x * wing + side_x * wing),
                        roundPixel(head_y - direction_y * wing + side_y * wing),
                        0xfff0f0f0u, 3, face_depth);
            screen.noteLine(head_x, head_y,
                        roundPixel(head_x - direction_x * wing - side_x * wing),
                        roundPixel(head_y - direction_y * wing - side_y * wing),
                        0xfff0f0f0u, 3, face_depth);
        }
    }
}

void drawCutFeedback(Framebuffer& screen,
                     const CutFeedback& feedback,
                     unsigned int now_ms) {
    static const int rays[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
    };
    for (unsigned int index = 0;
         index < CutFeedback::MAX_BURSTS; ++index) {
        if (!feedback.visible(index, now_ms)) continue;
        const CutBurst& burst = feedback.burst(index);
        const unsigned int age = now_ms - burst.started_ms;
        const float progress = static_cast<float>(age) /
            CutFeedback::DURATION_MS;
        const float outer = 0.06f + progress * 0.30f;
        const float inner = outer * 0.52f;
        const float z = burst.position.z - progress * 0.08f;
        const u32 base = burst.hand == movebeat::HAND_LEFT
            ? 0xff70d8ffu : 0xffff6878u;
        const u32 color = shadeNote(base, 10 - static_cast<int>(age * 7u /
            CutFeedback::DURATION_MS), 10);
        if (screen.effectsEnabled()) {
            // The cut happens right at the blade, whose own halo is bright,
            // so the flash must pop: it starts at about the cube's size at
            // full strength, expands quickly and fades quadratically.
            // A white core marks the first few frames. Two fans per cut.
            // Hold full strength for the first quarter (~45 ms, three
            // frames), then fade; a fade from the very first frame read as
            // a dim smudge by the time the eye catches it.
            const float remaining = progress < 0.25f
                ? 1.0f : (1.0f - progress) / 0.75f;
            const float flash_meters = 0.30f + 0.35f * progress;
            float center_x = 0.0f, center_y = 0.0f, edge_x = 0.0f, edge_y = 0.0f;
            projectFloat(movebeat::Vec3(burst.position.x, burst.position.y, z),
                         screen, center_x, center_y);
            projectFloat(movebeat::Vec3(burst.position.x + flash_meters,
                                        burst.position.y, z),
                         screen, edge_x, edge_y);
            const float radius = std::fabs(edge_x - center_x) + 6.0f;
            screen.glowDisc(center_x, center_y, radius,
                            withAlpha(base, remaining * remaining));
            if (progress < 0.6f) {
                const float core = progress < 0.3f
                    ? 1.0f : (0.6f - progress) / 0.3f;
                screen.glowDisc(center_x, center_y, radius * 0.4f,
                                withAlpha(0xffffffffu, core));
            }
        }
        for (int ray = 0; ray < 8; ++ray) {
            const float dx = rays[ray][0] * 0.65f;
            const float dy = static_cast<float>(rays[ray][1]);
            const ScreenPoint from = projectTrackPoint(movebeat::Vec3(
                burst.position.x + dx * inner,
                burst.position.y + dy * inner, z), screen);
            const ScreenPoint to = projectTrackPoint(movebeat::Vec3(
                burst.position.x + dx * outer,
                burst.position.y + dy * outer, z), screen);
            screen.line(from.x, from.y, to.x, to.y, color, 2);
        }
        if (age < 55u) {
            const ScreenPoint center = projectTrackPoint(burst.position, screen);
            screen.circle(center.x, center.y, 3, 0xfff5f6ffu);
        }
    }
}

void drawHud(Framebuffer& screen,
             const movebeat::ScoreState& score,
             unsigned int elapsed_ms, unsigned int duration_ms) {
    const int panel_width = screen.width() / 4;
    const int panel_height = screen.height() / 10;
    const int panel_y = screen.height() / 11;
    const int edge = screen.width() / 48;
    const int label_scale = std::max(2, screen.height() / 360);
    const int value_scale = std::max(3, screen.height() / 145);
    const int label_y = panel_y + screen.height() / 90;
    const int value_y = panel_y + screen.height() / 30;
    const u32 panel = 0xff101827u;
    const u32 label = 0xff9cb3d3u;

    screen.rectangle(edge, panel_y, panel_width, panel_height, panel);
    screen.rectangle(screen.width() - edge - panel_width, panel_y,
                     panel_width, panel_height, panel);
    drawMenuText(screen, "SCORE", edge + screen.width() / 80,
                 label_y, label_scale, label);
    drawNumber(screen, score.score, edge + panel_width / 2,
               value_y, value_scale,
               score.failed ? 0xffff5060u : 0xfff0d050u);
    const int combo_x = screen.width() - edge - panel_width;
    drawMenuText(screen, "COMBO", combo_x + screen.width() / 80,
                 label_y, label_scale, label);
    drawNumber(screen, score.combo, combo_x + panel_width / 2,
               value_y, value_scale, 0xff70d8ffu);

    const int bar_x = screen.width() / 4;
    const int bar_width = screen.width() / 2;
    const int bar_height = std::max(12, screen.height() / 60);
    const int bar_y = screen.height() - screen.height() / 28;
    const int energy_y = bar_y - std::max(8, screen.height() / 90);
    screen.rectangle(bar_x, energy_y, bar_width,
                     std::max(4, screen.height() / 180), 0xff202838u);
    const u32 energy_color = score.energy > 50 ? 0xff40d870u :
        (score.energy > 20 ? 0xffffb030u : 0xffe83848u);
    screen.rectangle(bar_x,
                     energy_y,
                     bar_width * static_cast<int>(score.energy) / 100,
                     std::max(4, screen.height() / 180), energy_color);
    drawMenuText(screen, "LIFE", bar_x + bar_width + screen.width() / 96,
                 energy_y - label_scale * 7 - screen.height() / 180,
                 label_scale, label);

    screen.rectangle(bar_x, bar_y, bar_width, bar_height, 0xff202838u);
    if (duration_ms > 0) {
        const unsigned int clamped_time = std::min(elapsed_ms, duration_ms);
        const int progress_width = static_cast<int>(
            static_cast<unsigned long long>(bar_width) * clamped_time /
            duration_ms);
        screen.rectangle(bar_x, bar_y, progress_width, bar_height,
                         0xff40b8ffu);
        const unsigned int remaining_ms = duration_ms - clamped_time;
        drawMenuText(screen, "LEFT", screen.width() / 12,
                     bar_y - screen.height() / 48, label_scale, label);
        drawTime(screen, remaining_ms, screen.width() / 6,
                 bar_y - screen.height() / 48, label_scale,
                 0xfff0f0f0u);
    }
}

} // namespace ps3
} // namespace movebeat
