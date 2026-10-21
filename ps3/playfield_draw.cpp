#include "playfield_draw.hpp"

#include <algorithm>
#include <cmath>

#include "note_cube.hpp"
#include "note_spawn.hpp"
#include "projection.hpp"
#include "saber_model.hpp"
#include "software_note_renderer.hpp"
#include "stage_layout.hpp"
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

// Screen pixels per world meter at a point, for sizing round objects.
float pixelsPerMeter(const movebeat::Vec3& point, const Framebuffer& screen) {
    float depth = toCameraSpace(point).depth;
    if (depth < 0.1f) depth = 0.1f;
    return screen.height() * kProjectionFocalHeight * kProjectionDepth / depth;
}

void quad(Framebuffer& screen, const float (&x)[4], const float (&y)[4],
          u32 color) {
    screen.quadrilateral(roundPixel(x[0]), roundPixel(y[0]),
                         roundPixel(x[1]), roundPixel(y[1]),
                         roundPixel(x[2]), roundPixel(y[2]),
                         roundPixel(x[3]), roundPixel(y[3]), color);
}

// Black cylinder standing in for the PS Move in the hand: a rim-lit
// silhouette, a matte body, a lengthwise highlight, two grip rings and a
// bottom cap. Opaque, so it looks the same on both renderers.
void drawHilt(Framebuffer& screen, const SaberModel& model) {
    float bx, by, tx, ty;
    projectFloat(model.hilt_base, screen, bx, by);
    projectFloat(model.hilt_top, screen, tx, ty);
    const float base_radius = std::max(2.0f,
        kHiltRadius * pixelsPerMeter(model.hilt_base, screen));
    const float top_radius = std::max(2.0f,
        kHiltRadius * pixelsPerMeter(model.hilt_top, screen));
    const u32 rim = 0xff3c4250u;
    const u32 body = 0xff0d0e12u;
    const u32 cap = 0xff17181du;
    float dx = tx - bx;
    float dy = ty - by;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length >= 1.0f) {
        dx /= length;
        dy /= length;
        // Light from above: the highlight goes on the side facing up.
        float nx = -dy;
        float ny = dx;
        if (ny > 0.0f) {
            nx = -nx;
            ny = -ny;
        }
        const float edge = 1.5f;
        const float outline_x[4] = {
            bx + nx * (base_radius + edge), tx + nx * (top_radius + edge),
            tx - nx * (top_radius + edge), bx - nx * (base_radius + edge)
        };
        const float outline_y[4] = {
            by + ny * (base_radius + edge), ty + ny * (top_radius + edge),
            ty - ny * (top_radius + edge), by - ny * (base_radius + edge)
        };
        screen.circle(roundPixel(bx), roundPixel(by),
                      roundPixel(base_radius + edge), rim);
        quad(screen, outline_x, outline_y, rim);
        const float body_x[4] = {
            bx + nx * base_radius, tx + nx * top_radius,
            tx - nx * top_radius, bx - nx * base_radius
        };
        const float body_y[4] = {
            by + ny * base_radius, ty + ny * top_radius,
            ty - ny * top_radius, by - ny * base_radius
        };
        quad(screen, body_x, body_y, body);
        const float stripe_x[4] = {
            bx + nx * base_radius * 0.65f, tx + nx * top_radius * 0.65f,
            tx + nx * top_radius * 0.35f, bx + nx * base_radius * 0.35f
        };
        const float stripe_y[4] = {
            by + ny * base_radius * 0.65f, ty + ny * top_radius * 0.65f,
            ty + ny * top_radius * 0.35f, by + ny * base_radius * 0.35f
        };
        quad(screen, stripe_x, stripe_y, 0xff4a505eu);
        for (int ring = 1; ring <= 2; ++ring) {
            const float at = ring / 3.0f;
            const float cx = bx + (tx - bx) * at;
            const float cy = by + (ty - by) * at;
            const float radius = base_radius + (top_radius - base_radius) * at;
            const float half = 1.0f;
            const float ring_x[4] = {
                cx - dx * half + nx * radius, cx + dx * half + nx * radius,
                cx + dx * half - nx * radius, cx - dx * half - nx * radius
            };
            const float ring_y[4] = {
                cy - dy * half + ny * radius, cy + dy * half + ny * radius,
                cy + dy * half - ny * radius, cy - dy * half - ny * radius
            };
            quad(screen, ring_x, ring_y, 0xff272a32u);
        }
    } else {
        // Pointing straight at the camera: only the round end is visible.
        screen.circle(roundPixel(bx), roundPixel(by),
                      roundPixel(base_radius + 1.5f), rim);
    }
    screen.circle(roundPixel(bx), roundPixel(by), roundPixel(base_radius), cap);
}

