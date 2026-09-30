#ifndef MOVEBEAT_PS3_DRAW_BATCH_HPP
#define MOVEBEAT_PS3_DRAW_BATCH_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "software_note_renderer.hpp"

namespace movebeat {
namespace ps3 {

// One RSX vertex: clip-space position, RGBA color and texture coordinate.
// Positions are already in normalized device coordinates, so the vertex
// program is a pass-through and needs no constants.
struct BatchVertex {
    float position[4];
    float color[4];
    float texcoord[2];
};

enum BatchMode {
    // Painter's order, no depth test: track, HUD, menus, sabers.
    BATCH_FLAT,
    // Note cube faces: depth test LESS with depth write, matching the
    // software rasterizer's "nearer inverse depth wins".
    BATCH_NOTE_SOLID,
    // Arrows, dots and bombs over cubes: depth test LEQUAL without write,
    // matching paintNoteInverseDepthPixel's small tolerance.
    BATCH_NOTE_GLYPH,
    // Library cards: one textured quad each, painter's order.
    BATCH_TEXTURED
};

// Consecutive vertices drawn with the same pipeline state.
struct BatchRun {
    BatchMode mode;
    std::size_t first_vertex;
    std::size_t vertex_count;
    int texture;
};

// CPU copy of an image uploaded for a BATCH_TEXTURED run, in the same
// 0xAARRGGBB layout as the display buffer.
struct BatchTexture {
    int width;
    int height;
    std::vector<std::uint32_t> pixels;
};

// Largest inverse camera depth a note can have (camera depth > 0.1).
const float kBatchMaxInverseDepth = 10.0f;
// paintNoteInverseDepthPixel accepts glyphs this close behind a cube face.
const float kBatchGlyphDepthTolerance = 0.0001f;

// Maps inverse camera depth (larger is nearer, 0 is the cleared far plane)
// to the RSX depth range [0, 1] (smaller is nearer). The map is affine, so
// the GPU's screen-space interpolation matches the software rasterizer.
inline float batchDepthFromInverse(float inverse_depth) {
    float depth = 1.0f - inverse_depth / kBatchMaxInverseDepth;
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    return depth;
}

// Records one frame of Framebuffer drawing calls as triangles for the RSX.
// Pixel coordinates follow the software renderer: (0, 0) is the top-left
// pixel and integer vertices address pixel centers unless noted otherwise.
class DrawBatch {
public:
    DrawBatch();

    void reset(int width, int height, std::size_t vertex_capacity,
               std::size_t texture_capacity);

    // Same coverage as Framebuffer's software primitives.
    void rectangle(int x, int y, int width, int height, std::uint32_t color);
    void triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                  std::uint32_t color);
    void line(int x0, int y0, int x1, int y1, std::uint32_t color,
              int thickness);
    void circle(int center_x, int center_y, int radius, std::uint32_t color);
    // Depth-tested glyphs; camera_depth is the note's camera-space distance.
    void noteLine(int x0, int y0, int x1, int y1, std::uint32_t color,
                  int thickness, float camera_depth);
    void noteCircle(int center_x, int center_y, int radius,
                    std::uint32_t color, float camera_depth);
    // A note cube face triangle in software-rasterizer coordinates: sampled
    // at pixel centers (x + 0.5), with inverse camera depth per vertex.
    void solidTriangle(const ProjectedSolidVertex& a,
                       const ProjectedSolidVertex& b,
                       const ProjectedSolidVertex& c, std::uint32_t color);
    // Copies an RGB565 image and draws it 1:1 at (x, y).
    void image(int x, int y, int width, int height,
               const unsigned short* rgb565);

    // Functor form of solidTriangle for forEachNoteCubeTriangle.
    void operator()(const ProjectedSolidVertex& a,
                    const ProjectedSolidVertex& b,
                    const ProjectedSolidVertex& c, std::uint32_t color) {
        solidTriangle(a, b, c, color);
    }

    const std::vector<BatchVertex>& vertices() const { return vertices_; }
    const std::vector<BatchRun>& runs() const { return runs_; }
    const std::vector<BatchTexture>& textures() const { return textures_; }
    // Primitives dropped this frame because a capacity was reached.
    unsigned int droppedPrimitives() const { return dropped_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    // Appends a triangle in pixel-edge coordinates (a pixel's center is at
    // +0.5). Returns false and counts a drop when the batch is full.
    bool pushTriangle(BatchMode mode, int texture,
                      const float (&x)[3], const float (&y)[3],
                      const float (&depth)[3], std::uint32_t color,
                      const float (*texcoord)[2]);
    void pushQuad(BatchMode mode, float left, float top, float right,
                  float bottom, float depth, std::uint32_t color);
    void pushLine(BatchMode mode, int x0, int y0, int x1, int y1,
                  std::uint32_t color, int thickness, float depth);
    void pushDisc(BatchMode mode, int center_x, int center_y, int radius,
                  std::uint32_t color, float depth);

    int width_;
    int height_;
    std::size_t vertex_capacity_;
    std::size_t texture_capacity_;
    unsigned int dropped_;
    std::vector<BatchVertex> vertices_;
    std::vector<BatchRun> runs_;
    std::vector<BatchTexture> textures_;
};

} // namespace ps3
} // namespace movebeat

#endif
