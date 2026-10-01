// Checks that the RSX draw batch covers the same pixels as the software
// renderer. A small reference rasterizer replays the batch the way the RSX
// samples it (pixel centers, depth modes) and the result is compared with the
// software primitives from framebuffer.cpp and software_note_renderer.hpp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "draw_batch.hpp"
#include "note_cube.hpp"
#include "note_spawn.hpp"
#include "saber_model.hpp"
#include "saber_trail.hpp"
#include "stage_layout.hpp"
#include "software_note_renderer.hpp"

using movebeat::Vec3;
using namespace movebeat::ps3;

namespace {

int failures = 0;
// Set MOVEBEAT_TEST_VERBOSE to print every measured mismatch.
const bool verbose = std::getenv("MOVEBEAT_TEST_VERBOSE") != 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    } else if (verbose) {
        std::printf("ok: %s\n", message);
    }
}

struct Image {
    int width;
    int height;
    std::vector<std::uint32_t> pixels;
    std::vector<float> depth;

    Image(int w, int h)
        : width(w), height(h), pixels(static_cast<std::size_t>(w) * h, 0u),
          depth(static_cast<std::size_t>(w) * h, 1.0f) {}
};

// Replays a batch like the RSX: inclusive edge test at pixel centers, depth
// interpolated linearly in screen space, per-run depth and texture state.
void replay(const DrawBatch& batch, Image& image) {
    const std::vector<BatchVertex>& vertices = batch.vertices();
    for (std::size_t r = 0; r < batch.runs().size(); ++r) {
        const BatchRun& run = batch.runs()[r];
        for (std::size_t first = run.first_vertex;
             first < run.first_vertex + run.vertex_count; first += 3) {
            float px[3], py[3], pz[3], pu[3], pv[3];
            for (int corner = 0; corner < 3; ++corner) {
                const BatchVertex& v = vertices[first + corner];
                px[corner] = (v.position[0] + 1.0f) * image.width * 0.5f;
                py[corner] = (1.0f - v.position[1]) * image.height * 0.5f;
                pz[corner] = (v.position[2] + 1.0f) * 0.5f;
                pu[corner] = v.texcoord[0];
                pv[corner] = v.texcoord[1];
            }
            const std::uint32_t color = 0xff000000u |
                (static_cast<std::uint32_t>(vertices[first].color[0] * 255.0f + 0.5f) << 16) |
                (static_cast<std::uint32_t>(vertices[first].color[1] * 255.0f + 0.5f) << 8) |
                static_cast<std::uint32_t>(vertices[first].color[2] * 255.0f + 0.5f);
            const float area = (px[1] - px[0]) * (py[2] - py[0]) -
                               (py[1] - py[0]) * (px[2] - px[0]);
            if (std::fabs(area) < 1e-6f) continue;
            for (int y = 0; y < image.height; ++y) {
                for (int x = 0; x < image.width; ++x) {
                    const float sx = x + 0.5f;
                    const float sy = y + 0.5f;
                    const float w0 = ((px[1] - sx) * (py[2] - sy) -
                                      (py[1] - sy) * (px[2] - sx)) / area;
                    const float w1 = ((px[2] - sx) * (py[0] - sy) -
                                      (py[2] - sy) * (px[0] - sx)) / area;
                    const float w2 = 1.0f - w0 - w1;
                    // Centers exactly on a shared edge (a quad's diagonal)
                    // can round slightly negative in both triangles, e.g.
                    // with the PPU's fused multiply-add; the RSX's fixed-point
                    // top-left rule never drops them, so tolerate that.
                    const float edge_tolerance = -1e-5f;
                    if (w0 < edge_tolerance || w1 < edge_tolerance ||
                        w2 < edge_tolerance) continue;
                    const std::size_t index =
                        static_cast<std::size_t>(y) * image.width + x;
                    const float z = w0 * pz[0] + w1 * pz[1] + w2 * pz[2];
                    std::uint32_t out = color;
                    if (run.blend != BLEND_OPAQUE) {
                        // Interpolated vertex color, then the RSX blend
                        // equation against what is already in the image.
                        float src[4];
                        for (int channel = 0; channel < 4; ++channel)
                            src[channel] =
                                w0 * vertices[first].color[channel] +
                                w1 * vertices[first + 1].color[channel] +
                                w2 * vertices[first + 2].color[channel];
                        const std::uint32_t dst = image.pixels[index];
                        out = 0xff000000u;
                        for (int channel = 0; channel < 3; ++channel) {
                            const int shift = 16 - channel * 8;
                            const float d = ((dst >> shift) & 255u) / 255.0f;
                            float value = run.blend == BLEND_ADD
                                ? d + src[channel] * src[3]
                                : src[channel] * src[3] + d * (1.0f - src[3]);
                            if (value > 1.0f) value = 1.0f;
                            if (value < 0.0f) value = 0.0f;
                            out |= static_cast<std::uint32_t>(
                                value * 255.0f + 0.5f) << shift;
                        }
                    }
                    if (run.mode == BATCH_NOTE_SOLID) {
                        if (!(z < image.depth[index])) continue;
                        image.depth[index] = z;
                    } else if (run.mode == BATCH_NOTE_GLYPH) {
                        if (!(z <= image.depth[index])) continue;
                    } else if (run.mode == BATCH_TEXTURED) {
                        const BatchTexture& texture =
                            batch.textures()[static_cast<std::size_t>(run.texture)];
                        const float u = w0 * pu[0] + w1 * pu[1] + w2 * pu[2];
                        const float v = w0 * pv[0] + w1 * pv[1] + w2 * pv[2];
                        int tx = static_cast<int>(u * texture.width);
                        int ty = static_cast<int>(v * texture.height);
                        if (tx >= texture.width) tx = texture.width - 1;
                        if (ty >= texture.height) ty = texture.height - 1;
                        out = texture.pixels[static_cast<std::size_t>(ty) *
                                             texture.width + tx];
                    }
                    image.pixels[index] = out;
                }
            }
        }
    }
}

