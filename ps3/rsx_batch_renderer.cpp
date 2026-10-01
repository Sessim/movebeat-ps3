#include "rsx_batch_renderer.hpp"

#include <stddef.h>
#include <string.h>

#include <rsx/gcm_sys.h>

#if !defined(MOVEBEAT_PS3_STUBS)
#include "batch_vpo.h"
#include "batch_color_fpo.h"
#include "batch_texture_fpo.h"
#endif

namespace movebeat {
namespace ps3 {

#if defined(MOVEBEAT_PS3_STUBS)

// Host syntax builds have no RSX: report "not ready" so Framebuffer keeps
// its software path.
RsxBatchRenderer::RsxBatchRenderer()
    : context_(0), width_(0), height_(0), pitch_(0), ready_(false), slot_(0),
      depth_buffer_(0), depth_offset_(0), color_program_(0),
      color_program_offset_(0), texture_program_(0),
      texture_program_offset_(0), texture_unit_(0) {
    vertex_buffers_[0] = vertex_buffers_[1] = 0;
    texture_buffers_[0] = texture_buffers_[1] = 0;
    vertex_offsets_[0] = vertex_offsets_[1] = 0;
    texture_offsets_[0] = texture_offsets_[1] = 0;
}
RsxBatchRenderer::~RsxBatchRenderer() {}
bool RsxBatchRenderer::initialize(gcmContextData*, int, int) { return false; }
void RsxBatchRenderer::shutdown() {}
void RsxBatchRenderer::submit(const DrawBatch&, u32, u32) {}
void RsxBatchRenderer::bindSurface(u32) {}

#else

namespace {

const std::size_t kTextureBytes =
    static_cast<std::size_t>(RsxBatchRenderer::kMaxTextureWidth) *
    RsxBatchRenderer::kMaxTextureHeight * 4u;

const rsxVertexProgram* vertexProgram() {
    return reinterpret_cast<const rsxVertexProgram*>(batch_vpo);
}

const rsxFragmentProgram* colorProgram() {
    return reinterpret_cast<const rsxFragmentProgram*>(batch_color_fpo);
}

const rsxFragmentProgram* textureProgram() {
    return reinterpret_cast<const rsxFragmentProgram*>(batch_texture_fpo);
}

// Fragment programs execute from RSX memory: copy the microcode there.
bool uploadFragmentProgram(const rsxFragmentProgram* program,
                           void*& memory, u32& offset) {
    void* code = 0;
    u32 size = 0;
    rsxFragmentProgramGetUCode(program, &code, &size);
    if (!code || !size) return false;
    memory = rsxMemalign(64, size);
    if (!memory) return false;
    memcpy(memory, code, size);
    return rsxAddressToOffset(memory, &offset) == 0;
}

void* allocate(std::size_t alignment, std::size_t size, u32& offset) {
    void* memory = rsxMemalign(static_cast<u32>(alignment),
                               static_cast<u32>(size));
    if (memory && rsxAddressToOffset(memory, &offset) != 0) {
        rsxFree(memory);
        return 0;
    }
    return memory;
}

void release(void*& memory) {
    if (memory) rsxFree(memory);
    memory = 0;
}

} // namespace

RsxBatchRenderer::RsxBatchRenderer()
    : context_(0), width_(0), height_(0), pitch_(0), ready_(false), slot_(0),
      depth_buffer_(0), depth_offset_(0), color_program_(0),
      color_program_offset_(0), texture_program_(0),
      texture_program_offset_(0), texture_unit_(0) {
    vertex_buffers_[0] = vertex_buffers_[1] = 0;
    texture_buffers_[0] = texture_buffers_[1] = 0;
    vertex_offsets_[0] = vertex_offsets_[1] = 0;
    texture_offsets_[0] = texture_offsets_[1] = 0;
}

RsxBatchRenderer::~RsxBatchRenderer() {
    shutdown();
}

bool RsxBatchRenderer::initialize(gcmContextData* context, int width,
                                  int height) {
    shutdown();
    if (!context || width <= 0 || height <= 0) return false;
    context_ = context;
    width_ = width;
    height_ = height;
    pitch_ = width * 4;
    bool ok = (depth_buffer_ = allocate(64, static_cast<std::size_t>(pitch_) *
                                        height_, depth_offset_)) != 0;
    for (int slot = 0; ok && slot < 2; ++slot) {
        vertex_buffers_[slot] = allocate(
            128, kVertexCapacity * sizeof(BatchVertex), vertex_offsets_[slot]);
        texture_buffers_[slot] = allocate(
            128, kTextureCapacity * kTextureBytes, texture_offsets_[slot]);
        ok = vertex_buffers_[slot] && texture_buffers_[slot];
    }
    ok = ok &&
        uploadFragmentProgram(colorProgram(), color_program_,
                              color_program_offset_) &&
        uploadFragmentProgram(textureProgram(), texture_program_,
                              texture_program_offset_);
    const rsxProgramAttrib* sampler =
        ok ? rsxFragmentProgramGetAttrib(textureProgram(), "texture") : 0;
    ok = ok && sampler;
    if (!ok) {
        shutdown();
        return false;
    }
    texture_unit_ = static_cast<u8>(sampler->index);
    ready_ = true;
    return true;
}

void RsxBatchRenderer::shutdown() {
    if (context_ && ready_) rsxFinish(context_, 1);
    release(depth_buffer_);
    for (int slot = 0; slot < 2; ++slot) {
        release(vertex_buffers_[slot]);
        release(texture_buffers_[slot]);
    }
    release(color_program_);
    release(texture_program_);
    ready_ = false;
}

void RsxBatchRenderer::bindSurface(u32 color_offset) {
    gcmSurface surface;
    memset(&surface, 0, sizeof(surface));
    surface.colorFormat = GCM_SURFACE_X8R8G8B8;
    surface.colorTarget = GCM_SURFACE_TARGET_0;
    surface.colorLocation[0] = GCM_LOCATION_RSX;
    surface.colorOffset[0] = color_offset;
    surface.colorPitch[0] = pitch_;
    for (int index = 1; index < 4; ++index) {
        surface.colorLocation[index] = GCM_LOCATION_RSX;
        surface.colorPitch[index] = 64;
    }
    surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
    surface.depthLocation = GCM_LOCATION_RSX;
    surface.depthOffset = depth_offset_;
    surface.depthPitch = pitch_;
    surface.type = GCM_SURFACE_TYPE_LINEAR;
    surface.antiAlias = GCM_SURFACE_CENTER_1;
    surface.width = width_;
    surface.height = height_;
    rsxSetSurface(context_, &surface);
    // Top-left pixel origin: NDC y = +1 maps to row 0.
    const float scale[4] = {width_ * 0.5f, height_ * -0.5f, 0.5f, 0.0f};
    const float offset[4] = {width_ * 0.5f, height_ * 0.5f, 0.5f, 0.0f};
    rsxSetViewport(context_, 0, 0, width_, height_, 0.0f, 1.0f, scale, offset);
    rsxSetScissor(context_, 0, 0, width_, height_);
    for (unsigned int clip = 0; clip < 8; ++clip)
        rsxSetViewportClip(context_, clip, width_, height_);
}

void RsxBatchRenderer::submit(const DrawBatch& batch, u32 color_offset,
                              u32 clear_color) {
    if (!ready_) return;
    // Framebuffer waits for the previous flip before recording a frame, so
    // the RSX has consumed the slot used two frames ago.
    slot_ ^= 1;
    const std::vector<BatchVertex>& vertices = batch.vertices();
    std::size_t vertex_count = vertices.size();
    if (vertex_count > kVertexCapacity) vertex_count = kVertexCapacity;
    if (vertex_count)
        memcpy(vertex_buffers_[slot_], &vertices[0],
               vertex_count * sizeof(BatchVertex));
    const std::vector<BatchTexture>& textures = batch.textures();
    unsigned char* texture_memory =
        static_cast<unsigned char*>(texture_buffers_[slot_]);
    for (std::size_t index = 0;
         index < textures.size() && index < kTextureCapacity; ++index) {
        const BatchTexture& texture = textures[index];
        if (texture.width > kMaxTextureWidth ||
            texture.height > kMaxTextureHeight) continue;
        memcpy(texture_memory + index * kTextureBytes, &texture.pixels[0],
               texture.pixels.size() * 4u);
    }

    bindSurface(color_offset);
    rsxSetClearColor(context_, clear_color);
    rsxSetClearDepthStencil(context_, 0xffffff00u);
    rsxClearSurface(context_, GCM_CLEAR_M);
    if (!vertex_count) return;

    rsxInvalidateVertexCache(context_);
    rsxInvalidateTextureCache(context_, GCM_INVALIDATE_TEXTURE);
    rsxSetZMinMaxControl(context_, GCM_FALSE, GCM_TRUE, GCM_FALSE);
    rsxSetCullFaceEnable(context_, GCM_FALSE);
    rsxSetBlendEnable(context_, GCM_FALSE);
    rsxSetColorMask(context_, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G |
                              GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);

    void* vertex_code = 0;
    u32 vertex_size = 0;
    rsxVertexProgramGetUCode(vertexProgram(), &vertex_code, &vertex_size);
    rsxLoadVertexProgram(context_, vertexProgram(), vertex_code);
    const u32 base = vertex_offsets_[slot_];
    const u8 stride = static_cast<u8>(sizeof(BatchVertex));
    rsxBindVertexArrayAttrib(context_, GCM_VERTEX_ATTRIB_POS, 0,
                             base + offsetof(BatchVertex, position), stride, 4,
                             GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(context_, GCM_VERTEX_ATTRIB_COLOR0, 0,
                             base + offsetof(BatchVertex, color), stride, 4,
                             GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(context_, GCM_VERTEX_ATTRIB_TEX0, 0,
                             base + offsetof(BatchVertex, texcoord), stride, 2,
                             GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);

    bool texture_program_loaded = false;
    bool color_program_loaded = false;
    BatchBlend current_blend = BLEND_OPAQUE;
    const std::vector<BatchRun>& runs = batch.runs();
    for (std::size_t index = 0; index < runs.size(); ++index) {
        const BatchRun& run = runs[index];
        if (run.first_vertex >= vertex_count) break;
        std::size_t count = run.vertex_count;
        if (run.first_vertex + count > vertex_count)
            count = vertex_count - run.first_vertex;
        if (run.blend != current_blend) {
            if (run.blend == BLEND_OPAQUE) {
                rsxSetBlendEnable(context_, GCM_FALSE);
            } else {
                // Destination alpha is unused (X8R8G8B8), so only the color
                // factors matter.
                const u16 destination = run.blend == BLEND_ADD
                    ? GCM_ONE : GCM_ONE_MINUS_SRC_ALPHA;
                rsxSetBlendFunc(context_, GCM_SRC_ALPHA, destination,
                                GCM_SRC_ALPHA, destination);
                rsxSetBlendEquation(context_, GCM_FUNC_ADD, GCM_FUNC_ADD);
                rsxSetBlendEnable(context_, GCM_TRUE);
            }
            current_blend = run.blend;
        }
        switch (run.mode) {
            case BATCH_NOTE_SOLID:
                rsxSetDepthTestEnable(context_, GCM_TRUE);
                rsxSetDepthFunc(context_, GCM_LESS);
                rsxSetDepthWriteEnable(context_, GCM_TRUE);
                break;
            case BATCH_NOTE_GLYPH:
                rsxSetDepthTestEnable(context_, GCM_TRUE);
                rsxSetDepthFunc(context_, GCM_LEQUAL);
                rsxSetDepthWriteEnable(context_, GCM_FALSE);
                break;
            case BATCH_FLAT:
            case BATCH_TEXTURED:
                rsxSetDepthTestEnable(context_, GCM_FALSE);
                rsxSetDepthWriteEnable(context_, GCM_FALSE);
                break;
        }
        if (run.mode == BATCH_TEXTURED) {
            if (run.texture < 0 ||
                static_cast<std::size_t>(run.texture) >= kTextureCapacity ||
                static_cast<std::size_t>(run.texture) >= textures.size())
                continue;
            const BatchTexture& image = textures[run.texture];
            if (image.width > kMaxTextureWidth ||
                image.height > kMaxTextureHeight) continue;
            gcmTexture texture;
            memset(&texture, 0, sizeof(texture));
            texture.format = GCM_TEXTURE_FORMAT_A8R8G8B8 |
                             GCM_TEXTURE_FORMAT_LIN;
            texture.mipmap = 1;
            texture.dimension = GCM_TEXTURE_DIMS_2D;
            texture.cubemap = GCM_FALSE;
            texture.remap =
                (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
                (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
                (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
                (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
                (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) |
                (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
                (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
                (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT);
            texture.width = static_cast<u16>(image.width);
            texture.height = static_cast<u16>(image.height);
            texture.depth = 1;
            texture.location = GCM_LOCATION_RSX;
            texture.pitch = static_cast<u32>(image.width) * 4u;
            texture.offset = texture_offsets_[slot_] +
                static_cast<u32>(run.texture * kTextureBytes);
            rsxLoadTexture(context_, texture_unit_, &texture);
            rsxTextureControl(context_, texture_unit_, GCM_TRUE, 0 << 8,
                              12 << 8, GCM_TEXTURE_MAX_ANISO_1);
            rsxTextureFilter(context_, texture_unit_, 0, GCM_TEXTURE_NEAREST,
                             GCM_TEXTURE_NEAREST,
                             GCM_TEXTURE_CONVOLUTION_QUINCUNX);
            rsxTextureWrapMode(context_, texture_unit_,
                               GCM_TEXTURE_CLAMP_TO_EDGE,
                               GCM_TEXTURE_CLAMP_TO_EDGE,
                               GCM_TEXTURE_CLAMP_TO_EDGE, 0,
                               GCM_TEXTURE_ZFUNC_NEVER, 0);
            if (!texture_program_loaded) {
                rsxLoadFragmentProgramLocation(context_, textureProgram(),
                                               texture_program_offset_,
                                               GCM_LOCATION_RSX);
                texture_program_loaded = true;
                color_program_loaded = false;
            }
        } else if (!color_program_loaded) {
            rsxLoadFragmentProgramLocation(context_, colorProgram(),
                                           color_program_offset_,
                                           GCM_LOCATION_RSX);
            color_program_loaded = true;
            texture_program_loaded = false;
        }
        rsxDrawVertexArray(context_, GCM_TYPE_TRIANGLES,
                           static_cast<u32>(run.first_vertex),
                           static_cast<u32>(count));
    }
}

#endif

} // namespace ps3
} // namespace movebeat
