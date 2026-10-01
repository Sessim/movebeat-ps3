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

// Color blending. Effects use translucent geometry instead of extra
// full-screen passes: the RSX is fill-rate bound at 720p, so each effect
// stays a few triangles and at most a couple of overlapping layers.
enum BatchBlend {
    BLEND_OPAQUE,
    // src * a + dst * (1 - a): translucent walls and floor.
    BLEND_ALPHA,
    // src * a + dst: glow, trails and flashes brighten what is behind them.
    BLEND_ADD
};

// Consecutive vertices drawn with the same pipeline state.
struct BatchRun {
    BatchMode mode;
    std::size_t first_vertex;
    std::size_t vertex_count;
    int texture;
    BatchBlend blend;
};

// CPU copy of an image uploaded for a BATCH_TEXTURED run, in the same
// 0xAARRGGBB layout as the display buffer.
struct BatchTexture {
    int width;
    int height;
    // Caller-owned 0xAARRGGBB pixels, valid until the batch is submitted.
    const std::uint32_t* pixels;
    // Non-zero: identical pixels for every frame with this key, so the RSX
    // renderer keeps the texture resident instead of uploading it again.
    unsigned int cache_key;
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
    // Depth-tested glyphs drawn on a note's front plane at world depth
    // plane_z. Depth is evaluated per vertex on that plane: the face is
    // tilted toward the pitched camera, so one center depth would hide the
    // half of a glyph that lies nearer than the face center.
    void noteLine(int x0, int y0, int x1, int y1, std::uint32_t color,
                  int thickness, float plane_z);
    void noteCircle(int center_x, int center_y, int radius,
                    std::uint32_t color, float plane_z);
    // Filled glyph triangle in pixel-edge coordinates on the same plane.
    void noteTriangle(const float (&x)[3], const float (&y)[3],
                      std::uint32_t color, float plane_z);
    // A note body triangle in software-rasterizer coordinates: sampled at
    // pixel centers (x + 0.5), with inverse camera depth and a color per
    // vertex.
    void solidTriangle(const ProjectedSolidVertex& a,
                       const ProjectedSolidVertex& b,
                       const ProjectedSolidVertex& c, std::uint32_t color_a,
                       std::uint32_t color_b, std::uint32_t color_c);
    // Draws an 0xAARRGGBB image 1:1 at (x, y) without copying it; see
    // BatchTexture for the lifetime and cache_key rules.
    void image(int x, int y, int width, int height,
               const std::uint32_t* argb, unsigned int cache_key);
    // Draws the source rectangle (texels) of a texture_width x
    // texture_height image into a destination rectangle (pixel edges),
    // scaled with linear filtering and with an opacity.
    void imageRegion(float x, float y, float width, float height,
                     const std::uint32_t* argb, int texture_width,
                     int texture_height, float source_x0, float source_y0,
                     float source_x1, float source_y1, unsigned int cache_key,
                     float alpha);

    // Effects. Coordinates are pixel edges (a pixel center is at +0.5) and
    // every vertex has its own 0xAARRGGBB color, interpolated by the RSX.
    void blendTriangle(const float (&x)[3], const float (&y)[3],
                       const std::uint32_t (&colors)[3], BatchBlend blend);
    // Corners in order; drawn as triangles (0, 1, 2) and (0, 2, 3).
    void blendQuad(const float (&x)[4], const float (&y)[4],
                   const std::uint32_t (&colors)[4], BatchBlend blend);
    // Additive band: full color on the segment, fading to zero half_width
    // away from it. Four triangles.
    void glowLine(float x0, float y0, float x1, float y1, float half_width,
                  std::uint32_t color);
    // Additive radial fade from color at the center to zero at radius.
    void glowDisc(float center_x, float center_y, float radius,
                  std::uint32_t color, int segments);

    // Functor form of solidTriangle for forEachRoundedNoteTriangle.
    void operator()(const ProjectedSolidVertex& a,
                    const ProjectedSolidVertex& b,
                    const ProjectedSolidVertex& c, std::uint32_t color_a,
                    std::uint32_t color_b, std::uint32_t color_c) {
        solidTriangle(a, b, c, color_a, color_b, color_c);
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
    bool pushColoredTriangle(BatchMode mode, BatchBlend blend, int texture,
                             const float (&x)[3], const float (&y)[3],
                             const float (&depth)[3],
                             const std::uint32_t (&colors)[3],
                             const float (*texcoord)[2]);
    void pushQuad(BatchMode mode, float left, float top, float right,
                  float bottom, float depth, std::uint32_t color);
    void pushLine(BatchMode mode, int x0, int y0, int x1, int y1,
                  std::uint32_t color, int thickness, float depth);
    void pushDisc(BatchMode mode, int center_x, int center_y, int radius,
                  std::uint32_t color, float depth);

    bool glyphPlaneDepth(float x, float y, float& depth) const;

    int width_;
    int height_;
    std::size_t vertex_capacity_;
    std::size_t texture_capacity_;
    unsigned int dropped_;
    // Set while noteLine/noteCircle emit vertices on a note's front plane.
    bool glyph_plane_active_;
    float glyph_plane_z_;
    std::vector<BatchVertex> vertices_;
    std::vector<BatchRun> runs_;
    std::vector<BatchTexture> textures_;
};

} // namespace ps3
} // namespace movebeat

#endif
