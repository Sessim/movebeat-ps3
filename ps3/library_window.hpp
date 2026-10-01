#ifndef MOVEBEAT_PS3_LIBRARY_WINDOW_HPP
#define MOVEBEAT_PS3_LIBRARY_WINDOW_HPP

#include <cstddef>

namespace movebeat {
namespace ps3 {

// Steps between two entries of a circular list of `count` entries.
inline std::size_t libraryCardDistance(std::size_t index, std::size_t selected,
                                       std::size_t count) {
    if (count == 0 || index >= count || selected >= count) return 0;
    const std::size_t forward = index >= selected ? index - selected
                                                  : index + count - selected;
    const std::size_t backward = count - forward;
    return forward < backward ? forward : backward;
}

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