// A "^" chevron pointing along (dx, dy) (unit), centered at (cx, cy) in
// pixel-edge coordinates: arms reach `reach` sideways, strokes are
// `thickness` wide, and the strokes meet in a mitered point.
void drawChevron(Framebuffer& screen, float cx, float cy, float dx, float dy,
                 float reach, float thickness, u32 color, float plane_z) {
    const float sx = -dy;
    const float sy = dx;
    const float apex_x = cx + dx * reach * 0.35f;
    const float apex_y = cy + dy * reach * 0.35f;
    const float half = thickness * 0.5f;
    for (int arm = -1; arm <= 1; arm += 2) {
        const float end_x = apex_x - dx * reach * 0.7f + sx * reach * arm;
        const float end_y = apex_y - dy * reach * 0.7f + sy * reach * arm;
        float ux = end_x - apex_x;
        float uy = end_y - apex_y;
        const float length = std::sqrt(ux * ux + uy * uy);
        if (!(length > 0.0f)) return;
        ux /= length;
        uy /= length;
        // Sine of the angle between the arm and the axis; both outer and
        // inner edges cross the axis half a stroke / sine from the apex.
        const float sine = std::fabs(ux * dy - uy * dx);
        if (sine < 0.2f) return;
        const float miter = half / sine;
        // Arm normal on the side the chevron points to.
        float nx = -uy;
        float ny = ux;
        if (nx * dx + ny * dy < 0.0f) {
            nx = -nx;
            ny = -ny;
        }
        const float outer_x = apex_x + dx * miter;
        const float outer_y = apex_y + dy * miter;
        const float inner_x = apex_x - dx * miter;
        const float inner_y = apex_y - dy * miter;
        const float ax[3] = {outer_x, end_x + nx * half, end_x - nx * half};
        const float ay[3] = {outer_y, end_y + ny * half, end_y - ny * half};
        const float bx[3] = {outer_x, end_x - nx * half, inner_x};
        const float by[3] = {outer_y, end_y - ny * half, inner_y};
        screen.noteTriangle(ax, ay, color, plane_z);
        screen.noteTriangle(bx, by, color, plane_z);
    }
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

void drawTunnelRings(Framebuffer& screen, float far_z, unsigned int now_ms,
                     float pulse, bool behind_portal) {
    const float scale = effectScale(screen);
    for (unsigned int index = 0; index < kTunnelRingCount; ++index) {
        const TunnelRing ring = tunnelRing(index, now_ms, far_z);
        if (ring.alpha <= 0.01f || ring.z <= kTunnelVanishZ) continue;
        if ((ring.z > far_z) != behind_portal) continue;
        float x[4];
        float y[4];
        projectFloat(movebeat::Vec3(-kTunnelHalfWidth, kTunnelBottom, ring.z),
                     screen, x[0], y[0]);
        projectFloat(movebeat::Vec3(kTunnelHalfWidth, kTunnelBottom, ring.z),
                     screen, x[1], y[1]);
        projectFloat(movebeat::Vec3(kTunnelHalfWidth, kTunnelTop, ring.z),
                     screen, x[2], y[2]);
        projectFloat(movebeat::Vec3(-kTunnelHalfWidth, kTunnelTop, ring.z),
                     screen, x[3], y[3]);
        const u32 hue = index % 2u ? 0xff4060ffu : 0xff9040ffu;
        const float strength = ring.alpha * (0.35f + 0.45f * pulse);
        for (int edge = 0; edge < 4; ++edge) {
            const int next = (edge + 1) % 4;
            screen.glowLine(x[edge], y[edge], x[next], y[next], 7.0f * scale,
                            withAlpha(hue, strength));
            screen.glowLine(x[edge], y[edge], x[next], y[next], 1.5f * scale,
                            withAlpha(0xffd8d0ffu, strength));
        }
    }
}

} // namespace