// --- Software oracles, copied from Framebuffer's software primitives. ---

void softwarePixel(Image& image, int x, int y, std::uint32_t color) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return;
    image.pixels[static_cast<std::size_t>(y) * image.width + x] = color;
}

void softwareRectangle(Image& image, int x, int y, int w, int h,
                       std::uint32_t color) {
    for (int row = 0; row < h; ++row)
        for (int column = 0; column < w; ++column)
            softwarePixel(image, x + column, y + row, color);
}

void softwareLine(Image& image, int x0, int y0, int x1, int y1,
                  std::uint32_t color, int thickness) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    const int radius = thickness > 1 ? thickness / 2 : 0;
    for (;;) {
        softwareRectangle(image, x0 - radius, y0 - radius,
                          radius * 2 + 1, radius * 2 + 1, color);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = error * 2;
        if (doubled >= dy) { error += dy; x0 += sx; }
        if (doubled <= dx) { error += dx; y0 += sy; }
    }
}

long long edge(int ax, int ay, int bx, int by, int px, int py) {
    return static_cast<long long>(px - ax) * (by - ay) -
           static_cast<long long>(py - ay) * (bx - ax);
}

void softwareTriangle(Image& image, int x0, int y0, int x1, int y1,
                      int x2, int y2, std::uint32_t color) {
    const long long area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            const long long a = edge(x0, y0, x1, y1, x, y);
            const long long b = edge(x1, y1, x2, y2, x, y);
            const long long c = edge(x2, y2, x0, y0, x, y);
            if ((area > 0 && a >= 0 && b >= 0 && c >= 0) ||
                (area < 0 && a <= 0 && b <= 0 && c <= 0))
                softwarePixel(image, x, y, color);
        }
}

void softwareCircle(Image& image, int cx, int cy, int radius,
                    std::uint32_t color) {
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius)
                softwarePixel(image, cx + x, cy + y, color);
}

// Pixels whose colors differ between two images.
int differingPixels(const Image& a, const Image& b) {
    int count = 0;
    for (std::size_t index = 0; index < a.pixels.size(); ++index)
        if (a.pixels[index] != b.pixels[index]) ++count;
    return count;
}

int coloredPixels(const Image& image) {
    int count = 0;
    for (std::size_t index = 0; index < image.pixels.size(); ++index)
        if (image.pixels[index] != 0u) ++count;
    return count;
}

DrawBatch makeBatch(int width, int height) {
    DrawBatch batch;
    batch.reset(width, height, 65536, 4);
    return batch;
}

void testRectangleIsExact() {
    DrawBatch batch = makeBatch(64, 48);
    batch.rectangle(3, 5, 17, 9, 0xff112233u);
    batch.rectangle(-4, 40, 10, 20, 0xff445566u);  // clipped at edges
    batch.rectangle(10, 10, 0, 5, 0xffffffffu);    // empty
    Image gpu(64, 48), cpu(64, 48);
    replay(batch, gpu);
    softwareRectangle(cpu, 3, 5, 17, 9, 0xff112233u);
    softwareRectangle(cpu, -4, 40, 10, 20, 0xff445566u);
    check(differingPixels(gpu, cpu) == 0, "rectangles cover identical pixels");
}

