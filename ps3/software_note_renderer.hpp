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
        x, y, target.width, target.height, world_z);
    if (!std::isfinite(distance) || distance <= 0.1f) return false;
    return paintNoteInverseDepthPixel(target, x, y, 1.0f / distance, color);
}

inline std::uint32_t shadeSolidFace(std::uint32_t color, float light) {
    const unsigned int red = std::min(255u, static_cast<unsigned int>(
        ((color >> 16) & 255u) * light));
    const unsigned int green = std::min(255u, static_cast<unsigned int>(
        ((color >> 8) & 255u) * light));
    const unsigned int blue = std::min(255u, static_cast<unsigned int>(
        (color & 255u) * light));
    return 0xff000000u | (red << 16) | (green << 8) | blue;
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
    for (int y = top; y <= bottom; ++y) {
        float wa = wa_row;
        float wb = wb_row;
        float inverse_depth = depth_row;
        for (int x = left; x <= right; ++x) {
            const float wc = 1.0f - wa - wb;
            if (wa >= 0.0f && wb >= 0.0f && wc >= 0.0f) {
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

// Emits the visible faces of a note cube as screen-space triangles with
// inverse camera depth. The software rasterizer and the RSX batch both use
// this, so the two renderers share one note geometry.
template <typename TriangleSink>
void forEachNoteCubeTriangle(const NoteCubeMesh& cube, std::uint32_t color,
                             int width, int height, TriangleSink& sink) {
    const NoteCubeVisibleFaces faces = visibleNoteCubeFaces(cube);
    const int quad[5][4] = {
        {4, 0, 3, 7}, {1, 5, 6, 2}, {0, 1, 5, 4}, {3, 2, 6, 7},
        {0, 1, 2, 3}
    };
    const auto emitNoteQuad = [&](int face, std::uint32_t face_color) {
        ProjectedSolidVertex projected[4];
        for (int corner = 0; corner < 4; ++corner) {
            const int index = quad[face][corner];
            if (!projectSolidVertex(cube.world[index], width, height,
                                    projected[corner])) return;
            // Geometry comes from the local three-quarter projection, while
            // depth remains in the track camera so overlapping notes stay
            // ordered on their original straight lanes.
            projected[corner].x = static_cast<float>(cube.screen[index].x);
            projected[corner].y = static_cast<float>(cube.screen[index].y);
        }
        sink(projected[0], projected[1], projected[2], face_color);
        sink(projected[0], projected[2], projected[3], face_color);
    };
    if (faces.left)
        emitNoteQuad(0, shadeSolidFace(color, 0.50f));
    else if (faces.right)
        emitNoteQuad(1, shadeSolidFace(color, 0.54f));
    if (faces.top)
        emitNoteQuad(2, shadeSolidFace(color, 0.84f));
    else if (faces.bottom)
        emitNoteQuad(3, shadeSolidFace(color, 0.42f));
    emitNoteQuad(4, color);
}

struct SoftwareTriangleSink {
    const SoftwareNoteTarget& target;
    void operator()(const ProjectedSolidVertex& a, const ProjectedSolidVertex& b,
                    const ProjectedSolidVertex& c, std::uint32_t color) {
        rasterizeProjectedTriangle(target, a, b, c, color);
    }
};

inline void rasterizeNoteCube(const SoftwareNoteTarget& target,
                              const NoteCubeMesh& cube, std::uint32_t color) {
    SoftwareTriangleSink sink = {target};
    forEachNoteCubeTriangle(cube, color, target.width, target.height, sink);
}

} // namespace ps3
} // namespace movebeat

#endif
