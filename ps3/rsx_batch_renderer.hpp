#ifndef MOVEBEAT_PS3_RSX_BATCH_RENDERER_HPP
#define MOVEBEAT_PS3_RSX_BATCH_RENDERER_HPP

#include <cstddef>

#include <ppu-types.h>
#include <rsx/rsx.h>

#include "draw_batch.hpp"

namespace movebeat {
namespace ps3 {

// Draws a DrawBatch on the RSX: clears color and depth, then issues one
// draw per batch run with the matching depth, program and texture state.
class RsxBatchRenderer {
public:
    // 98304 vertices (40 bytes each) and four 512x128 textures per slot;
    // two slots alternate so the CPU never rewrites data the RSX may read.
    static const std::size_t kVertexCapacity = 98304;
    static const std::size_t kTextureCapacity = 4;
    static const int kMaxTextureWidth = 512;
    static const int kMaxTextureHeight = 128;

    RsxBatchRenderer();
    ~RsxBatchRenderer();

    // Allocates the depth buffer, vertex/texture memory and programs.
    bool initialize(gcmContextData* context, int width, int height);
    void shutdown();
    bool ready() const { return ready_; }

    // Queues the frame into the RSX command buffer (not flushed).
    void submit(const DrawBatch& batch, u32 color_offset, u32 clear_color);

private:
    RsxBatchRenderer(const RsxBatchRenderer&);
    RsxBatchRenderer& operator=(const RsxBatchRenderer&);

    void bindSurface(u32 color_offset);

    gcmContextData* context_;
    int width_;
    int height_;
    int pitch_;
    bool ready_;
    int slot_;
    void* depth_buffer_;
    u32 depth_offset_;
    void* vertex_buffers_[2];
    u32 vertex_offsets_[2];
    void* texture_buffers_[2];
    u32 texture_offsets_[2];
    void* color_program_;
    u32 color_program_offset_;
    void* texture_program_;
    u32 texture_program_offset_;
    u8 texture_unit_;
};

} // namespace ps3
} // namespace movebeat

#endif
