#include "library_card.hpp"

#include <stdio.h>
#include <string.h>

namespace movebeat {
namespace ps3 {
namespace {

// Distinguishes reloads that reuse the same buffer address, so a cached
// texture of an older card is never shown for a newer one.
unsigned int g_next_card_id = 1;

} // namespace

void LibraryCard::clear() {
    std::vector<std::uint32_t>().swap(pixels_);
    id_ = 0;
}

bool LibraryCard::load(const std::string& directory) {
    clear();
    const std::string path = directory + "/library.mblc";
    FILE* file = 0;
#if defined(_MSC_VER)
    fopen_s(&file, path.c_str(), "rb");
#else
    file = fopen(path.c_str(), "rb");
#endif
    if (!file) return false;
    unsigned char header[8] = {};
    const bool valid_header = fread(header, 1, sizeof(header), file) == sizeof(header) &&
        memcmp(header, "MBLC", 4) == 0 &&
        header[4] == (WIDTH >> 8) && header[5] == (WIDTH & 255) &&
        header[6] == (HEIGHT >> 8) && header[7] == (HEIGHT & 255);
    if (!valid_header) {
        fclose(file);
        return false;
    }
    std::vector<unsigned char> bytes(WIDTH * HEIGHT * 2);
    const bool valid_payload = fread(&bytes[0], 1, bytes.size(), file) == bytes.size() &&
        fgetc(file) == EOF;
    fclose(file);
    if (!valid_payload) return false;
    // Convert the big-endian RGB565 payload once, at load, to the display's
    // 0xAARRGGBB so drawing never converts pixels again.
    pixels_.resize(WIDTH * HEIGHT);
    for (std::size_t index = 0; index < pixels_.size(); ++index) {
        const unsigned int color =
            (static_cast<unsigned int>(bytes[index * 2]) << 8) | bytes[index * 2 + 1];
        const std::uint32_t red = ((color >> 11) & 31u) * 255u / 31u;
        const std::uint32_t green = ((color >> 5) & 63u) * 255u / 63u;
        const std::uint32_t blue = (color & 31u) * 255u / 31u;
        pixels_[index] = 0xff000000u | (red << 16) | (green << 8) | blue;
    }
    id_ = g_next_card_id++;
    if (g_next_card_id == 0) g_next_card_id = 1;
    return true;
}

void LibraryCard::draw(Framebuffer& screen, int x, int y) const {
    if (!ready()) return;
    screen.image(x, y, WIDTH, HEIGHT, &pixels_[0], id_);
}

} // namespace ps3
} // namespace movebeat
