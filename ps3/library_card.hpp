#ifndef MOVEBEAT_PS3_LIBRARY_CARD_HPP
#define MOVEBEAT_PS3_LIBRARY_CARD_HPP

#include <string>
#include <vector>

#include "framebuffer.hpp"

namespace movebeat {
namespace ps3 {

class LibraryCard {
public:
    enum { WIDTH = 512, HEIGHT = 96 };

    bool load(const std::string& directory);
    void clear();
    bool ready() const { return pixels_.size() == WIDTH * HEIGHT; }
    void draw(Framebuffer& screen, int x, int y) const;

private:
    std::vector<unsigned short> pixels_;
};

} // namespace ps3
} // namespace movebeat

#endif
