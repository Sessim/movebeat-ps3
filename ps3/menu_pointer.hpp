#ifndef MOVEBEAT_PS3_MENU_POINTER_HPP
#define MOVEBEAT_PS3_MENU_POINTER_HPP

#include "move_input.hpp"

#include <cstddef>
#include <cmath>

namespace movebeat {
namespace ps3 {

enum LibraryPointerTarget {
    LIBRARY_POINTER_NONE,
    LIBRARY_POINTER_PREVIOUS,
    LIBRARY_POINTER_CURRENT,
    LIBRARY_POINTER_NEXT
};

struct ScreenPointer {
    int x;
    int y;
    bool valid;

    ScreenPointer() : x(0), y(0), valid(false) {}
};

inline ScreenPointer moveImageScreenPoint(const MoveFrame::ImagePoint& image,
                                          int width, int height) {
    ScreenPointer result;
    if (!image.valid || width <= 0 || height <= 0 ||
        !std::isfinite(image.x) || !std::isfinite(image.y) ||
        image.x < 0.0f || image.x > 1.0f ||
        image.y < 0.0f || image.y > 1.0f) return result;
    result.x = static_cast<int>(image.x * width);
    result.y = static_cast<int>(image.y * height);
    if (result.x >= width) result.x = width - 1;
    if (result.y >= height) result.y = height - 1;
    result.valid = true;
    return result;
}

inline LibraryPointerTarget hitTestLibraryPointer(int x, int y,
                                                  int width, int height,
                                                  std::size_t level_count) {
    if (width <= 0 || height <= 0 || level_count == 0) return LIBRARY_POINTER_NONE;
    const int center_x = width / 2;
    const int card_x = center_x - 256;
    const int center_y = height / 2 - 55;
    const int offsets[] = {-1, 0, 1};
    for (int slot = 0; slot < 3; ++slot) {
        const int offset = offsets[slot];
        if ((level_count == 1 && offset != 0) ||
            (level_count == 2 && offset < 0)) continue;
        const int top = center_y + offset * 116 - 8;
        if (x >= card_x - 12 && x < card_x + 524 &&
            y >= top && y < top + 112) {
            if (offset < 0) return LIBRARY_POINTER_PREVIOUS;
            if (offset > 0) return LIBRARY_POINTER_NEXT;
            return LIBRARY_POINTER_CURRENT;
        }
    }
    return LIBRARY_POINTER_NONE;
}

} // namespace ps3
} // namespace movebeat

#endif
