#ifndef MOVEBEAT_PS3_SOFTWARE_NOTE_RENDERER_HPP
#define MOVEBEAT_PS3_SOFTWARE_NOTE_RENDERER_HPP

#include "note_cube.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace movebeat {
namespace ps3 {

struct SoftwareNoteTarget {
    std::uint32_t* pixels;
    float* inverse_depth;
    int width;
    int height;
};

inline bool paintNoteInverseDepthPixel(const SoftwareNoteTarget& target,
                                       int x, int y, float inverse_depth,
                                       std::uint32_t color) {
    if (x < 0 || y < 0 || x >= target.width || y >= target.height) return false;
    const int index = y * target.width + x;
    // The same front plane may differ by a few FP ulps after interpolation.
    // This tolerance is far smaller than the depth gap between distinct notes.
    // Glyph depths are evaluated at the cube face center while the rasterized
    // face depth varies slightly across its perspective projection. A small
    // tolerance keeps the center arrow on its own face without admitting the
    // much farther notes behind it.
    if (inverse_depth + 0.0001f < target.inverse_depth[index]) return false;
    target.pixels[index] = color;
    if (inverse_depth > target.inverse_depth[index])
        target.inverse_depth[index] = inverse_depth;
    return true;
}

inline bool paintNoteDepthPixel(const SoftwareNoteTarget& target,
                                int x, int y, float world_z,
                                std::uint32_t color) {
    const float distance = cameraDepthAtScreenPointOnWorldZ(
        static_cast<float>(x), static_cast<float>(y), target.width,
        target.height, world_z);
    if (!std::isfinite(distance) || distance <= 0.1f) return false;
    return paintNoteInverseDepthPixel(target, x, y, 1.0f / distance, color);
}

struct ProjectedSolidVertex {
    float x;
    float y;
    float inverse_depth;
};

inline bool projectSolidVertex(const Vec3& point, int width, int height,
                               ProjectedSolidVertex& result) {
    const CameraPoint camera = toNoteCameraSpace(point);
    if (!std::isfinite(camera.depth) || camera.depth <= 0.1f ||
        !std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    const float focal = height * kProjectionFocalHeight * kProjectionDepth;
    result.inverse_depth = 1.0f / camera.depth;
    result.x = width * 0.5f + camera.x * focal * result.inverse_depth;
    result.y = height * 0.5f - camera.y * focal * result.inverse_depth;
    return std::isfinite(result.x) && std::isfinite(result.y);
}

inline float solidEdge(float ax, float ay, float bx, float by,
                       float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

inline void rasterizeProjectedTriangle(const SoftwareNoteTarget& target,
                                       const ProjectedSolidVertex& pa,
                                       const ProjectedSolidVertex& pb,
                                       const ProjectedSolidVertex& pc,
                                       std::uint32_t color) {
    const float area = solidEdge(pa.x, pa.y, pb.x, pb.y, pc.x, pc.y);
    if (!std::isfinite(area) || std::fabs(area) < 0.0001f) return;
    const int left = std::max(0, static_cast<int>(std::floor(
        std::min(pa.x, std::min(pb.x, pc.x)))));
    const int right = std::min(target.width - 1, static_cast<int>(std::ceil(
        std::max(pa.x, std::max(pb.x, pc.x)))));
    const int top = std::max(0, static_cast<int>(std::floor(
        std::min(pa.y, std::min(pb.y, pc.y)))));
    const int bottom = std::min(target.height - 1, static_cast<int>(std::ceil(
        std::max(pa.y, std::max(pb.y, pc.y)))));
    if (left > right || top > bottom) return;
    // Edge functions and inverse depth are affine in screen space. Step them
    // across the covered rectangle instead of dividing three times per pixel.
    const float inverse_area = 1.0f / area;
    const float wa_step_x = (pc.y - pb.y) * inverse_area;
    const float wa_step_y = (pb.x - pc.x) * inverse_area;
    const float wb_step_x = (pa.y - pc.y) * inverse_area;
    const float wb_step_y = (pc.x - pa.x) * inverse_area;
    const float start_x = left + 0.5f;
    const float start_y = top + 0.5f;
    float wa_row = solidEdge(pb.x, pb.y, pc.x, pc.y,
                             start_x, start_y) * inverse_area;
    float wb_row = solidEdge(pc.x, pc.y, pa.x, pa.y,
                             start_x, start_y) * inverse_area;
    const float depth_step_x =
        (pa.inverse_depth - pc.inverse_depth) * wa_step_x +
        (pb.inverse_depth - pc.inverse_depth) * wb_step_x;
    const float depth_step_y =
        (pa.inverse_depth - pc.inverse_depth) * wa_step_y +
        (pb.inverse_depth - pc.inverse_depth) * wb_step_y;
    float depth_row = pc.inverse_depth +
        (pa.inverse_depth - pc.inverse_depth) * wa_row +
        (pb.inverse_depth - pc.inverse_depth) * wb_row;
    // The weights are stepped, not recomputed, so rounding drifts along a
    // row. Pixels on the diagonal shared by a face's two triangles could
    // then fall just outside both and leave a dark seam; accept a hair of
    // overlap instead (the second write loses the strict depth test).
    const float edge_tolerance = -1e-4f;
    for (int y = top; y <= bottom; ++y) {
        float wa = wa_row;
        float wb = wb_row;
        float inverse_depth = depth_row;
        for (int x = left; x <= right; ++x) {
            const float wc = 1.0f - wa - wb;
            if (wa >= edge_tolerance && wb >= edge_tolerance &&
                wc >= edge_tolerance) {
                const int index = y * target.width + x;
                if (inverse_depth > target.inverse_depth[index]) {
                    target.inverse_depth[index] = inverse_depth;
                    target.pixels[index] = color;
                }
            }
            wa += wa_step_x;
            wb += wb_step_x;
            inverse_depth += depth_step_x;
        }
        wa_row += wa_step_y;
        wb_row += wb_step_y;
        depth_row += depth_step_y;
    }
}

inline void rasterizeSolidTriangle(const SoftwareNoteTarget& target,
                                   const Vec3& a, const Vec3& b, const Vec3& c,
                                   std::uint32_t color) {
    ProjectedSolidVertex pa, pb, pc;
    if (!projectSolidVertex(a, target.width, target.height, pa) ||
        !projectSolidVertex(b, target.width, target.height, pb) ||
        !projectSolidVertex(c, target.width, target.height, pc)) return;
    rasterizeProjectedTriangle(target, pa, pb, pc, color);
}

inline void rasterizeSolidQuad(const SoftwareNoteTarget& target,
                               const Vec3& a, const Vec3& b,
                               const Vec3& c, const Vec3& d,
                               std::uint32_t color) {
    rasterizeSolidTriangle(target, a, b, c, color);
    rasterizeSolidTriangle(target, a, c, d, color);
}

// Like projectSolidVertex, but with the principal-point shift used by
// projectWorldPoint, so the mesh lines up with the track and the glyphs.
inline bool projectNoteVertex(const Vec3& point, int width, int height,
                              ProjectedSolidVertex& result) {
    if (!projectSolidVertex(point, width, height, result)) return false;
    result.y += projectionVerticalCenterOffset(height);
    return std::isfinite(result.y);
}

// Rounded note body: a superellipsoid |x|^n + |y|^n + |z|^n = 1 scaled to
// the cube. Its corners round off more than its edges ("inflated" corners,
// slightly less inflated edges) while each face center stays exactly on the
// cube's face plane, so glyphs drawn on that plane always sit on top.
const float kRoundedNoteExponent = 6.0f;
// Grid across a face, denser toward the rounded border where it bends.
const int kRoundedNoteGridPoints = 7;
inline float roundedNoteGrid(int index) {
    static const float grid[kRoundedNoteGridPoints] = {
        -1.0f, -0.8f, -0.45f, 0.0f, 0.45f, 0.8f, 1.0f
    };
    return grid[index];
}

// Maps a point on the unit cube surface (max norm 1) onto the superellipsoid.
inline Vec3 roundedNoteSurface(const Vec3& cube_point) {
    const float n = kRoundedNoteExponent;
    const float sum = std::pow(std::fabs(cube_point.x), n) +
                      std::pow(std::fabs(cube_point.y), n) +
                      std::pow(std::fabs(cube_point.z), n);
    const float scale = sum > 0.0f ? std::pow(sum, -1.0f / n) : 1.0f;
    return Vec3(cube_point.x * scale, cube_point.y * scale,
                cube_point.z * scale);
}

// Unit normal of the scaled superellipsoid at a surface point q.
inline Vec3 roundedNoteNormal(const Vec3& q, float half_x, float half_y,
                              float half_z) {
    const float n = kRoundedNoteExponent - 1.0f;
    const auto component = [n](float value, float half) {
        const float magnitude = std::pow(std::fabs(value), n) / half;
        return value < 0.0f ? -magnitude : magnitude;
    };
    Vec3 normal(component(q.x, half_x), component(q.y, half_y),
                component(q.z, half_z));
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y +
                                   normal.z * normal.z);
    if (length > 0.0f) {
        normal.x /= length;
        normal.y /= length;
        normal.z /= length;
    }
    return normal;
}

// Key light from the upper left, on the player's side, plus a soft
// highlight so the rounded corners catch light. Returns the diffuse factor
// and the additive highlight for a unit normal.
inline void roundedNoteLight(const Vec3& normal, float& light, float& specular) {
    const float lx = -0.35f, ly = 0.6f, lz = -0.7f;
    const float light_length = std::sqrt(lx * lx + ly * ly + lz * lz);
    float diffuse = (normal.x * lx + normal.y * ly + normal.z * lz) /
                    light_length;
    if (diffuse < 0.0f) diffuse = 0.0f;
    // Half vector between the light and a viewer looking along +Z.
    const float hx = lx / light_length, hy = ly / light_length,
                hz = lz / light_length - 1.0f;
    const float half_length = std::sqrt(hx * hx + hy * hy + hz * hz);
    specular = (normal.x * hx + normal.y * hy + normal.z * hz) / half_length;
    specular = specular > 0.0f ? std::pow(specular, 24.0f) * 0.35f : 0.0f;
    light = 0.4f + 0.8f * diffuse;
}

inline std::uint32_t applyRoundedNoteLight(std::uint32_t color, float light,
                                           float specular) {
    const float highlight = 255.0f * specular;
    const auto channel = [light, highlight](unsigned int value) {
        const float shaded = value * light + highlight;
        return shaded >= 255.0f ? 255u : static_cast<unsigned int>(shaded);
    };
    return 0xff000000u | (channel((color >> 16) & 255u) << 16) |
           (channel((color >> 8) & 255u) << 8) | channel(color & 255u);
}

// The unit body never changes, so its surface points and lighting are
// computed once: six faces (index axis * 2 + positive side) on the grid.
// Notes are true cubes (gameplayNoteDepth == side), so one table serves
// every size. This keeps per-vertex work to a few multiplies on the PPU.
struct RoundedNoteTable {
    Vec3 point[6][kRoundedNoteGridPoints][kRoundedNoteGridPoints];
    float light[6][kRoundedNoteGridPoints][kRoundedNoteGridPoints];
    float specular[6][kRoundedNoteGridPoints][kRoundedNoteGridPoints];
};

inline const RoundedNoteTable& roundedNoteTable() {
    static RoundedNoteTable table;
    static bool ready = false;
    if (ready) return table;
    for (int face = 0; face < 6; ++face) {
        const int axis = face / 2;
        const float sign = face % 2 ? 1.0f : -1.0f;
        for (int row = 0; row < kRoundedNoteGridPoints; ++row) {
            for (int column = 0; column < kRoundedNoteGridPoints; ++column) {
                const float u = roundedNoteGrid(column);
                const float v = roundedNoteGrid(row);
                Vec3 cube_point;
                if (axis == 2) cube_point = Vec3(u, v, sign);
                else if (axis == 0) cube_point = Vec3(sign, v, u);
                else cube_point = Vec3(u, sign, v);
                const Vec3 q = roundedNoteSurface(cube_point);
                table.point[face][row][column] = q;
                roundedNoteLight(roundedNoteNormal(q, 1.0f, 1.0f, 1.0f),
                                 table.light[face][row][column],
                                 table.specular[face][row][column]);
            }
        }
    }
    ready = true;
    return table;
}

// Emits the visible part of a rounded note as screen-space triangles with
// inverse camera depth and per-vertex shading:
// sink(a, b, c, color_a, color_b, color_c). The software rasterizer and the
// RSX batch both use this, so the two renderers share one note geometry.
template <typename TriangleSink>
void forEachRoundedNoteTriangle(const Vec3& center, float side, float depth,
                                std::uint32_t color, int width, int height,
                                TriangleSink& sink) {
    const float half_x = side * 0.5f;
    const float half_y = side * 0.5f;
    const float half_z = depth * 0.5f;
    // The camera transform is affine, so project through a basis built
    // once per note instead of re-deriving the camera for every vertex.
    const CameraPoint origin = toNoteCameraSpace(center);
    const CameraPoint along_x = toNoteCameraSpace(
        Vec3(center.x + half_x, center.y, center.z));
    const CameraPoint along_y = toNoteCameraSpace(
        Vec3(center.x, center.y + half_y, center.z));
    const CameraPoint along_z = toNoteCameraSpace(
        Vec3(center.x, center.y, center.z + half_z));
    const float basis[3][3] = {
        {along_x.x - origin.x, along_x.y - origin.y, along_x.depth - origin.depth},
        {along_y.x - origin.x, along_y.y - origin.y, along_y.depth - origin.depth},
        {along_z.x - origin.x, along_z.y - origin.y, along_z.depth - origin.depth}
    };
    const float focal = height * kProjectionFocalHeight * kProjectionDepth;
    const float center_x = width * 0.5f;
    const float center_y = height * 0.5f + projectionVerticalCenterOffset(height);
    const RoundedNoteTable& table = roundedNoteTable();
    // Same face choice as the flat cube: the front plus the side and the
    // top/bottom facing the camera; hidden faces never rely on Z.
    const bool left = kNoteCameraX < center.x - half_x;
    const bool right = kNoteCameraX > center.x + half_x;
    const float eye_y = kProjectionEyeHeight + kNoteCameraHeight;
    const bool top = eye_y > center.y + half_y;
    const bool bottom = eye_y < center.y - half_y;
    // Face: fixed axis (0 = x, 1 = y, 2 = z) and its sign.
    int faces[3][2];
    int face_count = 0;
    faces[face_count][0] = 2; faces[face_count][1] = -1; ++face_count;
    if (left || right) {
        faces[face_count][0] = 0; faces[face_count][1] = left ? -1 : 1;
        ++face_count;
    }
    if (top || bottom) {
        faces[face_count][0] = 1; faces[face_count][1] = top ? 1 : -1;
        ++face_count;
    }
    const int points = kRoundedNoteGridPoints;
    ProjectedSolidVertex projected[kRoundedNoteGridPoints][kRoundedNoteGridPoints];
    std::uint32_t shaded[kRoundedNoteGridPoints][kRoundedNoteGridPoints];
    bool valid[kRoundedNoteGridPoints][kRoundedNoteGridPoints];
    for (int face = 0; face < face_count; ++face) {
        const int index = faces[face][0] * 2 + (faces[face][1] > 0 ? 1 : 0);
        for (int row = 0; row < points; ++row) {
            for (int column = 0; column < points; ++column) {
                const Vec3& q = table.point[index][row][column];
                const float camera_x = origin.x + q.x * basis[0][0] +
                    q.y * basis[1][0] + q.z * basis[2][0];
                const float camera_y = origin.y + q.x * basis[0][1] +
                    q.y * basis[1][1] + q.z * basis[2][1];
                const float camera_depth = origin.depth + q.x * basis[0][2] +
                    q.y * basis[1][2] + q.z * basis[2][2];
                ProjectedSolidVertex& out = projected[row][column];
                valid[row][column] = camera_depth > 0.1f;
                if (valid[row][column]) {
                    out.inverse_depth = 1.0f / camera_depth;
                    out.x = center_x + camera_x * focal * out.inverse_depth;
                    out.y = center_y - camera_y * focal * out.inverse_depth;
                    valid[row][column] = std::isfinite(out.x) &&
                                         std::isfinite(out.y);
                }
                shaded[row][column] = applyRoundedNoteLight(
                    color, table.light[index][row][column],
                    table.specular[index][row][column]);
            }
        }
        for (int row = 0; row + 1 < points; ++row) {
            for (int column = 0; column + 1 < points; ++column) {
                if (!valid[row][column] || !valid[row][column + 1] ||
                    !valid[row + 1][column] || !valid[row + 1][column + 1])
                    continue;
                sink(projected[row][column], projected[row][column + 1],
                     projected[row + 1][column + 1], shaded[row][column],
                     shaded[row][column + 1], shaded[row + 1][column + 1]);
                sink(projected[row][column], projected[row + 1][column + 1],
                     projected[row + 1][column], shaded[row][column],
                     shaded[row + 1][column + 1], shaded[row + 1][column]);
            }
        }
    }
}

inline std::uint32_t averageColor(std::uint32_t a, std::uint32_t b,
                                  std::uint32_t c) {
    std::uint32_t result = 0xff000000u;
    for (int shift = 0; shift <= 16; shift += 8) {
        const unsigned int sum = ((a >> shift) & 255u) + ((b >> shift) & 255u) +
                                 ((c >> shift) & 255u);
        result |= ((sum + 1u) / 3u) << shift;
    }
    return result;
}

// The PPU fallback fills each small triangle with its average shade.
struct SoftwareTriangleSink {
    const SoftwareNoteTarget& target;
    void operator()(const ProjectedSolidVertex& a, const ProjectedSolidVertex& b,
                    const ProjectedSolidVertex& c, std::uint32_t color_a,
                    std::uint32_t color_b, std::uint32_t color_c) {
        rasterizeProjectedTriangle(target, a, b, c,
                                   averageColor(color_a, color_b, color_c));
    }
};

inline void rasterizeRoundedNote(const SoftwareNoteTarget& target,
                                 const Vec3& center, float side, float depth,
                                 std::uint32_t color) {
    SoftwareTriangleSink sink = {target};
    forEachRoundedNoteTriangle(center, side, depth, color, target.width,
                               target.height, sink);
}

} // namespace ps3
} // namespace movebeat

#endif
