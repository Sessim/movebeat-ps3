#ifndef MOVEBEAT_PS3_LIBRARY_WINDOW_HPP
#define MOVEBEAT_PS3_LIBRARY_WINDOW_HPP

#include <cstddef>

namespace movebeat {
namespace ps3 {

inline bool libraryCardVisible(std::size_t index, std::size_t selected,
                               std::size_t count) {
    if (count == 0 || index >= count || selected >= count) return false;
    if (index == selected) return true;
    if (count == 2) return true;
    return index == (selected + 1) % count ||
           index == (selected == 0 ? count - 1 : selected - 1);
}

} // namespace ps3
} // namespace movebeat

#endif
