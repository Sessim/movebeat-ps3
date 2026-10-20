#include "text_draw.hpp"

#include <stdio.h>
#include <string.h>

namespace movebeat {
namespace ps3 {

void drawMenuText(Framebuffer& screen, const char* label,
                  int x, int y, int scale, u32 color) {
    for (const char* cursor = label; *cursor;) {
        const MenuGlyph glyph = nextMenuGlyph(cursor);
        if (glyph.rows) {
            const u32 ink = glyph.icon_color ? glyph.icon_color : color;
            for (int row = 0; row < 7; ++row)
                for (int column = 0; column < glyph.columns; ++column)
                    if (glyph.rows[row] & (1u << (glyph.columns - 1 - column)))
                        screen.rectangle(x + column * scale, y + row * scale,
                                         scale, scale, ink);
        }
        x += glyph.advance * scale;
    }
}

void drawCenteredMenuText(Framebuffer& screen,
                          const char* label, int y, int scale, u32 color) {
    drawMenuText(screen, label, (screen.width() - menuTextWidth(label, scale)) / 2,
                 y, scale, color);
}

std::string menuLabel(const std::string& value) {
    std::string label;
    for (std::size_t index = 0; index < value.size() && label.size() < 18;
         ++index) {
        const char letter = value[index];
        if (letter >= 'a' && letter <= 'z')
            label.push_back(static_cast<char>(letter - 'a' + 'A'));
        else if ((letter >= 'A' && letter <= 'Z') || letter == ' ')
            label.push_back(letter);
    }
    return label.empty() ? "UNKNOWN" : label;
}

void drawTime(Framebuffer& screen, unsigned int milliseconds,
              int x, int y, int scale, u32 color) {
    unsigned int seconds = milliseconds / 1000u;
    unsigned int minutes = seconds / 60u;
    seconds %= 60u;
    if (minutes > 99u) minutes = 99u;
    const unsigned int digits[4] = {
        minutes / 10u, minutes % 10u, seconds / 10u, seconds % 10u
    };
    const int advance = scale * 4;
    for (int index = 0; index < 4; ++index) {
        const int position = index < 2 ? index : index + 1;
        drawDigit(screen, digits[index], x + position * advance, y, scale, color);
    }
    const int colon_x = x + 2 * advance;
    screen.rectangle(colon_x + scale, y + scale, scale, scale, color);
    screen.rectangle(colon_x + scale, y + scale * 3, scale, scale, color);
}

void drawDigit(Framebuffer& screen, unsigned int digit,
               int x, int y, int scale, u32 color) {
    static const unsigned char rows[10][5] = {
        { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 },
        { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
        { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 },
        { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 },
        { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 }
    };
    if (digit > 9) return;
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (rows[digit][row] & (1u << (2 - column))) {
                screen.rectangle(x + column * scale, y + row * scale,
                                 scale, scale, color);
            }
        }
    }
}

void drawNumber(Framebuffer& screen, unsigned int value,
                int center_x, int y, int scale, u32 color) {
    char text[16];
    snprintf(text, sizeof(text), "%u", value);
    int length = 0;
    while (text[length]) ++length;
    const int advance = scale * 4;
    int x = center_x - (length * advance - scale) / 2;
    for (int index = 0; index < length; ++index) {
        drawDigit(screen, static_cast<unsigned int>(text[index] - '0'),
                  x, y, scale, color);
        x += advance;
    }
}

void drawHexNumber(Framebuffer& screen, unsigned int value,
                   int center_x, int y, int scale, u32 color) {
    static const unsigned char rows[16][5] = {
        { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 },
        { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
        { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 },
        { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 },
        { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
        { 7, 5, 7, 5, 5 }, { 6, 5, 6, 5, 6 },
        { 7, 4, 4, 4, 7 }, { 6, 5, 5, 5, 6 },
        { 7, 4, 7, 4, 7 }, { 7, 4, 7, 4, 4 }
    };
    const int digits = 8;
    const int advance = scale * 4;
    int x = center_x - (digits * advance - scale) / 2;
    for (int index = digits - 1; index >= 0; --index) {
        const unsigned int digit = (value >> (index * 4)) & 0xfu;
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if (rows[digit][row] & (1u << (2 - column))) {
                    screen.rectangle(x + column * scale, y + row * scale,
                                     scale, scale, color);
                }
            }
        }
        x += advance;
    }
}

} // namespace ps3
} // namespace movebeat