void drawBackground(Framebuffer& screen, float far_z, unsigned int now_ms,
                    float beat_pulse) {
    if (!screen.effectsEnabled()) return;
    const float width = static_cast<float>(screen.width());
    const float height = static_cast<float>(screen.height());
    // Sky: one full-screen opaque layer, brightest at the portal's horizon.
    float horizon_x = 0.0f, horizon = 0.0f;
    projectFloat(movebeat::Vec3(0.0f, 0.14f, far_z), screen, horizon_x, horizon);
    if (horizon < 1.0f) horizon = 1.0f;
    if (horizon > height - 1.0f) horizon = height - 1.0f;
    const u32 zenith = 0xff06061au;
    const u32 glow = 0xff161436u;
    const u32 ground = 0xff05050fu;
    const float sky_x[4] = {0.0f, width, width, 0.0f};
    const float sky_y[4] = {0.0f, 0.0f, horizon, horizon};
    const u32 sky_colors[4] = {zenith, zenith, glow, glow};
    screen.gradientQuad(sky_x, sky_y, sky_colors);
    const float low_y[4] = {horizon, horizon, height, height};
    const u32 low_colors[4] = {glow, glow, ground, ground};
    screen.gradientQuad(sky_x, low_y, low_colors);

    // Frames beyond the portal show dimmed through it.
    drawTunnelRings(screen, far_z, now_ms, beat_pulse, true);

    // The portal in the far wall that notes fly out of.
    const float ground_y = kPortalBottom;
    const float outer_x = kPortalHalfWidth;
    const float top_y = kPortalTop;
    float x[4];
    float y[4];
    projectFloat(movebeat::Vec3(-outer_x, ground_y, far_z), screen, x[0], y[0]);
    projectFloat(movebeat::Vec3(outer_x, ground_y, far_z), screen, x[1], y[1]);
    projectFloat(movebeat::Vec3(outer_x, top_y, far_z), screen, x[2], y[2]);
    projectFloat(movebeat::Vec3(-outer_x, top_y, far_z), screen, x[3], y[3]);
    const u32 veil = 0xa8070616u;
    const u32 veil_colors[4] = {veil, veil, veil, veil};
    screen.blendQuad(x, y, veil_colors, false);
    const float center_x = (x[0] + x[1] + x[2] + x[3]) * 0.25f;
    const float center_y = (y[0] + y[1] + y[2] + y[3]) * 0.25f;
    screen.glowDisc(center_x, center_y, (x[1] - x[0]) * 0.55f,
                    withAlpha(0xff6040c0u, 0.25f + 0.35f * beat_pulse));
    const float scale = effectScale(screen);
    const u32 rim = withAlpha(0xff8060f0u, 0.5f + 0.4f * beat_pulse);
    for (int edge = 0; edge < 4; ++edge) {
        const int next = (edge + 1) % 4;
        screen.glowLine(x[edge], y[edge], x[next], y[next], 10.0f * scale, rim);
    }

    drawTunnelRings(screen, far_z, now_ms, beat_pulse, false);
}

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
        movebeat::Vec3 previous =
            visualArcPoint(game, game.arcPoint(arc, now_ms, 0.0f));
        const u32 core = arc.hand == movebeat::HAND_LEFT
            ? 0xff70d8ffu : 0xffff6878u;
        const u32 glow = arc.hand == movebeat::HAND_LEFT
            ? 0xff174870u : 0xff701727u;
        // Followed by its blade, the arc lights up as a neon tube.
        const float lit = screen.effectsEnabled() ? game.arcGlow(index, now_ms) : 0.0f;
        for (unsigned int segment = 1; segment <= segments; ++segment) {
            const float progress = static_cast<float>(segment) /
                static_cast<float>(segments);
            const movebeat::Vec3 current =
                visualArcPoint(game, game.arcPoint(arc, now_ms, progress));
            if (previous.z >= -0.8f && previous.z <= game.noteSpawnDepth() &&
                current.z >= -0.8f && current.z <= game.noteSpawnDepth()) {
                const ScreenPoint from = projectTrackPoint(previous, screen);
                const ScreenPoint to = projectTrackPoint(current, screen);
                if (lit > 0.0f) {
                    screen.glowLine(from.x + 0.5f, from.y + 0.5f, to.x + 0.5f, to.y + 0.5f,
                                    22.0f * effectScale(screen), withAlpha(core, 0.8f * lit));
                    screen.line(from.x, from.y, to.x, to.y, core, 6);
                    screen.line(from.x, from.y, to.x, to.y, 0xfff2f8ffu, 2);
                } else {
                    screen.line(from.x, from.y, to.x, to.y, glow, 7);
                    screen.line(from.x, from.y, to.x, to.y, core, 2);
                }
            }
            previous = current;
        }
    }
}

