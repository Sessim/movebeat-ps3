#include "framebuffer.hpp"
#include "note_cube.hpp"
#include "note_spawn.hpp"
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

namespace movebeat {
namespace ps3 {
namespace {

const u32 kCommandBufferSize = 0x100000;
const u32 kHostMemorySize = 32 * 1024 * 1024;
const unsigned int kMaxSolidNotes = 256;
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
      software_depth_(0), note_depth_active_(false), last_solid_note_count_(0),
      rsx_(false), effects_(true), frame_submitted_(false),
      last_batch_vertices_(0),
      last_batch_dropped_(0) {
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

bool Framebuffer::initialize(RendererPreference preference) {
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
    // Prefer 720p: the software fallback writes every pixel from the PPU,
    // and 1080p adds nothing useful for tracking or readability.
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

    rsx_ = preference == RENDERER_PREFER_RSX &&
           gpu_.initialize(context_, width_, height_);
    recordVideoStage(rsx_ ? "renderer_rsx" : "renderer_software");

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
    gpu_.shutdown();
    rsx_ = false;
    if (software_depth_) free(software_depth_);
    software_depth_ = 0;
    note_depth_active_ = false;
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
    if (rsx_) {
        // The RSX clears the surface when the batch is submitted.
        batch_.reset(width_, height_, RsxBatchRenderer::kVertexCapacity,
                     RsxBatchRenderer::kTextureCapacity);
        frame_submitted_ = false;
        return;
    }
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

void Framebuffer::submitFrame() {
    if (!rsx_ || frame_submitted_) return;
    gpu_.submit(batch_, buffers_[current_].offset, clear_color_);
    last_batch_vertices_ = static_cast<unsigned int>(batch_.vertices().size());
    last_batch_dropped_ = batch_.droppedPrimitives();
    frame_submitted_ = true;
}

bool Framebuffer::drawSolidNotes(const movebeat::Game& game, unsigned int now_ms) {
    last_solid_note_count_ = 0;
    if (!initialized_) return false;
    SoftwareNoteTarget target = {0, 0, width_, height_};
    if (!rsx_) {
        const std::size_t pixel_count = static_cast<std::size_t>(width_) * height_;
        if (!software_depth_) {
            software_depth_ = static_cast<float*>(malloc(pixel_count * sizeof(float)));
            if (!software_depth_) return false;
        }
        memset(software_depth_, 0, pixel_count * sizeof(float));
        target.pixels = buffers_[current_].pixels;
        target.inverse_depth = software_depth_;
    }
    note_depth_active_ = true;
    for (std::size_t index = 0; index < game.noteCount(); ++index) {
        if (!game.notePending(index)) continue;
        const Note& note = game.note(index);
        if (note.kind == NOTE_BOMB) continue;
        const NoteArrival arrival = visualNote(game, note, now_ms);
        const Vec3& position = arrival.position;
        if (position.z < -0.8f || position.z > game.noteSpawnDepth()) continue;
        if (last_solid_note_count_ >= kMaxSolidNotes) break;
        const u32 color = note.hand == HAND_LEFT ? 0xff2098e8u : 0xffe83048u;
        const float side = (note.kind == NOTE_CHAIN ? 0.16f : 0.32f) *
                           arrival.scale;
        const NoteCubeMesh cube = makeNoteCube(
            position, side, width_, height_, gameplayNoteDepth(side));
        if (rsx_) forEachNoteCubeTriangle(cube, color, width_, height_, batch_);
        else rasterizeNoteCube(target, cube, color);
        ++last_solid_note_count_;
    }
    return true;
}

void Framebuffer::present() {
    if (!initialized_) return;
    submitFrame();
    if (gcmSetFlip(context_, buffers_[current_].id) == 0) {
        rsxFlushBuffer(context_);
        gcmSetWaitFlip(context_);
        flip_submitted_ = true;
        current_ = (current_ + 1) % 2;
    }
}

bool Framebuffer::savePpm(const char* path) {
    if (!initialized_) return false;
    if (rsx_) {
        // Render now and wait, so video memory holds this frame. RPCS3 only
        // mirrors GPU output there with "Write Color Buffers" enabled.
        submitFrame();
        rsxFlushBuffer(context_);
        rsxFinish(context_, 1);
    }
    return writePpmPixels(path, buffers_[current_].pixels, width_, height_);
}

void Framebuffer::pixel(int x, int y, u32 color) {
    if (!initialized_ || x < 0 || y < 0 || x >= width_ || y >= height_) return;
    if (rsx_) {
        batch_.rectangle(x, y, 1, 1, color);
        return;
    }
    buffers_[current_].pixels[y * width_ + x] = color;
}

void Framebuffer::line(int x0, int y0, int x1, int y1, u32 color, int thickness) {
    if (rsx_) {
        batch_.line(x0, y0, x1, y1, color, thickness);
        return;
    }
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
    if (rsx_) {
        batch_.rectangle(x, y, width, height, color);
        return;
    }
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            pixel(x + column, y + row, color);
        }
    }
}

void Framebuffer::triangle(int x0, int y0, int x1, int y1,
                           int x2, int y2, u32 color) {
    if (!initialized_) return;
    if (rsx_) {
        batch_.triangle(x0, y0, x1, y1, x2, y2, color);
        return;
    }
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
    if (rsx_) {
        batch_.circle(center_x, center_y, radius, color);
        return;
    }
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) pixel(center_x + x, center_y + y, color);
        }
    }
}

