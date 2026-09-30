#include "library_card.hpp"

#include <stdio.h>
#include <string.h>

namespace movebeat {
namespace ps3 {

void LibraryCard::clear() {
    std::vector<unsigned short>().swap(pixels_);
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
    pixels_.resize(WIDTH * HEIGHT);
    for (std::size_t index = 0; index < pixels_.size(); ++index) {
        pixels_[index] = static_cast<unsigned short>(
            (static_cast<unsigned int>(bytes[index * 2]) << 8) | bytes[index * 2 + 1]);
    }
    return true;
}

void LibraryCard::draw(Framebuffer& screen, int x, int y) const {
    if (!ready()) return;
    screen.image(x, y, WIDTH, HEIGHT, &pixels_[0]);
}

} // namespace ps3
} // namespace movebeat
