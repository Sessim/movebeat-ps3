#include "framebuffer.hpp"
#include "note_cube.hpp"
#include "software_note_renderer.hpp"

#include <algorithm>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#include <rsx/gcm_sys.h>
#include <rsx/rsx.h>
#include <sysutil/video.h>

#if defined(MOVEBEAT_EXPERIMENTAL_RSX_NOTES) && !defined(_MSC_VER)
#include "note3d_vpo.h"
#include "note3d_fpo.h"
#endif

namespace movebeat {
namespace ps3 {
namespace {

const u32 kCommandBufferSize = 0x100000;
const u32 kHostMemorySize = 32 * 1024 * 1024;
const unsigned int kMaxSolidNotes = 256;
#if defined(MOVEBEAT_EXPERIMENTAL_RSX_NOTES)
// Two camera-facing side faces, four sloped bevels and the front face.
// Never submit a back face: it can show through on a degraded Z path.
const unsigned int kVerticesPerCube = 42;
struct SolidVertex {
    float position[3];
    float color[4];
};

void putSolidVertex(SolidVertex& target, const Vec3& position,
                    u32 color, float light) {
    target.position[0] = position.x;
    target.position[1] = position.y;
    target.position[2] = position.z;
    target.color[0] = ((color >> 16) & 255u) * light / 255.0f;
    target.color[1] = ((color >> 8) & 255u) * light / 255.0f;
    target.color[2] = (color & 255u) * light / 255.0f;
    target.color[3] = 1.0f;
}

void putSolidQuad(SolidVertex* target, const Vec3& a, const Vec3& b,
                  const Vec3& c, const Vec3& d, u32 color, float light) {
    putSolidVertex(target[0], a, color, light);
    putSolidVertex(target[1], b, color, light);
    putSolidVertex(target[2], c, color, light);
    putSolidVertex(target[3], a, color, light);
    putSolidVertex(target[4], c, color, light);
    putSolidVertex(target[5], d, color, light);
}
#endif
const char kVideoStagePath[] =
    "/dev_hdd0/game/MOVEBEAT1/USRDIR/video_stage.txt";

void recordVideoStage(const char* stage) {
    FILE* output = fopen(kVideoStagePath, "w");
    if (!output) return;
    fprintf(output, "stage=%s\n", stage);
    fclose(output);
}

int absolute(int value) {
    return value < 0 ? -value : value;
}

long long edge(int ax, int ay, int bx, int by, int px, int py) {
    return static_cast<long long>(px - ax) * (by - ay) -
           static_cast<long long>(py - ay) * (bx - ax);
}

bool writePpmPixels(const char* path, const u32* source, int width, int height) {
    if (!path || !path[0] || !source) return false;
    FILE* output = fopen(path, "wb");
    if (!output) return false;
    if (fprintf(output, "P6\n%d %d\n255\n", width, height) < 0) {
        fclose(output);
        return false;
    }
    std::vector<unsigned char> row(static_cast<std::size_t>(width) * 3u);
    bool complete = true;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const u32 color = source[y * width + x];
            row[static_cast<std::size_t>(x) * 3u] =
                static_cast<unsigned char>((color >> 16) & 0xffu);
            row[static_cast<std::size_t>(x) * 3u + 1u] =
                static_cast<unsigned char>((color >> 8) & 0xffu);
            row[static_cast<std::size_t>(x) * 3u + 2u] =
                static_cast<unsigned char>(color & 0xffu);
        }
        if (fwrite(&row[0], 1, row.size(), output) != row.size()) {
            complete = false;
            break;
        }
    }
    const bool closed = fclose(output) == 0;
    if (!complete || !closed) {
        remove(path);
        return false;
    }
    return true;
}

} // namespace

Framebuffer::Framebuffer()
    : context_(0), host_memory_(0), current_(0), width_(0), height_(0),
      pitch_(0), clear_color_(0xff050510u), initialized_(false), flip_submitted_(false),
      depth_buffer_(0), note_vertex_buffer_(0), note_fragment_buffer_(0),
      depth_offset_(0), note_vertex_offset_(0), note_fragment_offset_(0),
      solid_notes_ready_(false), software_depth_(0), note_depth_active_(false),
      last_solid_note_count_(0) {
    memset(buffers_, 0, sizeof(buffers_));
}