void drawSaber(Framebuffer& screen, const movebeat::SaberPose& saber,
               u32 core, u32 glow) {
    const SaberModel model = saberModel(saber);
    if (!model.valid) return;
    const ScreenPoint emitter = projectTrackPoint(model.emitter, screen);
    const ScreenPoint tip = projectTrackPoint(model.blade_tip, screen);
    if (screen.effectsEnabled()) {
        // Additive halo instead of the opaque dark band, then a colored
        // blade with a white-hot center.
        const float scale = effectScale(screen);
        screen.glowLine(emitter.x + 0.5f, emitter.y + 0.5f,
                        tip.x + 0.5f, tip.y + 0.5f, 30.0f * scale,
                        withAlpha(core, 0.55f));
        screen.line(emitter.x, emitter.y, tip.x, tip.y, core, 8);
        screen.line(emitter.x, emitter.y, tip.x, tip.y, 0xfff2f8ffu, 2);
    } else {
        screen.line(emitter.x, emitter.y, tip.x, tip.y, glow, 22);
        screen.line(emitter.x, emitter.y, tip.x, tip.y, core, 8);
    }
    drawHilt(screen, model);
    // The Move's sphere glows in the saber's color at the blade's root.
    const int sphere = std::max(3, roundPixel(
        kEmitterRadius * pixelsPerMeter(model.emitter, screen)));
    if (screen.effectsEnabled())
        screen.glowDisc(emitter.x + 0.5f, emitter.y + 0.5f, sphere * 3.0f,
                        withAlpha(core, 0.6f));
    screen.circle(emitter.x, emitter.y, sphere, core);
    screen.circle(emitter.x - sphere / 3, emitter.y - sphere / 3,
                  std::max(1, sphere / 3), 0xfff2f8ffu);
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
        const NoteArrival arrival = visualNote(game, note, now_ms);
        const movebeat::Vec3 position = arrival.position;
        if (position.z < -0.8f || position.z > far_z) continue;
        float depth_scale = arrival.scale * (1.0f - position.z / kProjectionDepth);
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
            screen.noteCircle(x, y, size / 2, 0xff60606cu, bomb_front_z);
            screen.noteCircle(x, y, size / 2 - 2, color, bomb_front_z);
            screen.noteCircle(x, y, size / 5, 0xffd0d0d8u, bomb_front_z);
            continue;
        }
        const float side = (note.kind == movebeat::NOTE_CHAIN ? 0.16f : 0.32f) *
                           arrival.scale;
        const float depth = gameplayNoteDepth(side);
        // Glyphs lie on the front plane; the rounded body bulges only
        // behind it, so they always stay on top of their own note.
        const float face_z = position.z - depth * 0.5f;
        const NoteCubeMesh cube = makeNoteCube(
            position, side, screen.width(), screen.height(), depth);
        if (draw_software_faces) drawNoteCube(screen, cube, color);
        x = cube.front_center.x;
        y = cube.front_center.y;
        const int face_width = cube.screen[1].x - cube.screen[0].x;
        const int face_height = cube.screen[3].y - cube.screen[0].y;
        size = std::max(4, std::min(face_width, face_height));
        if (note.kind == movebeat::NOTE_CHAIN) {
            screen.noteCircle(x, y, size / 3 + 1, 0xfff0f0f0u, face_z);
        } else if (note.direction == movebeat::CUT_ANY) {
            screen.noteCircle(x, y, size / 8 + 1, 0xfff0f0f0u, face_z);
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
            // A plain wide chevron "^" pointing in the cut direction, like
            // the First Steps library icon: no stem, thick strokes, centered
            // on the face.
            // Filled, so the two strokes meet in a clean mitered point
            // instead of overlapping square caps: four triangles.
            drawChevron(screen, x + 0.5f, y + 0.5f, direction_x, direction_y,
                        size * 0.30f, std::max(3.0f, size / 9.0f),
                        0xfff0f0f0u, face_z);
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
    // Glow where the blade went through: a slash streak along the swing at
    // the cut, and a halo on each half's cut face while the halves fly.
    for (unsigned int index = 0;
         screen.effectsEnabled() && index < CutFeedback::MAX_BURSTS; ++index) {
        if (!feedback.piecesVisible(index, now_ms)) continue;
        const CutBurst& burst = feedback.burst(index);
        const unsigned int age = now_ms - burst.started_ms;
        const u32 base = burst.hand == movebeat::HAND_LEFT
            ? 0xff70d8ffu : 0xffff6878u;
        const float side = burst.kind == movebeat::NOTE_CHAIN ? 0.16f : 0.32f;
        const float slash_ms = 250.0f;
        if (age < slash_ms) {
            const float fade = 1.0f - age / slash_ms;
            const movebeat::Vec3 reach = burst.swing * (side * (0.9f + 0.6f * (1.0f - fade)));
            float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
            projectFloat(burst.position - reach, screen, x0, y0);
            projectFloat(burst.position + reach, screen, x1, y1);
            const float scale = pixelsPerMeter(burst.position, screen);
            screen.glowLine(x0, y0, x1, y1, scale * 0.07f * fade + 3.0f,
                            withAlpha(base, fade));
            if (age < 160u)
                screen.glowLine(x0, y0, x1, y1, scale * 0.018f + 1.5f,
                                withAlpha(0xffffffffu, 1.0f - age / 160.0f));
        }
        CutPiece pieces[2];
        if (!cutPieces(burst, now_ms, side, pieces)) continue;
        const RoundedNoteTable& table = roundedNoteTable();
        const int last = kRoundedNoteGridPoints - 1;
        for (int half = 0; half < 2; ++half) {
            const CutPiece& piece = pieces[half];
            const int face = piece.cut_face_positive ? 1 : 0;
            const movebeat::Vec3 face_center = piece.center +
                piece.axis[0] * (piece.cut_face_positive ? 1.0f : -1.0f);
            float x = 0.0f, y = 0.0f;
            projectFloat(face_center, screen, x, y);
            const float scale = pixelsPerMeter(face_center, screen);
            const float extent = std::sqrt(movebeat::lengthSquared(piece.axis[1]));
            screen.glowDisc(x, y, extent * scale * 1.6f + 4.0f,
                            withAlpha(base, 0.4f * piece.opacity));
            // Neon along the rim of the cut: the border of the cut face's
            // grid, walked once around.
            float rim_x[4 * kRoundedNoteGridPoints], rim_y[4 * kRoundedNoteGridPoints];
            int rim = 0;
            for (int step = 0; step < 4 * last; ++step) {
                int row = 0, column = 0;
                if (step < last) { row = 0; column = step; }
                else if (step < 2 * last) { row = step - last; column = last; }
                else if (step < 3 * last) { row = last; column = 3 * last - step; }
                else { row = 4 * last - step; column = 0; }
                const movebeat::Vec3& q = table.point[face][row][column];
                projectFloat(piece.center + piece.axis[0] * q.x + piece.axis[1] * q.y +
                             piece.axis[2] * q.z, screen, rim_x[rim], rim_y[rim]);
                ++rim;
            }
            const u32 neon = withAlpha(base, piece.opacity);
            const u32 core = withAlpha(0xffffffffu, 0.8f * piece.opacity);
            for (int point = 0; point < rim; ++point) {
                const int next = (point + 1) % rim;
                screen.glowLine(rim_x[point], rim_y[point], rim_x[next], rim_y[next],
                                scale * 0.014f + 1.5f, neon);
                screen.glowLine(rim_x[point], rim_y[point], rim_x[next], rim_y[next],
                                scale * 0.004f + 0.8f, core);
            }
        }
    }
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
             unsigned int elapsed_ms, unsigned int duration_ms,
             float combo_flash, bool show_energy) {
    if (combo_flash > 0.0f) {
        // A red glow from the screen edges, fading inward.
        const u32 red = withAlpha(0xffff2030u, 0.55f * combo_flash);
        const u32 none = 0x00ff2030u;
        const float w = static_cast<float>(screen.width());
        const float h = static_cast<float>(screen.height());
        const float band = h / 6.0f;
        if (screen.effectsEnabled()) {
            const float top_x[4] = {0.0f, w, w, 0.0f};
            const float top_y[4] = {0.0f, 0.0f, band, band};
            const u32 top_c[4] = {red, red, none, none};
            screen.blendQuad(top_x, top_y, top_c, false);
            const float bottom_y[4] = {h - band, h - band, h, h};
            const u32 bottom_c[4] = {none, none, red, red};
            screen.blendQuad(top_x, bottom_y, bottom_c, false);
            const float left_x[4] = {0.0f, band, band, 0.0f};
            const float side_y[4] = {0.0f, 0.0f, h, h};
            const u32 left_c[4] = {red, none, none, red};
            screen.blendQuad(left_x, side_y, left_c, false);
            const float right_x[4] = {w - band, w, w, w - band};
            const u32 right_c[4] = {none, red, red, none};
            screen.blendQuad(right_x, side_y, right_c, false);
        } else {
            const int edge = std::max(4, static_cast<int>(band / 8.0f));
            const u32 solid = 0xffe02838u;
            screen.rectangle(0, 0, screen.width(), edge, solid);
            screen.rectangle(0, screen.height() - edge, screen.width(), edge, solid);
            screen.rectangle(0, 0, edge, screen.height(), solid);
            screen.rectangle(screen.width() - edge, 0, edge, screen.height(), solid);
        }
    }
    const int panel_width = screen.width() / 4;
    const int panel_height = screen.height() / 10;
    const int panel_y = screen.height() / 11;
    const int edge = screen.width() / 48;
    const int label_scale = std::max(2, screen.height() / 360);
    const int value_scale = std::max(3, screen.height() / 145);
    // The numbers alone, in the middle of their panels.
    const int value_y = panel_y + (panel_height - value_scale * 5) / 2;
    const u32 panel = 0xff101827u;
    const u32 label = 0xff9cb3d3u;

    if (screen.effectsEnabled()) {
        // Translucent panels: densest at the screen edge, fading to nothing
        // toward the playfield so the track stays visible behind them.
        const float top = static_cast<float>(panel_y);
        const float bottom = static_cast<float>(panel_y + panel_height);
        const u32 dense = withAlpha(panel, 0.78f);
        const u32 clear = panel & 0x00ffffffu;
        const float left_x0 = static_cast<float>(edge);
        const float left_x1 = static_cast<float>(edge + panel_width);
        const float left_x[4] = {left_x0, left_x1, left_x1, left_x0};
        const float y[4] = {top, top, bottom, bottom};
        const u32 left_colors[4] = {dense, clear, clear, dense};
        screen.blendQuad(left_x, y, left_colors, false);
        const float right_x0 = static_cast<float>(
            screen.width() - edge - panel_width);
        const float right_x1 = static_cast<float>(screen.width() - edge);
        const float right_x[4] = {right_x0, right_x1, right_x1, right_x0};
        const u32 right_colors[4] = {clear, dense, dense, clear};
        screen.blendQuad(right_x, y, right_colors, false);
    } else {
        screen.rectangle(edge, panel_y, panel_width, panel_height, panel);
        screen.rectangle(screen.width() - edge - panel_width, panel_y,
                         panel_width, panel_height, panel);
    }
    drawNumber(screen, score.score, edge + panel_width / 2,
               value_y, value_scale,
               score.failed ? 0xffff5060u : 0xfff0d050u);
    const int combo_x = screen.width() - edge - panel_width;
    const u32 combo_color = combo_flash > 0.0f ? 0xffff5060u : 0xff70d8ffu;
    drawNumber(screen, score.combo, combo_x + panel_width / 2,
               value_y, value_scale, combo_color);
    // A small x before the combo, its foot level with the digits'.
    char combo_text[16];
    const int combo_digits = snprintf(combo_text, sizeof(combo_text), "%u", score.combo);
    const int combo_left = combo_x + panel_width / 2 -
        (combo_digits * value_scale * 4 - value_scale) / 2;
    drawMenuText(screen, "x", combo_left - label_scale * 7,
                 value_y + value_scale * 5 - label_scale * 7, label_scale, combo_color);

    const int bar_x = screen.width() / 4;
    const int bar_width = screen.width() / 2;
    const int bar_height = std::max(12, screen.height() / 60);
    const int bar_y = screen.height() - screen.height() / 28;
    const int energy_y = bar_y - std::max(8, screen.height() / 90);
    if (show_energy) {
        screen.rectangle(bar_x, energy_y, bar_width,
                         std::max(4, screen.height() / 180), 0xff202838u);
        const u32 energy_color = score.energy > 50 ? 0xff40d870u :
            (score.energy > 20 ? 0xffffb030u : 0xffe83848u);
        screen.rectangle(bar_x,
                         energy_y,
                         bar_width * static_cast<int>(score.energy) / 100,
                         std::max(4, screen.height() / 180), energy_color);
        drawMenuText(screen, tr("LIFE", "ЖИЗНЬ"), bar_x + bar_width + screen.width() / 96,
                     energy_y - label_scale * 7 - screen.height() / 180,
                     label_scale, label);
    }

    screen.rectangle(bar_x, bar_y, bar_width, bar_height, 0xff202838u);
    if (duration_ms > 0) {
        const unsigned int clamped_time = std::min(elapsed_ms, duration_ms);
        const int progress_width = static_cast<int>(
            static_cast<unsigned long long>(bar_width) * clamped_time /
            duration_ms);
        screen.rectangle(bar_x, bar_y, progress_width, bar_height,
                         0xff40b8ffu);
        const unsigned int remaining_ms = duration_ms - clamped_time;
        const int time_x = screen.width() / 6;
        const int time_y = bar_y - screen.height() / 48;
        drawMenuText(screen, ICON_TIMER, time_x - label_scale * 11,
                     time_y - label_scale, label_scale, label);
        drawTime(screen, remaining_ms, time_x, time_y, label_scale, 0xfff0f0f0u);
    }
}

} // namespace ps3
} // namespace movebeat