void testTriangleMatchesSoftware() {
    DrawBatch batch = makeBatch(96, 96);
    batch.triangle(10, 10, 80, 20, 30, 85, 0xffa0b0c0u);
    batch.triangle(90, 90, 50, 60, 70, 40, 0xff203040u);
    Image gpu(96, 96), cpu(96, 96);
    replay(batch, gpu);
    softwareTriangle(cpu, 10, 10, 80, 20, 30, 85, 0xffa0b0c0u);
    softwareTriangle(cpu, 90, 90, 50, 60, 70, 40, 0xff203040u);
    // Only boundary pixels may differ (edge inclusion rules).
    const int diff = differingPixels(gpu, cpu);
    char message[80];
    std::snprintf(message, sizeof(message),
                  "triangles differ only on edges (%d of %d)",
                  diff, coloredPixels(cpu));
    check(diff * 25 < coloredPixels(cpu), message);
}

void testLinesMatchSoftware() {
    const int thickness[] = {1, 2, 3, 5, 15};
    const int ends[][4] = {
        {5, 40, 120, 40}, {60, 3, 60, 90}, {10, 10, 110, 85},
        {100, 12, 20, 70}, {30, 80, 34, 5}, {50, 50, 50, 50}
    };
    for (int t = 0; t < 5; ++t) {
        for (int e = 0; e < 6; ++e) {
            DrawBatch batch = makeBatch(128, 96);
            batch.line(ends[e][0], ends[e][1], ends[e][2], ends[e][3],
                       0xffffffffu, thickness[t]);
            Image gpu(128, 96), cpu(128, 96);
            replay(batch, gpu);
            softwareLine(cpu, ends[e][0], ends[e][1], ends[e][2], ends[e][3],
                         0xffffffffu, thickness[t]);
            const int diff = differingPixels(gpu, cpu);
            const int area = coloredPixels(cpu);
            const bool axis = ends[e][0] == ends[e][2] || ends[e][1] == ends[e][3];
            if (axis) {
                check(diff == 0, "axis-aligned lines are exact");
            } else {
                // A diagonal staircase and its straight band differ at the
                // steps; keep the mismatch well under the line's own area.
                char message[96];
                std::snprintf(message, sizeof(message),
                              "diagonal line t=%d e=%d close (%d of %d)",
                              thickness[t], e, diff, area);
                check(diff * 3 <= area, message);
            }
        }
    }
}

void testCirclesMatchSoftware() {
    const int radii[] = {0, 1, 2, 3, 7, 11, 25};
    for (int r = 0; r < 7; ++r) {
        DrawBatch batch = makeBatch(80, 80);
        batch.circle(40, 40, radii[r], 0xffffffffu);
        Image gpu(80, 80), cpu(80, 80);
        replay(batch, gpu);
        softwareCircle(cpu, 40, 40, radii[r], 0xffffffffu);
        const int diff = differingPixels(gpu, cpu);
        char message[64];
        std::snprintf(message, sizeof(message), "circle r=%d close (%d)",
                      radii[r], diff);
        // Boundary pixels of a lattice disc may flip; the disc itself holds.
        check(diff <= 2 + radii[r], message);
    }
}

void testDepthMapping() {
    check(batchDepthFromInverse(0.0f) == 1.0f, "cleared far plane is depth 1");
    check(batchDepthFromInverse(kBatchMaxInverseDepth) == 0.0f,
          "nearest allowed note is depth 0");
    check(batchDepthFromInverse(0.5f) > batchDepthFromInverse(0.6f),
          "nearer notes get smaller RSX depth");
    check(batchDepthFromInverse(-1.0f) == 1.0f &&
          batchDepthFromInverse(20.0f) == 0.0f, "depth is clamped");
}

// Renders overlapping notes with arrows through both renderers.
void testNoteCubesMatchSoftware() {
    const int width = 320;
    const int height = 180;
    const Vec3 centers[] = {
        Vec3(-0.3f, 0.0f, 1.4f), Vec3(-0.25f, 0.05f, 0.9f),
        Vec3(0.4f, -0.3f, 0.6f), Vec3(0.2f, 0.35f, 2.2f),
        Vec3(-0.6f, 0.3f, 0.2f)
    };
    const std::uint32_t colors[] = {
        0xff2098e8u, 0xffe83048u, 0xff2098e8u, 0xffe83048u, 0xff2098e8u
    };
    Image cpu(width, height);
    std::vector<float> inverse(static_cast<std::size_t>(width) * height, 0.0f);
    const SoftwareNoteTarget target = {&cpu.pixels[0], &inverse[0], width, height};
    DrawBatch batch = makeBatch(width, height);
    const float side = 0.32f;
    const float depth = gameplayNoteDepth(side);
    for (int n = 0; n < 5; ++n) {
        rasterizeRoundedNote(target, centers[n], side, depth, colors[n]);
        forEachRoundedNoteTriangle(centers[n], side, depth, colors[n], width,
                                   height, batch);
    }
    // Arrows far-to-near, like drawNotes, on each note's front plane.
    for (int n = 4; n >= 0; --n) {
        const NoteCubeMesh cube = makeNoteCube(centers[n], side, width, height,
                                               depth);
        const float plane_z = centers[n].z - depth * 0.5f;
        const int x = cube.front_center.x;
        const int y = cube.front_center.y;
        batch.noteLine(x - 4, y, x + 4, y, 0xfff0f0f0u, 3, plane_z);
        for (int px = x - 5; px <= x + 5; ++px)
            for (int py = y - 1; py <= y + 1; ++py)
                paintNoteDepthPixel(target, px, py, plane_z, 0xfff0f0f0u);
    }
    Image gpu(width, height);
    replay(batch, gpu);
    // The RSX shades smoothly per vertex while the PPU fills each small
    // triangle with one shade, so compare what each pixel shows: nothing,
    // a glyph, or a note body.
    int diff = 0;
    int area = 0;
    for (std::size_t index = 0; index < cpu.pixels.size(); ++index) {
        const std::uint32_t a = cpu.pixels[index];
        const std::uint32_t b = gpu.pixels[index];
        const int class_a = a == 0u ? 0 : (a == 0xfff0f0f0u ? 1 : 2);
        const int class_b = b == 0u ? 0 : (b == 0xfff0f0f0u ? 1 : 2);
        if (class_a != 0) ++area;
        if (class_a != class_b) ++diff;
    }
    char message[96];
    std::snprintf(message, sizeof(message),
                  "note bodies and arrows match software (%d of %d differ)",
                  diff, area);
    check(area > 600, "note scene has visible notes");
    check(diff * 100 < area, message);
}