Framebuffer::~Framebuffer() {
    shutdown();
}

bool Framebuffer::makeBuffer(int index) {
    const int byte_count = pitch_ * height_;
    buffers_[index].pixels = static_cast<u32*>(rsxMemalign(64, byte_count));
    if (!buffers_[index].pixels) return false;
    if (rsxAddressToOffset(buffers_[index].pixels, &buffers_[index].offset) != 0) {
        rsxFree(buffers_[index].pixels);
        buffers_[index].pixels = 0;
        return false;
    }
    buffers_[index].id = index;
    return gcmSetDisplayBuffer(index, buffers_[index].offset, pitch_, width_, height_) == 0;
}

bool Framebuffer::initialize() {
    if (initialized_) return true;

    host_memory_ = memalign(1024 * 1024, kHostMemorySize);
    if (!host_memory_) return false;
    if (rsxInit(&context_, kCommandBufferSize, kHostMemorySize, host_memory_) != 0 ||
        !context_) {
        free(host_memory_);
        host_memory_ = 0;
        return false;
    }
    recordVideoStage("rsx_ready");

    videoState state;
    videoResolution resolution;
    if (videoGetState(VIDEO_PRIMARY, 0, &state) != 0 || state.state != 0) {
        shutdown();
        return false;
    }
    // Prefer 720p: the diagnostic renderer writes the framebuffer from the
    // PPU, so 1080p wastes bandwidth without adding useful tracking detail.
    const u32 resolution_id =
        videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720,
                                       VIDEO_ASPECT_AUTO, 0) == 1
        ? VIDEO_RESOLUTION_720 : state.displayMode.resolution;
    if (videoGetResolution(resolution_id, &resolution) != 0) {
        shutdown();
        return false;
    }
    width_ = resolution.width;
    height_ = resolution.height;
    pitch_ = width_ * static_cast<int>(sizeof(u32));

    videoConfiguration config;
    memset(&config, 0, sizeof(config));
    config.resolution = static_cast<u8>(resolution_id);
    config.format = VIDEO_BUFFER_FORMAT_XRGB;
    config.pitch = pitch_;
    config.aspect = VIDEO_ASPECT_AUTO;
    if (videoConfigure(VIDEO_PRIMARY, &config, 0, 0) != 0 ||
        !makeBuffer(0) || !makeBuffer(1)) {
        shutdown();
        return false;
    }
    recordVideoStage("buffers_ready");

    gcmSetFlipMode(GCM_FLIP_VSYNC);
    gcmResetFlipStatus();
    initialized_ = true;
    beginFrame(0xff050510u);
    recordVideoStage("first_flip_pending");
    present();
    recordVideoStage("first_flip_done");
    return true;
}

void Framebuffer::shutdown() {
    if (context_ && initialized_) gcmSetWaitFlip(context_);
    if (context_ && initialized_) rsxFinish(context_, 1);
    if (depth_buffer_) rsxFree(depth_buffer_);
    if (note_vertex_buffer_) rsxFree(note_vertex_buffer_);
    if (note_fragment_buffer_) rsxFree(note_fragment_buffer_);
    depth_buffer_ = note_vertex_buffer_ = note_fragment_buffer_ = 0;
    if (software_depth_) free(software_depth_);
    software_depth_ = 0;
    note_depth_active_ = false;
    solid_notes_ready_ = false;
    for (int i = 0; i < 2; ++i) {
        if (buffers_[i].pixels) {
            rsxFree(buffers_[i].pixels);
            buffers_[i].pixels = 0;
        }
    }
    if (context_) rsxFinish(context_, 1);
    if (host_memory_) {
        free(host_memory_);
        host_memory_ = 0;
    }
    context_ = 0;
    initialized_ = false;
    flip_submitted_ = false;
}