void Framebuffer::image(int x, int y, int width, int height,
                        const unsigned short* rgb565) {
    if (!initialized_ || !rgb565) return;
    if (rsx_) {
        batch_.image(x, y, width, height, rgb565);
        return;
    }
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const unsigned short color = rgb565[row * width + column];
            const u32 red = ((color >> 11) & 31u) * 255u / 31u;
            const u32 green = ((color >> 5) & 63u) * 255u / 63u;
            const u32 blue = (color & 31u) * 255u / 31u;
            pixel(x + column, y + row,
                  0xff000000u | (red << 16) | (green << 8) | blue);
        }
    }
}

void Framebuffer::blendQuad(const float (&x)[4], const float (&y)[4],
                            const u32 (&colors)[4], bool additive) {
    if (!initialized_ || !effectsEnabled()) return;
    const std::uint32_t batch_colors[4] = {
        colors[0], colors[1], colors[2], colors[3]
    };
    batch_.blendQuad(x, y, batch_colors, additive ? BLEND_ADD : BLEND_ALPHA);
}

void Framebuffer::gradientQuad(const float (&x)[4], const float (&y)[4],
                               const u32 (&colors)[4]) {
    if (!initialized_ || !effectsEnabled()) return;
    const std::uint32_t batch_colors[4] = {
        colors[0], colors[1], colors[2], colors[3]
    };
    batch_.blendQuad(x, y, batch_colors, BLEND_OPAQUE);
}

void Framebuffer::glowLine(float x0, float y0, float x1, float y1,
                           float half_width, u32 color) {
    if (!initialized_ || !effectsEnabled()) return;
    batch_.glowLine(x0, y0, x1, y1, half_width, color);
}

void Framebuffer::glowDisc(float center_x, float center_y, float radius,
                           u32 color) {
    if (!initialized_ || !effectsEnabled()) return;
    // Enough segments to look round at the sizes used, but no more.
    const int segments = radius < 24.0f ? 12 : 20;
    batch_.glowDisc(center_x, center_y, radius, color, segments);
}

void Framebuffer::noteLine(int x0, int y0, int x1, int y1, u32 color,
                           int thickness, float camera_depth) {
    if (!note_depth_active_) {
        line(x0, y0, x1, y1, color, thickness);
        return;
    }
    if (rsx_) {
        batch_.noteLine(x0, y0, x1, y1, color, thickness, camera_depth);
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
    if (rsx_) {
        batch_.noteCircle(center_x, center_y, radius, color, camera_depth);
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