// The front face is tilted toward the pitched camera, so its upper half is
// nearer than its center. A glyph must still be visible over the whole face.
void testGlyphFullyVisibleOnTiltedFace() {
    const int width = 640;
    const int height = 360;
    const float side = 0.32f;
    const float depth = gameplayNoteDepth(side);
    const Vec3 centers[] = {
        Vec3(0.3f, 0.55f, 0.6f), Vec3(-0.5f, -0.5f, 1.2f), Vec3(0.0f, 0.0f, 3.0f)
    };
    for (int n = 0; n < 3; ++n) {
        const NoteCubeMesh cube = makeNoteCube(centers[n], side, width, height,
                                               depth);
        const float plane_z = centers[n].z - depth * 0.5f;
        const int x = cube.front_center.x;
        const int y = cube.front_center.y;
        const int reach = std::max(2, (cube.screen[3].y - cube.screen[0].y) / 3);
        // RSX path.
        DrawBatch batch = makeBatch(width, height);
        forEachRoundedNoteTriangle(centers[n], side, depth, 0xffe83048u, width,
                                   height, batch);
        batch.noteLine(x, y - reach, x, y + reach, 0xfff0f0f0u, 3, plane_z);
        Image gpu(width, height);
        replay(batch, gpu);
        DrawBatch alone = makeBatch(width, height);
        alone.line(x, y - reach, x, y + reach, 0xfff0f0f0u, 3);
        Image expected(width, height);
        replay(alone, expected);
        // Software path.
        Image cpu(width, height);
        std::vector<float> inverse(static_cast<std::size_t>(width) * height, 0.0f);
        const SoftwareNoteTarget target = {&cpu.pixels[0], &inverse[0], width,
                                           height};
        rasterizeRoundedNote(target, centers[n], side, depth, 0xffe83048u);
        int software_glyph = 0;
        for (int py = y - reach - 1; py <= y + reach + 1; ++py)
            for (int px = x - 1; px <= x + 1; ++px)
                if (py >= y - reach && py <= y + reach &&
                    paintNoteDepthPixel(target, px, py, plane_z, 0xfff0f0f0u))
                    ++software_glyph;
        int gpu_glyph = 0;
        int expected_glyph = 0;
        int upper_half = 0;
        for (std::size_t index = 0; index < gpu.pixels.size(); ++index) {
            if (expected.pixels[index] == 0xfff0f0f0u) ++expected_glyph;
            if (gpu.pixels[index] == 0xfff0f0f0u) {
                ++gpu_glyph;
                if (static_cast<int>(index / width) < y) ++upper_half;
            }
        }
        char message[128];
        std::snprintf(message, sizeof(message),
                      "glyph on note %d is whole: rsx %d of %d, upper half %d, "
                      "software %d of %d", n, gpu_glyph, expected_glyph,
                      upper_half, software_glyph, 3 * (2 * reach + 1));
        check(gpu_glyph == expected_glyph && upper_half >= reach * 3 - 3 &&
              software_glyph == 3 * (2 * reach + 1), message);
    }
}