void Framebuffer::waitForFlip() {
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

void Framebuffer::waitForPreviousFlip() {
    if (!initialized_ || !flip_submitted_) return;
    waitForFlip();
    flip_submitted_ = false;
}

void Framebuffer::beginFrame(u32 color) {
    if (!initialized_) return;
    // The next CPU-writable buffer may still be scanned out until the
    // previous flip completes. Do not draw into it prematurely.
    waitForPreviousFlip();
    clear_color_ = color;
    note_depth_active_ = false;
    u32* destination = buffers_[current_].pixels;
    const int count = width_ * height_;
    // Both display buffers are 64-byte aligned and every supported video mode
    // has an even pixel count. Fill two identical XRGB pixels per PPE store to
    // reduce loop/store overhead on the Cell PPU; keep a one-pixel tail for
    // unusual future modes.
    const u64 pair = (static_cast<u64>(color) << 32) | color;
    u64* pairs = reinterpret_cast<u64*>(destination);
    const int pair_count = count / 2;
    for (int i = 0; i < pair_count; ++i) pairs[i] = pair;
    if (count & 1) destination[count - 1] = color;
}

void Framebuffer::bindCurrentSurface() {
#if !defined(_MSC_VER)
    gcmSurface surface;
    memset(&surface, 0, sizeof(surface));
    surface.colorFormat = GCM_SURFACE_X8R8G8B8;
    surface.colorTarget = GCM_SURFACE_TARGET_0;
    surface.colorLocation[0] = GCM_LOCATION_RSX;
    surface.colorOffset[0] = buffers_[current_].offset;
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
    const float scale[4] = {width_ * 0.5f, height_ * -0.5f, 0.5f, 0.0f};
    const float offset[4] = {width_ * 0.5f, height_ * 0.5f, 0.5f, 0.0f};
    rsxSetViewport(context_, 0, 0, width_, height_, 0.0f, 1.0f, scale, offset);
    rsxSetScissor(context_, 0, 0, width_, height_);
    for (unsigned int clip = 0; clip < 8; ++clip)
        rsxSetViewportClip(context_, clip, width_, height_);
#endif
}

bool Framebuffer::initializeSolidNotes() {
#if !defined(MOVEBEAT_EXPERIMENTAL_RSX_NOTES) || defined(_MSC_VER)
    return false;
#else
    if (solid_notes_ready_) return true;
    if (!initialized_) return false;
    const rsxFragmentProgram* fragment =
        reinterpret_cast<const rsxFragmentProgram*>(note3d_fpo);
    void* fragment_code = 0;
    u32 fragment_size = 0;
    rsxFragmentProgramGetUCode(fragment, &fragment_code, &fragment_size);
    if (!fragment_code || !fragment_size) return false;
    depth_buffer_ = rsxMemalign(64, pitch_ * height_);
    note_vertex_buffer_ = rsxMemalign(
        128, kMaxSolidNotes * kVerticesPerCube * sizeof(SolidVertex));
    note_fragment_buffer_ = rsxMemalign(64, fragment_size);
    bool ready = depth_buffer_ && note_vertex_buffer_ && note_fragment_buffer_;
    if (ready) {
        memcpy(note_fragment_buffer_, fragment_code, fragment_size);
        ready = rsxAddressToOffset(depth_buffer_, &depth_offset_) == 0 &&
                rsxAddressToOffset(note_vertex_buffer_, &note_vertex_offset_) == 0 &&
                rsxAddressToOffset(note_fragment_buffer_, &note_fragment_offset_) == 0;
    }
    if (!ready) {
        if (depth_buffer_) rsxFree(depth_buffer_);
        if (note_vertex_buffer_) rsxFree(note_vertex_buffer_);
        if (note_fragment_buffer_) rsxFree(note_fragment_buffer_);
        depth_buffer_ = note_vertex_buffer_ = note_fragment_buffer_ = 0;
        depth_offset_ = note_vertex_offset_ = note_fragment_offset_ = 0;
        return false;
    }
    solid_notes_ready_ = true;
    return true;
#endif
}

bool Framebuffer::drawSolidNotes(const movebeat::Game& game, unsigned int now_ms) {
    last_solid_note_count_ = 0;
#if !defined(MOVEBEAT_EXPERIMENTAL_RSX_NOTES)
    if (!initialized_) return false;
    const std::size_t pixel_count = static_cast<std::size_t>(width_) * height_;
    if (!software_depth_) {
        software_depth_ = static_cast<float*>(malloc(pixel_count * sizeof(float)));
        if (!software_depth_) return false;
    }
    memset(software_depth_, 0, pixel_count * sizeof(float));
    note_depth_active_ = true;
    const SoftwareNoteTarget target = {
        buffers_[current_].pixels, software_depth_, width_, height_
    };
    for (std::size_t index = 0; index < game.noteCount(); ++index) {
        if (!game.notePending(index)) continue;
        const Note& note = game.note(index);
        if (note.kind == NOTE_BOMB) continue;
        const Vec3 position = game.notePosition(note, now_ms);
        if (position.z < -0.8f || position.z > game.noteSpawnDepth()) continue;
        if (last_solid_note_count_ >= kMaxSolidNotes) break;
        const u32 color = note.hand == HAND_LEFT ? 0xff2098e8u : 0xffe83048u;
        const float side = note.kind == NOTE_CHAIN ? 0.16f : 0.32f;
        rasterizeNoteCube(target, makeNoteCube(
            position, side, width_, height_, gameplayNoteDepth(side)), color);
        ++last_solid_note_count_;
    }
    return true;
#elif defined(_MSC_VER)
    (void)game;
    (void)now_ms;
    return false;
#else
    if (!initializeSolidNotes()) return false;
    SolidVertex* vertices = static_cast<SolidVertex*>(note_vertex_buffer_);
    struct VisibleNote {
        const Note* note;
        Vec3 position;
    };
    VisibleNote visible[kMaxSolidNotes];
    unsigned int count = 0;
    for (std::size_t index = 0; index < game.noteCount(); ++index) {
        if (!game.notePending(index)) continue;
        const Note& note = game.note(index);
        if (note.kind == NOTE_BOMB) continue;
        const Vec3 position = game.notePosition(note, now_ms);
        if (position.z < -0.8f || position.z > game.noteSpawnDepth()) continue;
        if (count >= kMaxSolidNotes) break;
        visible[count].note = &note;
        visible[count].position = position;
        ++count;
    }
    // Charts are time-ordered, hence nearer notes were collected first.
    // Submit far-to-near so a nearer front face covers a farther cube even if
    // a driver's depth state is degraded; the Z-buffer handles intersecting
    // faces within each solid.
    unsigned int vertex_count = 0;
    for (unsigned int draw_index = 0; draw_index < count; ++draw_index) {
        const VisibleNote& item = visible[count - 1u - draw_index];
        const Note& note = *item.note;
        const u32 color = note.hand == HAND_LEFT ? 0xff2098e8u : 0xffe83048u;
        const float side = note.kind == NOTE_CHAIN ? 0.16f : 0.32f;
        const float depth = gameplayNoteDepth(side);
        const NoteCubeMesh cube = makeNoteCube(
            item.position, side, width_, height_, depth);
        const NoteCubeVisibleFaces faces = visibleNoteCubeFaces(cube);
        SolidVertex* output = vertices + vertex_count;
        // Camera is at (cameraX, eyeHeight + cameraOffset, -projectionDepth).
        // The travel-facing front face is
        // always visible; select at most one X and one Y face by the camera's
        // position relative to this cube. Hidden faces must not rely on Z.
        if (faces.left) {
            putSolidQuad(output, cube.world[4], cube.world[0],
                         cube.world[3], cube.world[7], color, 0.50f);
            output += 6;
            vertex_count += 6;
        } else if (faces.right) {
            putSolidQuad(output, cube.world[1], cube.world[5],
                         cube.world[6], cube.world[2], color, 0.54f);
            output += 6;
            vertex_count += 6;
        }
        if (faces.top) {
            putSolidQuad(output, cube.world[0], cube.world[1],
                         cube.world[5], cube.world[4], color, 0.84f);
            output += 6;
            vertex_count += 6;
        } else if (faces.bottom) {
            putSolidQuad(output, cube.world[3], cube.world[2],
                         cube.world[6], cube.world[7], color, 0.42f);
            output += 6;
            vertex_count += 6;
        }
        putSolidQuad(output, cube.front[0], cube.front[1],
                     cube.front[2], cube.front[3], color, 1.00f);
        vertex_count += 6;
    }
    bindCurrentSurface();
    // The PPU's writes to an RSX color surface are not a reliable render-target
    // clear in RPCS3: the GPU can retain earlier cube pixels in its cached
    // surface. Clear COLOR on the GPU itself every frame, including frames
    // with zero visible notes, so a moving cube cannot leave old positions.
    rsxSetClearColor(context_, clear_color_);
    rsxSetClearDepthStencil(context_, 0xffffff00u);
    rsxClearSurface(context_, GCM_CLEAR_M);
    if (!count) {
        rsxFinish(context_, 1);
        return true;
    }
    rsxSetZMinMaxControl(context_, GCM_FALSE, GCM_TRUE, GCM_FALSE);
    rsxSetDepthTestEnable(context_, GCM_TRUE);
    rsxSetDepthFunc(context_, GCM_LESS);
    rsxSetDepthWriteEnable(context_, 1);
    rsxSetCullFaceEnable(context_, GCM_FALSE);
    rsxSetBlendEnable(context_, GCM_FALSE);
    rsxSetColorMask(context_, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G |
                              GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);

    const rsxVertexProgram* vertex =
        reinterpret_cast<const rsxVertexProgram*>(note3d_vpo);
    const rsxFragmentProgram* fragment =
        reinterpret_cast<const rsxFragmentProgram*>(note3d_fpo);
    void* vertex_code = 0;
    u32 vertex_size = 0;
    rsxVertexProgramGetUCode(vertex, &vertex_code, &vertex_size);
    rsxLoadVertexProgram(context_, vertex, vertex_code);
    const rsxProgramConst* camera_parameter =
        rsxVertexProgramGetConst(vertex, "camera");
    static bool printed_gpu_setup = false;
    if (!printed_gpu_setup) {
        printed_gpu_setup = true;
        printf("RSX note pass: cubes=%u vertex_bytes=%u fragment_offset=%08X camera_const=%p\n",
               count, vertex_size, note_fragment_offset_, camera_parameter);
    }
    const float camera[4] = {
        height_ * kProjectionFocalHeight * kProjectionDepth * 2.0f / width_,
        kProjectionFocalHeight * kProjectionDepth * 2.0f,
        kProjectionEyeHeight, kProjectionDepth
    };
    rsxLoadVertexProgramParameterBlock(context_, 0, 1, camera);
    rsxLoadFragmentProgramLocation(context_, fragment, note_fragment_offset_,
                                   GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(context_, GCM_VERTEX_ATTRIB_POS, 0,
                             note_vertex_offset_, sizeof(SolidVertex), 3,
                             GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(context_, GCM_VERTEX_ATTRIB_COLOR0, 0,
                             note_vertex_offset_ + offsetof(SolidVertex, color),
                             sizeof(SolidVertex), 4,
                             GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxDrawVertexArray(context_, GCM_TYPE_TRIANGLES, 0, vertex_count);
    // The remaining HUD and direction symbols are written by the PPU into
    // this same color buffer, so finish the RSX pass before touching them.
    rsxFinish(context_, 1);
    last_solid_note_count_ = count;
    return true;
#endif
}

void Framebuffer::present() {
    if (!initialized_) return;
    if (gcmSetFlip(context_, buffers_[current_].id) == 0) {
        rsxFlushBuffer(context_);
        gcmSetWaitFlip(context_);
        flip_submitted_ = true;
        current_ = (current_ + 1) % 2;
    }
}

bool Framebuffer::savePpm(const char* path) const {
    return initialized_ && writePpmPixels(
        path, buffers_[current_].pixels, width_, height_);
}

void Framebuffer::pixel(int x, int y, u32 color) {
    if (!initialized_ || x < 0 || y < 0 || x >= width_ || y >= height_) return;
    buffers_[current_].pixels[y * width_ + x] = color;
}

void Framebuffer::line(int x0, int y0, int x1, int y1, u32 color, int thickness) {
    const int dx = absolute(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -absolute(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    const int radius = thickness > 1 ? thickness / 2 : 0;

    for (;;) {
        rectangle(x0 - radius, y0 - radius, radius * 2 + 1, radius * 2 + 1, color);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = error * 2;
        if (doubled >= dy) { error += dy; x0 += sx; }
        if (doubled <= dx) { error += dx; y0 += sy; }
    }
}

void Framebuffer::rectangle(int x, int y, int width, int height, u32 color) {
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            pixel(x + column, y + row, color);
        }
    }
}

void Framebuffer::triangle(int x0, int y0, int x1, int y1,
                           int x2, int y2, u32 color) {
    if (!initialized_) return;
    const long long area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    const int left = std::max(0, std::min(x0, std::min(x1, x2)));
    const int right = std::min(width_ - 1, std::max(x0, std::max(x1, x2)));
    const int top = std::max(0, std::min(y0, std::min(y1, y2)));
    const int bottom = std::min(height_ - 1, std::max(y0, std::max(y1, y2)));
    u32* pixels = buffers_[current_].pixels;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const long long a = edge(x0, y0, x1, y1, x, y);
            const long long b = edge(x1, y1, x2, y2, x, y);
            const long long c = edge(x2, y2, x0, y0, x, y);
            if ((area > 0 && a >= 0 && b >= 0 && c >= 0) ||
                (area < 0 && a <= 0 && b <= 0 && c <= 0)) {
                pixels[y * width_ + x] = color;
            }
        }
    }
}

void Framebuffer::quadrilateral(int x0, int y0, int x1, int y1,
                                int x2, int y2, int x3, int y3, u32 color) {
    triangle(x0, y0, x1, y1, x2, y2, color);
    triangle(x0, y0, x2, y2, x3, y3, color);
}

void Framebuffer::circle(int center_x, int center_y, int radius, u32 color) {
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) pixel(center_x + x, center_y + y, color);
        }
    }
}

