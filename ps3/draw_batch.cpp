#include "draw_batch.hpp"

#include <cmath>

namespace movebeat {
namespace ps3 {
namespace {

const float kPi = 3.14159265358979323846f;

int absolute(int value) {
    return value < 0 ? -value : value;
}

bool glyphDepth(float camera_depth, float& depth) {
    if (!std::isfinite(camera_depth) || camera_depth <= 0.1f) return false;
    depth = batchDepthFromInverse(1.0f / camera_depth +
                                  kBatchGlyphDepthTolerance);
    return true;
}

} // namespace

DrawBatch::DrawBatch()
    : width_(0), height_(0), vertex_capacity_(0), texture_capacity_(0),
      dropped_(0) {}

void DrawBatch::reset(int width, int height, std::size_t vertex_capacity,
                      std::size_t texture_capacity) {
    width_ = width;
    height_ = height;
    vertex_capacity_ = vertex_capacity;
    texture_capacity_ = texture_capacity;
    dropped_ = 0;
    vertices_.clear();
    runs_.clear();
    textures_.clear();
    if (vertices_.capacity() < vertex_capacity) vertices_.reserve(vertex_capacity);
}

bool DrawBatch::pushTriangle(BatchMode mode, int texture,
                             const float (&x)[3], const float (&y)[3],
                             const float (&depth)[3], std::uint32_t color,
                             const float (*texcoord)[2]) {
    if (width_ <= 0 || height_ <= 0) return false;
    if (vertices_.size() + 3u > vertex_capacity_) {
        ++dropped_;
        return false;
    }
    if (runs_.empty() || runs_.back().mode != mode ||
        runs_.back().texture != texture) {
        BatchRun run;
        run.mode = mode;
        run.first_vertex = vertices_.size();
        run.vertex_count = 0;
        run.texture = texture;
        runs_.push_back(run);
    }
    const float red = ((color >> 16) & 255u) / 255.0f;
    const float green = ((color >> 8) & 255u) / 255.0f;
    const float blue = (color & 255u) / 255.0f;
    const float alpha = ((color >> 24) & 255u) / 255.0f;
    for (int corner = 0; corner < 3; ++corner) {
        BatchVertex vertex;
        vertex.position[0] = x[corner] * 2.0f / width_ - 1.0f;
        vertex.position[1] = 1.0f - y[corner] * 2.0f / height_;
        vertex.position[2] = depth[corner] * 2.0f - 1.0f;
        vertex.position[3] = 1.0f;
        vertex.color[0] = red;
        vertex.color[1] = green;
        vertex.color[2] = blue;
        vertex.color[3] = alpha;
        vertex.texcoord[0] = texcoord ? texcoord[corner][0] : 0.0f;
        vertex.texcoord[1] = texcoord ? texcoord[corner][1] : 0.0f;
        vertices_.push_back(vertex);
    }
    runs_.back().vertex_count += 3;
    return true;
}

void DrawBatch::pushQuad(BatchMode mode, float left, float top, float right,
                         float bottom, float depth, std::uint32_t color) {
    const float x0[3] = {left, right, right};
    const float y0[3] = {top, top, bottom};
    const float x1[3] = {left, right, left};
    const float y1[3] = {top, bottom, bottom};
    const float z[3] = {depth, depth, depth};
    pushTriangle(mode, -1, x0, y0, z, color, 0);
    pushTriangle(mode, -1, x1, y1, z, color, 0);
}

void DrawBatch::rectangle(int x, int y, int width, int height,
                          std::uint32_t color) {
    if (width <= 0 || height <= 0) return;
    pushQuad(BATCH_FLAT, static_cast<float>(x), static_cast<float>(y),
             static_cast<float>(x + width), static_cast<float>(y + height),
             0.0f, color);
}

void DrawBatch::triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                         std::uint32_t color) {
    // The software rasterizer samples integer points; the RSX samples pixel
    // centers, so shift the vertices by half a pixel.
    const long long area =
        static_cast<long long>(x1 - x0) * (y2 - y0) -
        static_cast<long long>(y1 - y0) * (x2 - x0);
    if (area == 0) return;
    const float x[3] = {x0 + 0.5f, x1 + 0.5f, x2 + 0.5f};
    const float y[3] = {y0 + 0.5f, y1 + 0.5f, y2 + 0.5f};
    const float z[3] = {0.0f, 0.0f, 0.0f};
    pushTriangle(BATCH_FLAT, -1, x, y, z, color, 0);
}

void DrawBatch::pushLine(BatchMode mode, int x0, int y0, int x1, int y1,
                         std::uint32_t color, int thickness, float depth) {
    // Software lines stamp a (2r+1)-pixel square at every Bresenham step,
    // one step per pixel along the major axis. Their union is a
    // parallelogram: extended by r + 0.5 along the major axis at both ends
    // and offset across it by r + 0.5 plus the drift r * |minor/major| of
    // neighbouring stamps. With r = 0 this covers one pixel per step.
    const int radius = thickness > 1 ? thickness / 2 : 0;
    const float half = radius + 0.5f;
    const float start_x = x0 + 0.5f;
    const float start_y = y0 + 0.5f;
    if (x0 == x1 && y0 == y1) {
        pushQuad(mode, start_x - half, start_y - half,
                 start_x + half, start_y + half, depth, color);
        return;
    }
    const float dx = static_cast<float>(x1 - x0);
    const float dy = static_cast<float>(y1 - y0);
    const bool x_major = absolute(x1 - x0) >= absolute(y1 - y0);
    const float major = x_major ? std::fabs(dx) : std::fabs(dy);
    const float minor = x_major ? std::fabs(dy) : std::fabs(dx);
    const float extend = half / major;
    const float back_x = start_x - dx * extend;
    const float back_y = start_y - dy * extend;
    const float front_x = x1 + 0.5f + dx * extend;
    const float front_y = y1 + 0.5f + dy * extend;
    const float offset = half + radius * minor / major;
    const float offset_x = x_major ? 0.0f : offset;
    const float offset_y = x_major ? offset : 0.0f;
    const float ax[3] = {back_x - offset_x, front_x - offset_x, front_x + offset_x};
    const float ay[3] = {back_y - offset_y, front_y - offset_y, front_y + offset_y};
    const float bx[3] = {back_x - offset_x, front_x + offset_x, back_x + offset_x};
    const float by[3] = {back_y - offset_y, front_y + offset_y, back_y + offset_y};
    const float z[3] = {depth, depth, depth};
    pushTriangle(mode, -1, ax, ay, z, color, 0);
    pushTriangle(mode, -1, bx, by, z, color, 0);
}

void DrawBatch::line(int x0, int y0, int x1, int y1, std::uint32_t color,
                     int thickness) {
    pushLine(BATCH_FLAT, x0, y0, x1, y1, color, thickness, 0.0f);
}

void DrawBatch::pushDisc(BatchMode mode, int center_x, int center_y,
                         int radius, std::uint32_t color, float depth) {
    if (radius < 0) return;
    // Software discs fill integer offsets with x^2 + y^2 <= r^2. The next
    // lattice distance is sqrt(r^2 + 1), so a disc of radius sqrt(r^2 + 0.5)
    // around the pixel center covers exactly that set; the polygon's
    // vertices sit on it and only chord midpoints can lose a boundary pixel.
    int segments = 12 + 2 * radius;
    if (segments > 96) segments = 96;
    const float outer = std::sqrt(static_cast<float>(radius) * radius + 0.5f);
    const float cx = center_x + 0.5f;
    const float cy = center_y + 0.5f;
    const float z[3] = {depth, depth, depth};
    for (int segment = 0; segment < segments; ++segment) {
        const float a0 = 2.0f * kPi * segment / segments;
        const float a1 = 2.0f * kPi * (segment + 1) / segments;
        const float x[3] = {cx, cx + outer * std::cos(a0), cx + outer * std::cos(a1)};
        const float y[3] = {cy, cy + outer * std::sin(a0), cy + outer * std::sin(a1)};
        if (!pushTriangle(mode, -1, x, y, z, color, 0)) return;
    }
}

void DrawBatch::circle(int center_x, int center_y, int radius,
                       std::uint32_t color) {
    pushDisc(BATCH_FLAT, center_x, center_y, radius, color, 0.0f);
}

void DrawBatch::noteLine(int x0, int y0, int x1, int y1, std::uint32_t color,
                         int thickness, float camera_depth) {
    float depth = 0.0f;
    if (!glyphDepth(camera_depth, depth)) return;
    pushLine(BATCH_NOTE_GLYPH, x0, y0, x1, y1, color, thickness, depth);
}

void DrawBatch::noteCircle(int center_x, int center_y, int radius,
                           std::uint32_t color, float camera_depth) {
    float depth = 0.0f;
    if (!glyphDepth(camera_depth, depth)) return;
    pushDisc(BATCH_NOTE_GLYPH, center_x, center_y, radius, color, depth);
}

void DrawBatch::solidTriangle(const ProjectedSolidVertex& a,
                              const ProjectedSolidVertex& b,
                              const ProjectedSolidVertex& c,
                              std::uint32_t color) {
    // rasterizeProjectedTriangle samples (x + 0.5, y + 0.5) against these
    // coordinates, exactly like the RSX; no shift is needed.
    const float x[3] = {a.x, b.x, c.x};
    const float y[3] = {a.y, b.y, c.y};
    const float z[3] = {
        batchDepthFromInverse(a.inverse_depth),
        batchDepthFromInverse(b.inverse_depth),
        batchDepthFromInverse(c.inverse_depth)
    };
    pushTriangle(BATCH_NOTE_SOLID, -1, x, y, z, color, 0);
}

void DrawBatch::image(int x, int y, int width, int height,
                      const unsigned short* rgb565) {
    if (!rgb565 || width <= 0 || height <= 0) return;
    if (textures_.size() >= texture_capacity_) {
        ++dropped_;
        return;
    }
    BatchTexture texture;
    texture.width = width;
    texture.height = height;
    texture.pixels.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t index = 0; index < texture.pixels.size(); ++index) {
        const unsigned short color = rgb565[index];
        const std::uint32_t red = ((color >> 11) & 31u) * 255u / 31u;
        const std::uint32_t green = ((color >> 5) & 63u) * 255u / 63u;
        const std::uint32_t blue = (color & 31u) * 255u / 31u;
        texture.pixels[index] = 0xff000000u | (red << 16) | (green << 8) | blue;
    }
    const int texture_index = static_cast<int>(textures_.size());
    textures_.push_back(texture);
    const float left = static_cast<float>(x);
    const float top = static_cast<float>(y);
    const float right = static_cast<float>(x + width);
    const float bottom = static_cast<float>(y + height);
    const float z[3] = {0.0f, 0.0f, 0.0f};
    const float x0[3] = {left, right, right};
    const float y0[3] = {top, top, bottom};
    const float uv0[3][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
    const float x1[3] = {left, right, left};
    const float y1[3] = {top, bottom, bottom};
    const float uv1[3][2] = {{0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    // Texture color is multiplied by the vertex color, so use opaque white.
    pushTriangle(BATCH_TEXTURED, texture_index, x0, y0, z, 0xffffffffu, uv0);
    pushTriangle(BATCH_TEXTURED, texture_index, x1, y1, z, 0xffffffffu, uv1);
}

} // namespace ps3
} // namespace movebeat
