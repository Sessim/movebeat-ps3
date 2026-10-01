#ifndef MOVEBEAT_PS3_LIBRARY_CARD_HPP
#define MOVEBEAT_PS3_LIBRARY_CARD_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "framebuffer.hpp"

namespace movebeat {
namespace ps3 {

// Pre-rendered 512x96 library entry (library.mblc), kept in display format.
class LibraryCard {
public:
    enum { WIDTH = 512, HEIGHT = 96 };
    // Bytes held in memory per loaded card.
    enum { BYTES = WIDTH * HEIGHT * 4 };

    LibraryCard() : id_(0) {}

    bool load(const std::string& directory);
    void clear();
    bool ready() const { return pixels_.size() == WIDTH * HEIGHT; }
    void draw(Framebuffer& screen, int x, int y) const;

private:
    std::vector<std::uint32_t> pixels_;
    // Unique per successful load; the RSX caches the card's texture by it.
    unsigned int id_;
};

} // namespace ps3
} // namespace movebeat

#endif
