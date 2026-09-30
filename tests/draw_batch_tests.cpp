// Checks that the RSX draw batch covers the same pixels as the software
// renderer. A small reference rasterizer replays the batch the way the RSX
// samples it (pixel centers, depth modes) and the result is compared with the
// software primitives from framebuffer.cpp and software_note_renderer.hpp.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "draw_batch.hpp"
#include "note_cube.hpp"
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
    for (int n = 0; n < 5; ++n) {
        const NoteCubeMesh cube = makeNoteCube(centers[n], 0.32f, width, height,
                                               gameplayNoteDepth(0.32f));
        rasterizeNoteCube(target, cube, colors[n]);
        forEachNoteCubeTriangle(cube, colors[n], width, height, batch);
    }
    // Arrows far-to-near, like drawNotes: each is drawn at its face depth.
    for (int n = 4; n >= 0; --n) {
        const NoteCubeMesh cube = makeNoteCube(centers[n], 0.32f, width, height,
                                               gameplayNoteDepth(0.32f));
        const float face_depth = toNoteCameraSpace(Vec3(
            centers[n].x, centers[n].y, centers[n].z - 0.16f)).depth;
        const int x = cube.front_center.x;
        const int y = cube.front_center.y;
        batch.noteLine(x - 4, y, x + 4, y, 0xfff0f0f0u, 3, face_depth);
        const float inverse_depth = 1.0f / face_depth;
        for (int px = x - 5; px <= x + 5; ++px)
            for (int py = y - 1; py <= y + 1; ++py)
                paintNoteInverseDepthPixel(target, px, py, inverse_depth,
                                           0xfff0f0f0u);
    }
    Image gpu(width, height);
    replay(batch, gpu);
    const int diff = differingPixels(gpu, cpu);
    const int area = coloredPixels(cpu);
    char message[96];
    std::snprintf(message, sizeof(message),
                  "note cubes and arrows match software (%d of %d differ)",
                  diff, area);
    check(area > 1000, "note scene has visible cubes");
    check(diff * 100 < area, message);
}

void testGlyphHiddenBehindNearerCube() {
    const int width = 320;
    const int height = 180;
    DrawBatch batch = makeBatch(width, height);
    const Vec3 near_center(0.0f, 0.0f, 0.3f);
    const NoteCubeMesh cube = makeNoteCube(near_center, 0.32f, width, height,
                                           gameplayNoteDepth(0.32f));
    forEachNoteCubeTriangle(cube, 0xffe83048u, width, height, batch);
    // A far note's arrow projects onto the near cube: it must stay hidden.
    const float far_depth = toNoteCameraSpace(Vec3(0.0f, 0.0f, 3.0f)).depth;
    batch.noteCircle(cube.front_center.x, cube.front_center.y, 4,
                     0xff00ff00u, far_depth);
    // The near note's own arrow must remain visible on its face.
    const float near_depth = toNoteCameraSpace(Vec3(
        near_center.x, near_center.y, near_center.z - 0.16f)).depth;
    batch.noteCircle(cube.front_center.x + 12, cube.front_center.y, 2,
                     0xfff0f0f0u, near_depth);
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
    unsigned short source[w * h];
    for (int index = 0; index < w * h; ++index)
        source[index] = static_cast<unsigned short>(index * 2731u);
    DrawBatch batch = makeBatch(32, 32);
    batch.image(7, 9, w, h, source);
    Image gpu(32, 32);
    replay(batch, gpu);
    bool exact = true;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned short color = source[y * w + x];
            const std::uint32_t expected = 0xff000000u |
                ((((color >> 11) & 31u) * 255u / 31u) << 16) |
                ((((color >> 5) & 63u) * 255u / 63u) << 8) |
                ((color & 31u) * 255u / 31u);
            if (gpu.pixels[static_cast<std::size_t>(9 + y) * 32 + 7 + x] != expected)
                exact = false;
        }
    check(exact, "library card texels land 1:1 with the software conversion");
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
    invalid.noteLine(0, 0, 10, 10, 0xffffffffu, 3, 0.05f);
    invalid.noteCircle(5, 5, 3, 0xffffffffu, NAN);
    check(invalid.vertices().empty(), "glyphs behind the camera are skipped");
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
    testImageIsOneToOne();
    testRunsAndCapacity();
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