void testGlyphHiddenBehindNearerCube() {
    const int width = 320;
    const int height = 180;
    DrawBatch batch = makeBatch(width, height);
    const Vec3 near_center(0.0f, 0.0f, 0.3f);
    const NoteCubeMesh cube = makeNoteCube(near_center, 0.32f, width, height,
                                           gameplayNoteDepth(0.32f));
    forEachRoundedNoteTriangle(near_center, 0.32f, gameplayNoteDepth(0.32f),
                               0xffe83048u, width, height, batch);
    // A far note's arrow projects onto the near cube: it must stay hidden.
    batch.noteCircle(cube.front_center.x, cube.front_center.y, 4,
                     0xff00ff00u, 3.0f - 0.16f);
    // The near note's own arrow must remain visible on its face.
    batch.noteCircle(cube.front_center.x + 12, cube.front_center.y, 2,
                     0xfff0f0f0u, near_center.z - 0.16f);
    Image gpu(width, height);
    replay(batch, gpu);
    int green = 0;
    for (std::size_t index = 0; index < gpu.pixels.size(); ++index)
        if (gpu.pixels[index] == 0xff00ff00u) ++green;
    check(green == 0, "far glyph does not show through a nearer cube");
    check(gpu.pixels[static_cast<std::size_t>(cube.front_center.y) * width +
                     cube.front_center.x + 12] == 0xfff0f0f0u,
          "near glyph is visible on its own face");
}

void testImageIsOneToOne() {
    const int w = 6;
    const int h = 4;
    std::uint32_t source[w * h];
    for (int index = 0; index < w * h; ++index)
        source[index] = 0xff000000u | static_cast<std::uint32_t>(index * 0x0a1b2cu);
    DrawBatch batch = makeBatch(32, 32);
    batch.image(7, 9, w, h, source, 42u);
    check(batch.textures().size() == 1 && batch.textures()[0].pixels == source &&
          batch.textures()[0].cache_key == 42u,
          "image keeps the caller's pixels and cache key without copying");
    Image gpu(32, 32);
    replay(batch, gpu);
    bool exact = true;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (gpu.pixels[static_cast<std::size_t>(9 + y) * 32 + 7 + x] !=
                source[y * w + x])
                exact = false;
    check(exact, "library card texels land 1:1");
    check(coloredPixels(gpu) == w * h, "image covers exactly its rectangle");
}

void testRunsAndCapacity() {
    DrawBatch batch;
    batch.reset(64, 64, 12, 1);
    batch.rectangle(0, 0, 4, 4, 0xffffffffu);       // 6 vertices, flat
    batch.rectangle(8, 0, 4, 4, 0xffffffffu);       // merges into same run
    check(batch.runs().size() == 1 && batch.runs()[0].vertex_count == 12,
          "same-state primitives share one run");
    batch.rectangle(16, 0, 4, 4, 0xffffffffu);      // no room
    check(batch.droppedPrimitives() == 2, "overflow drops and is counted");
    check(batch.vertices().size() == 12, "capacity is never exceeded");

    DrawBatch modes = makeBatch(64, 64);
    modes.rectangle(0, 0, 2, 2, 0xffffffffu);
    modes.noteCircle(10, 10, 2, 0xffffffffu, 2.0f);
    modes.rectangle(0, 0, 2, 2, 0xffffffffu);
    check(modes.runs().size() == 3, "state changes start new runs in order");
    check(modes.runs()[1].mode == BATCH_NOTE_GLYPH, "glyph run keeps its mode");

    DrawBatch invalid = makeBatch(64, 64);
    invalid.noteLine(0, 0, 10, 10, 0xffffffffu, 3, -5.0f);
    invalid.noteCircle(5, 5, 3, 0xffffffffu, NAN);
    check(invalid.vertices().empty(), "glyphs behind the camera are skipped");
}

std::uint32_t channel(std::uint32_t color, int shift) {
    return (color >> shift) & 255u;
}