void Framebuffer::noteLine(int x0, int y0, int x1, int y1, u32 color,
                           int thickness, float camera_depth) {
    if (!note_depth_active_) {
        line(x0, y0, x1, y1, color, thickness);
        return;
    }
    const SoftwareNoteTarget target = {
        buffers_[current_].pixels, software_depth_, width_, height_
    };
    const float distance = camera_depth;
    if (!std::isfinite(distance) || distance <= 0.1f) return;
    const float inverse_depth = 1.0f / distance;
    const int dx = absolute(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -absolute(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    const int radius = thickness > 1 ? thickness / 2 : 0;
    for (;;) {
        for (int y = y0 - radius; y <= y0 + radius; ++y)
            for (int x = x0 - radius; x <= x0 + radius; ++x)
                paintNoteInverseDepthPixel(target, x, y, inverse_depth, color);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = error * 2;
        if (doubled >= dy) { error += dy; x0 += sx; }
        if (doubled <= dx) { error += dx; y0 += sy; }
    }
}

void Framebuffer::noteCircle(int center_x, int center_y, int radius,
                             u32 color, float camera_depth) {
    if (!note_depth_active_) {
        circle(center_x, center_y, radius, color);
        return;
    }
    const SoftwareNoteTarget target = {
        buffers_[current_].pixels, software_depth_, width_, height_
    };
    const float distance = camera_depth;
    if (!std::isfinite(distance) || distance <= 0.1f) return;
    const float inverse_depth = 1.0f / distance;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius)
                paintNoteInverseDepthPixel(target, center_x + x, center_y + y,
                                           inverse_depth, color);
}

} // namespace ps3
} // namespace movebeat
