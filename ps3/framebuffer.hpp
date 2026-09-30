#ifndef MOVEBEAT_PS3_FRAMEBUFFER_HPP
#define MOVEBEAT_PS3_FRAMEBUFFER_HPP

#include <ppu-types.h>
#include <rsx/rsx.h>

#include "movebeat/game.hpp"

namespace movebeat {
namespace ps3 {

class Framebuffer {
public:
    Framebuffer();
    ~Framebuffer();

    bool initialize();
    void shutdown();
    void waitForPreviousFlip();
    void beginFrame(u32 color);
    void present();
    bool savePpm(const char* path) const;

    void pixel(int x, int y, u32 color);
    void line(int x0, int y0, int x1, int y1, u32 color, int thickness);
    void rectangle(int x, int y, int width, int height, u32 color);
    void triangle(int x0, int y0, int x1, int y1, int x2, int y2, u32 color);
    void quadrilateral(int x0, int y0, int x1, int y1,
                       int x2, int y2, int x3, int y3, u32 color);
    void circle(int center_x, int center_y, int radius, u32 color);
    // Direction glyphs and bombs obey the 3D note depth buffer when active.
    void noteLine(int x0, int y0, int x1, int y1, u32 color,
                  int thickness, float camera_depth);
    void noteCircle(int center_x, int center_y, int radius, u32 color,
                    float camera_depth);
    // Draw cuttable notes as 3D mesh triangles with perspective and a Z-buffer.
    // Returns false if the depth buffer cannot be allocated.
    bool drawSolidNotes(const movebeat::Game& game, unsigned int now_ms);
    unsigned int lastSolidNoteCount() const { return last_solid_note_count_; }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    struct Buffer {
        u32* pixels;
        u32 offset;
        int id;
    };

    Framebuffer(const Framebuffer&);
    Framebuffer& operator=(const Framebuffer&);

    bool makeBuffer(int index);
    bool initializeSolidNotes();
    void bindCurrentSurface();
    void waitForFlip();

    gcmContextData* context_;
    void* host_memory_;
    Buffer buffers_[2];
    int current_;
    int width_;
    int height_;
    int pitch_;
    u32 clear_color_;
    bool initialized_;
    bool flip_submitted_;
    void* depth_buffer_;
    void* note_vertex_buffer_;
    void* note_fragment_buffer_;
    u32 depth_offset_;
    u32 note_vertex_offset_;
    u32 note_fragment_offset_;
    bool solid_notes_ready_;
    float* software_depth_;
    bool note_depth_active_;
    unsigned int last_solid_note_count_;
};

} // namespace ps3
} // namespace movebeat

#endif