void testBlendRunsAndBudget() {
    DrawBatch batch = makeBatch(64, 64);
    batch.rectangle(0, 0, 4, 4, 0xffffffffu);
    batch.glowLine(10.0f, 10.0f, 50.0f, 10.0f, 6.0f, 0x80ffffffu);
    check(batch.runs().size() == 2 && batch.runs()[1].blend == BLEND_ADD,
          "glow starts an additive run after opaque geometry");
    check(batch.runs()[1].vertex_count == 12,
          "glow line costs four triangles");
    const float x[4] = {0.0f, 8.0f, 8.0f, 0.0f};
    const float y[4] = {0.0f, 0.0f, 8.0f, 8.0f};
    const std::uint32_t colors[4] = {
        0x40ff0000u, 0x40ff0000u, 0x40ff0000u, 0x40ff0000u
    };
    batch.blendQuad(x, y, colors, BLEND_ALPHA);
    check(batch.runs().size() == 3 && batch.runs()[2].blend == BLEND_ALPHA &&
          batch.runs()[2].vertex_count == 6,
          "alpha quad is its own two-triangle run");
    batch.glowDisc(30.0f, 30.0f, 8.0f, 0xffffffffu, 12);
    check(batch.runs().back().vertex_count == 36,
          "glow disc costs one triangle per segment");
    batch.glowDisc(30.0f, 30.0f, 8.0f, 0xffffffffu, 500);
    check(batch.runs().back().vertex_count == 36 + 96,
          "glow disc segments are capped at 32");
    // A heavy effects frame: two full trails, four glowing walls with two
    // fills, eight cut flashes, floor and saber glow. It must stay a small
    // fraction of the vertex budget.
    DrawBatch frame = makeBatch(1280, 720);
    for (int quad = 0; quad < 2 * 11; ++quad) frame.blendQuad(x, y, colors, BLEND_ADD);
    for (int wall = 0; wall < 4; ++wall) {
        frame.blendQuad(x, y, colors, BLEND_ALPHA);
        frame.blendQuad(x, y, colors, BLEND_ALPHA);
        for (int edge = 0; edge < 4; ++edge)
            frame.glowLine(0.0f, 0.0f, 50.0f, 50.0f, 9.0f, 0x80ffffffu);
    }
    for (int burst = 0; burst < 8; ++burst)
        frame.glowDisc(100.0f, 100.0f, 30.0f, 0x80ffffffu, 20);
    frame.blendQuad(x, y, colors, BLEND_ALPHA);
    for (int line = 0; line < 5; ++line)
        frame.glowLine(0.0f, 0.0f, 50.0f, 50.0f, 20.0f, 0x80ffffffu);
    char message[96];
    std::snprintf(message, sizeof(message),
                  "heavy effects frame stays small (%u vertices)",
                  static_cast<unsigned int>(frame.vertices().size()));
    check(frame.vertices().size() < 1200, message);
}

void testGlowFallsOffAcrossTheLine() {
    DrawBatch batch = makeBatch(64, 64);
    batch.glowLine(8.0f, 32.0f, 56.0f, 32.0f, 10.0f, 0xc0ffffffu);
    Image image(64, 64);
    replay(batch, image);
    const std::uint32_t center = channel(image.pixels[31 * 64 + 32], 16);
    const std::uint32_t mid = channel(image.pixels[26 * 64 + 32], 16);
    const std::uint32_t mid_other = channel(image.pixels[36 * 64 + 32], 16);
    const std::uint32_t outside = channel(image.pixels[20 * 64 + 32], 16);
    char message[96];
    std::snprintf(message, sizeof(message),
                  "glow fades from center %u to %u/%u to %u",
                  center, mid, mid_other, outside);
    check(center > 150 && mid > 40 && mid < center && outside == 0, message);
    check(mid > mid_other ? mid - mid_other <= 30 : mid_other - mid <= 30,
          "glow is symmetric across the line");
}

void testBlendEquations() {
    // 50% blue over red mixes; additive red over red saturates.
    DrawBatch batch = makeBatch(16, 16);
    batch.rectangle(0, 0, 16, 16, 0xff800000u);
    const float x[4] = {0.0f, 8.0f, 8.0f, 0.0f};
    const float y[4] = {0.0f, 0.0f, 16.0f, 16.0f};
    const std::uint32_t blue[4] = {
        0x800000ffu, 0x800000ffu, 0x800000ffu, 0x800000ffu
    };
    batch.blendQuad(x, y, blue, BLEND_ALPHA);
    const float x2[4] = {8.0f, 16.0f, 16.0f, 8.0f};
    const std::uint32_t red[4] = {
        0xffff0000u, 0xffff0000u, 0xffff0000u, 0xffff0000u
    };
    batch.blendQuad(x2, y, red, BLEND_ADD);
    Image image(16, 16);
    replay(batch, image);
    const std::uint32_t mixed = image.pixels[8 * 16 + 4];
    const std::uint32_t added = image.pixels[8 * 16 + 12];
    check(channel(mixed, 16) > 50 && channel(mixed, 16) < 80 &&
          channel(mixed, 0) > 115 && channel(mixed, 0) < 140,
          "alpha blending mixes source and destination");
    check(channel(added, 16) == 255 && channel(added, 0) == 0,
          "additive blending saturates instead of wrapping");
}

void testGlowDiscFadesOutward() {
    DrawBatch batch = makeBatch(64, 64);
    batch.glowDisc(32.0f, 32.0f, 20.0f, 0xffffffffu, 20);
    Image image(64, 64);
    replay(batch, image);
    const std::uint32_t c0 = channel(image.pixels[32 * 64 + 32], 8);
    const std::uint32_t c1 = channel(image.pixels[32 * 64 + 40], 8);
    const std::uint32_t c2 = channel(image.pixels[32 * 64 + 48], 8);
    const std::uint32_t c3 = channel(image.pixels[32 * 64 + 60], 8);
    check(c0 > c1 && c1 > c2 && c3 == 0, "glow disc fades radially");
}

movebeat::SaberPose pose(float x, bool tracked) {
    return movebeat::SaberPose(Vec3(x, -0.5f, 0.0f), Vec3(x, 0.5f, 0.0f),
                               tracked);
}

