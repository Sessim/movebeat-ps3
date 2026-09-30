#pragma once

#include <string>

#include "framebuffer.hpp"

namespace movebeat {
namespace ps3 {

// Blocky uppercase Latin text; other characters advance without drawing.
void drawMenuText(Framebuffer& screen, const char* label,
                  int x, int y, int scale, u32 color);
void drawCenteredMenuText(Framebuffer& screen,
                          const char* label, int y, int scale, u32 color);
// Uppercase A-Z and spaces from value, at most 18 characters.
std::string menuLabel(const std::string& value);
void drawTime(Framebuffer& screen, unsigned int milliseconds,
              int x, int y, int scale, u32 color);
void drawDigit(Framebuffer& screen, unsigned int digit,
               int x, int y, int scale, u32 color);
void drawNumber(Framebuffer& screen, unsigned int value,
                int center_x, int y, int scale, u32 color);
void drawHexNumber(Framebuffer& screen, unsigned int value,
                   int center_x, int y, int scale, u32 color);

} // namespace ps3
} // namespace movebeat
