#ifndef MOVEBEAT_PS3_FRAMEBUFFER_HPP
#define MOVEBEAT_PS3_FRAMEBUFFER_HPP

#include <ppu-types.h>
#include <rsx/rsx.h>

#include "cut_feedback.hpp"
#include "draw_batch.hpp"
#include "rsx_batch_renderer.hpp"
#include "movebeat/game.hpp"

namespace movebeat {
namespace ps3 {

enum RendererPreference {
    // Draw the frame on the RSX; fall back to software if it cannot start.
    RENDERER_PREFER_RSX,
    // PPU rasterizer only (USRDIR/renderer.ini: renderer=software).
    RENDERER_FORCE_SOFTWARE
};

// Double-buffered 32-bit display with two interchangeable back ends. The RSX
// path records every primitive into a DrawBatch and draws it at present();
// the software path writes pixels from the PPU. Both expose the same calls.
class Framebuffer {
public:
    Framebuffer();
    ~Framebuffer();

    // `full_hd`: on the RSX, output 1080p when the TV takes it. Everything
    // is still drawn in 1280x720 coordinates (width(), height()); the RSX
    // scales the geometry to the 1920x1080 surface, so lines, cubes and the
    // font are drawn at full resolution and the layout is unchanged.
    bool initialize(RendererPreference preference = RENDERER_PREFER_RSX,
                    bool full_hd = true);
    void shutdown();
    void waitForPreviousFlip();
    void beginFrame(u32 color);
    void present();
    // On the RSX path this renders the pending frame and reads it back from
    // video memory, which is slow; use it for diagnostics only.
    bool savePpm(const char* path);

    void pixel(int x, int y, u32 color);
    void line(int x0, int y0, int x1, int y1, u32 color, int thickness);
    void rectangle(int x, int y, int width, int height, u32 color);
    void triangle(int x0, int y0, int x1, int y1, int x2, int y2, u32 color);
    void quadrilateral(int x0, int y0, int x1, int y1,
                       int x2, int y2, int x3, int y3, u32 color);
    void circle(int center_x, int center_y, int radius, u32 color);
    // Draws an 0xAARRGGBB image 1:1 at (x, y). The pixels must stay valid
    // until present(). A non-zero cache_key promises identical pixels for
    // the same key, so the RSX path uploads the texture only once.
    void image(int x, int y, int width, int height,
               const std::uint32_t* argb, unsigned int cache_key);
    // Draws a source rectangle of an image scaled into a destination
    // rectangle. The RSX filters linearly and blends `alpha`; the software
    // path scales by nearest texel and skips images below alpha 0.35.
    void imageRegion(float x, float y, float width, float height,
                     const std::uint32_t* argb, int texture_width,
                     int texture_height, float source_x0, float source_y0,
                     float source_x1, float source_y1, unsigned int cache_key,
                     float alpha);
    // Direction glyphs and bombs obey the 3D note depth buffer when active.
    // They lie on the note's front plane at world depth plane_z, whose
    // depth is evaluated per pixel (the plane is tilted to the camera).
    void noteLine(int x0, int y0, int x1, int y1, u32 color,
                  int thickness, float plane_z);
    void noteCircle(int center_x, int center_y, int radius, u32 color,
                    float plane_z);
    // Filled glyph triangle; coordinates are pixel edges (a pixel's center
    // is at +0.5), as for the effect primitives.
    void noteTriangle(const float (&x)[3], const float (&y)[3], u32 color,
                      float plane_z);
    // Draw cuttable notes as 3D mesh triangles with perspective and a Z-buffer,
    // and with `cuts` the two flying halves of every recently cut cube.
    // Returns false if the software depth buffer cannot be allocated.
    bool drawSolidNotes(const movebeat::Game& game, unsigned int now_ms,
                        const CutFeedback* cuts = 0);
    unsigned int lastSolidNoteCount() const { return last_solid_note_count_; }
    unsigned int lastCutPieceCount() const { return last_cut_piece_count_; }

    // Translucent and additive effects. They exist only on the RSX path:
    // blending on the PPU would read video memory back, which is far too
    // slow, so the software fallback and "effects=off" skip these calls.
    bool effectsEnabled() const { return rsx_ && effects_; }
    void setEffectsEnabled(bool enabled) { effects_ = enabled; }
    void blendQuad(const float (&x)[4], const float (&y)[4],
                   const u32 (&colors)[4], bool additive);
    // Opaque quad with per-corner colors, e.g. the sky gradient.
    void gradientQuad(const float (&x)[4], const float (&y)[4],
                      const u32 (&colors)[4]);
    void glowLine(float x0, float y0, float x1, float y1, float half_width,
                  u32 color);
    void glowDisc(float center_x, float center_y, float radius, u32 color);

    bool usingRsx() const { return rsx_; }
    const char* rendererName() const { return rsx_ ? "rsx" : "software"; }
    // Vertices and dropped primitives of the last submitted RSX frame.
    unsigned int lastBatchVertices() const { return last_batch_vertices_; }
    unsigned int lastBatchDropped() const { return last_batch_dropped_; }

    int width() const { return width_; }
    int height() const { return height_; }
    // The video mode's lines: 720 or 1080.
    int displayHeight() const { return display_height_; }

private:
    struct Buffer {
        u32* pixels;
        u32 offset;
        int id;
    };

    Framebuffer(const Framebuffer&);
    Framebuffer& operator=(const Framebuffer&);

    bool makeBuffer(int index);
    bool configureDisplay(u32 resolution_id, bool scaled);
    void freeBuffers();
    void waitForFlip();
    void submitFrame();

    gcmContextData* context_;
    void* host_memory_;
    Buffer buffers_[2];
    int current_;
    int width_;
    int height_;
    // The display buffers' size; larger than width_ x height_ only on the
    // RSX at 1080p.
    int display_width_;
    int display_height_;
    int pitch_;
    u32 clear_color_;
    bool initialized_;
    bool flip_submitted_;
    float* software_depth_;
    bool note_depth_active_;
    unsigned int last_solid_note_count_;
    unsigned int last_cut_piece_count_;
    bool rsx_;
    bool effects_;
    bool frame_submitted_;
    DrawBatch batch_;
    RsxBatchRenderer gpu_;
    unsigned int last_batch_vertices_;
    unsigned int last_batch_dropped_;
};

} // namespace ps3
} // namespace movebeat

#endif