void testSaberTrail() {
    SaberTrail trail;
    trail.add(pose(0.0f, true), 1000);
    check(trail.size() == 1, "first pose starts the trail");
    const TrailSample& first = trail.sample(0);
    check(std::fabs(first.base.y - (-0.5f + SaberTrail::baseFraction())) < 1e-5f &&
          first.tip.y == 0.5f, "trail spans the outer part of the blade");
    trail.add(pose(0.1f, false), 1016);
    check(trail.size() == 0, "losing tracking erases the trail");

    for (unsigned int step = 0; step < 20; ++step)
        trail.add(pose(step * 0.01f, true), 2000 + step);
    check(trail.size() == SaberTrail::kCapacity, "capacity bounds the trail");
    check(trail.sample(0).tip.x > trail.sample(1).tip.x,
          "newest sample comes first");

    check(SaberTrail::fade(trail.sample(0), trail.sample(0).time_ms) == 1.0f,
          "fresh sample is fully bright");
    TrailSample old_sample = trail.sample(0);
    check(SaberTrail::fade(old_sample, old_sample.time_ms +
                           SaberTrail::kLifetimeMs) == 0.0f,
          "sample fades out at the end of its lifetime");
    check(SaberTrail::fade(old_sample, old_sample.time_ms - 5u) == 1.0f,
          "a clock step backwards does not overflow the age");

    // A fast but human swing keeps the trail; a teleport restarts it.
    SaberTrail swing;
    swing.add(pose(0.0f, true), 0);
    swing.add(pose(0.2f, true), 16);   // 12.5 m/s
    check(swing.size() == 2, "a fast real swing keeps its trail");
    swing.add(pose(1.2f, true), 32);   // 62 m/s jump
    check(swing.size() == 1 && swing.sample(0).tip.x == 1.2f,
          "a tracking jump restarts the trail instead of streaking");
    swing.add(pose(1.25f, true), 32);  // same timestamp, small move
    check(swing.size() == 2, "duplicate timestamps do not break the trail");

    // The visible span is set by time, not by frame rate.
    SaberTrail at60;
    SaberTrail at30;
    for (unsigned int ms = 0; ms <= 600; ms += 16) at60.add(pose(0.0f, true), ms);
    for (unsigned int ms = 0; ms <= 600; ms += 33) at30.add(pose(0.0f, true), ms);
    const unsigned int span60 = at60.sample(0).time_ms -
                                at60.sample(at60.size() - 1u).time_ms;
    const unsigned int span30 = at30.sample(0).time_ms -
                                at30.sample(at30.size() - 1u).time_ms;
    char message[96];
    std::snprintf(message, sizeof(message),
                  "trail span is frame-rate independent (%u vs %u ms)",
                  span60, span30);
    check(span60 <= SaberTrail::kLifetimeMs &&
          span30 <= SaberTrail::kLifetimeMs &&
          span60 + 33u >= span30 && span30 + 33u >= span60, message);
}

void testNoteArrival() {
    const float spawn = 5.0f;
    const Vec3 upper(0.825f, 0.55f + spawn * -0.03f, spawn);
    const float middle = spawn * -0.03f;
    const NoteArrival start = noteArrival(upper, middle, spawn);
    check(std::fabs(start.position.x - upper.x * kArrivalLateralStart) < 1e-5f &&
          std::fabs(start.position.y - middle) < 1e-5f &&
          std::fabs(start.scale - kArrivalStartScale) < 1e-5f,
          "a note leaves the portal small, near its center");
    // Upper rows rise into place, lower rows drop into place.
    const Vec3 lower(-0.825f, -0.55f + 4.5f * -0.03f, 4.5f);
    const NoteArrival rising = noteArrival(Vec3(0.0f, 0.55f, 4.5f), 0.0f, spawn);
    const NoteArrival falling = noteArrival(lower, 4.5f * -0.03f, spawn);
    check(rising.position.y < 0.55f && rising.position.y > 0.0f,
          "upper-row notes rise into place");
    check(falling.position.y > lower.y && falling.position.y < 4.5f * -0.03f,
          "lower-row notes drop into place");
    float previous = 0.0f;
    bool monotonic = true;
    for (int step = 0; step <= 20; ++step) {
        const float z = spawn - step * 0.1f;
        const float y = noteArrival(Vec3(0.0f, 0.55f, z), 0.0f, spawn).position.y;
        if (y + 1e-6f < previous) monotonic = false;
        previous = y;
    }
    check(monotonic, "arrival never moves a note away from its cell");
    const NoteArrival settled = noteArrival(
        Vec3(0.3f, 0.4f, spawn * (1.0f - kArrivalFraction) - 0.01f), 0.0f, spawn);
    check(settled.position.x == 0.3f && settled.position.y == 0.4f &&
          settled.scale == 1.0f, "a note is settled after the arrival distance");
    // Whatever the map's jump distance, the strike zone is untouched.
    const float depths[] = {0.25f, 0.8f, 1.5f, 2.0f, 3.0f, 5.0f, 12.0f, 24.0f};
    bool strike_zone_exact = true;
    for (int d = 0; d < 8; ++d) {
        for (int step = 0; step <= 46; ++step) {
            const float z = -0.8f + step * 0.05f;  // up to 1.5 m
            if (z > depths[d]) break;
            const Vec3 position(0.825f, 0.55f, z);
            const NoteArrival visual = noteArrival(position, 0.0f, depths[d]);
            if (visual.position.x != position.x || visual.position.y != position.y ||
                visual.position.z != position.z || visual.scale != 1.0f)
                strike_zone_exact = false;
        }
    }
    check(strike_zone_exact,
          "notes within 1.5 m are always drawn at their gameplay position");
}

void testSaberModel() {
    const SaberModel untracked = saberModel(movebeat::SaberPose());
    check(!untracked.valid, "untracked saber has no model");
    const SaberModel stub = saberModel(movebeat::SaberPose(
        Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.1f, 0.0f), true));
    check(!stub.valid, "a blade shorter than the hilt is not drawn");
    const movebeat::SaberPose pose(Vec3(0.1f, -0.6f, -0.3f),
                                   Vec3(0.1f, 0.4f, -0.3f), true);
    const SaberModel model = saberModel(pose);
    check(model.valid, "tracked saber has a model");
    const float hilt = model.hilt_top.y - model.hilt_base.y;
    check(std::fabs(hilt - (kHiltBelowHandle + kHiltAboveHandle)) < 1e-5f &&
          model.hilt_base.y < pose.handle.y && model.hilt_top.y > pose.handle.y,
          "hilt spans the hand around the tracked handle");
    check(model.emitter.y > model.hilt_top.y && model.emitter.y < pose.tip.y &&
          model.blade_tip.y == pose.tip.y && model.emitter.x == 0.1f,
          "emitter sits on top of the hilt, under the blade");
}

void testStageLayout() {
    const float far_z = 5.0f;
    const float span = kTunnelRingCount * kTunnelRingSpacing;
    bool in_range = true;
    bool faded_ends = true;
    for (unsigned int ms = 0; ms < 20000; ms += 37) {
        for (unsigned int index = 0; index < kTunnelRingCount; ++index) {
            const TunnelRing ring = tunnelRing(index, ms, far_z);
            if (ring.z > far_z + kTunnelBeyondPortal + 1e-4f ||
                ring.z < far_z + kTunnelBeyondPortal - span - 1e-4f)
                in_range = false;
            if ((ring.z <= kTunnelVanishZ && ring.alpha > 0.0f) ||
                ring.alpha < 0.0f || ring.alpha > 1.0f)
                faded_ends = false;
        }
    }
    check(in_range, "tunnel frames stay within their loop");
    check(faded_ends, "tunnel frames fade out before reaching the player");
    const TunnelRing a = tunnelRing(0, 1000, far_z);
    const TunnelRing b = tunnelRing(0, 1100, far_z);
    check(a.z - b.z > 0.11f && a.z - b.z < 0.13f,
          "tunnel frames drift toward the player at 1.2 m/s");
    check(std::fabs(beatPulse(1000, 120.0f) - 1.0f) < 1e-5f &&
          std::fabs(beatPulse(1250, 120.0f) - 0.125f) < 1e-4f,
          "beat pulse peaks on the beat and decays");
    check(beatPulse(1000, 0.0f) == 0.0f && beatPulse(1000, NAN) == 0.0f,
          "beat pulse is off without a valid tempo");
}

} // namespace

// Also linked into the PPU test package (see game_tests.cpp), so the batch
// is checked with the Cell's big-endian floats as well as on the host.
int runDrawBatchTests() {
    testRectangleIsExact();
    testTriangleMatchesSoftware();
    testLinesMatchSoftware();
    testCirclesMatchSoftware();
    testDepthMapping();
    testNoteCubesMatchSoftware();
    testGlyphHiddenBehindNearerCube();
    testGlyphFullyVisibleOnTiltedFace();
    testImageIsOneToOne();
    testRunsAndCapacity();
    testBlendRunsAndBudget();
    testGlowFallsOffAcrossTheLine();
    testBlendEquations();
    testGlowDiscFadesOutward();
    testSaberTrail();
    testNoteArrival();
    testSaberModel();
    testStageLayout();
    if (failures) {
        std::printf("%d draw batch check(s) failed\n", failures);
        return failures;
    }
    std::printf("All draw batch tests passed.\n");
    return 0;
}

#if !defined(__PPU__)
int main() {
    return runDrawBatchTests() == 0 ? 0 : 1;
}
#endif
