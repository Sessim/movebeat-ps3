#ifndef MOVEBEAT_PS3_MENU_FONT_HPP
#define MOVEBEAT_PS3_MENU_FONT_HPP

#include <stdint.h>

#include <string>

namespace movebeat {
namespace ps3 {

// The language of the on-screen text: settings.ini "language=en|ru|auto"
// (auto follows the PS3 system language).
enum Language { LANGUAGE_EN, LANGUAGE_RU };

inline Language& languageSetting() {
    static Language value = LANGUAGE_EN;
    return value;
}
inline void setLanguage(Language language) { languageSetting() = language; }
inline Language language() { return languageSetting(); }
// The text in the current language; both are UTF-8.
inline const char* tr(const char* en, const char* ru) {
    return language() == LANGUAGE_RU ? ru : en;
}

// Controller buttons inside menu text, drawn as icons in the buttons' own
// colors. Keep each one a separate string literal ("\x03" " GO"), so the
// next letter is not read as part of the escape.
#define ICON_TRIANGLE "\x01"
#define ICON_CIRCLE "\x02"
#define ICON_CROSS "\x03"
#define ICON_SQUARE "\x04"
#define ICON_MOVE "\x05"
#define ICON_T "\x06"
#define ICON_START "\x07"

enum MenuIcon {
    MENU_ICON_TRIANGLE = 1, MENU_ICON_CIRCLE, MENU_ICON_CROSS, MENU_ICON_SQUARE,
    MENU_ICON_MOVE, MENU_ICON_T, MENU_ICON_START, MENU_ICON_COUNT
};

// One character of menu text: a 5x7 letter (bit 4 is the left column), a
// 7x7 icon (bit 6 is the left column) or a blank, and its advance in
// pixels of the font (6 for letters, 9 for icons).
struct MenuGlyph {
    const unsigned char* rows;
    int columns;
    int advance;
    uint32_t icon_color;
};

namespace menu_font_detail {

// Latin capitals, the HD44780-style face the menus have always used.
static const unsigned char kLatin[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17}, {31,4,4,4,4,4,31},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};
// Cyrillic capitals А..Я in the same face, then Ё.
static const unsigned char kCyrillic[33][7] = {
    {14,17,17,31,17,17,17}, {31,16,16,30,17,17,30}, {30,17,17,30,17,17,30},  // А Б В
    {31,16,16,16,16,16,16}, {14,10,10,10,10,31,17}, {31,16,16,30,16,16,31},  // Г Д Е
    {21,21,21,14,21,21,21}, {14,17,1,6,1,17,14}, {17,17,19,21,25,17,17},     // Ж З И
    {10,4,17,19,21,25,17}, {17,18,20,24,20,18,17}, {7,9,9,9,9,9,17},          // Й К Л
    {17,27,21,21,17,17,17}, {17,17,17,31,17,17,17}, {14,17,17,17,17,17,14},  // М Н О
    {31,17,17,17,17,17,17}, {30,17,17,30,16,16,16}, {14,17,16,16,16,17,14},  // П Р С
    {31,4,4,4,4,4,4}, {17,17,17,15,1,17,14}, {4,14,21,21,21,14,4},           // Т У Ф
    {17,17,10,4,10,17,17}, {18,18,18,18,18,31,1}, {17,17,17,15,1,1,1},       // Х Ц Ч
    {21,21,21,21,21,21,31}, {21,21,21,21,21,31,1}, {24,8,8,14,9,9,14},       // Ш Щ Ъ
    {17,17,17,29,19,19,29}, {16,16,16,30,17,17,30}, {14,17,1,7,1,17,14},     // Ы Ь Э
    {18,21,21,29,21,21,18}, {15,17,17,15,5,9,17},                            // Ю Я
    {10,0,31,16,30,16,31}                                                    // Ё
};
static const unsigned char kDigits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {31,2,4,2,1,17,14}, {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12}
};
static const char kPunctuation[] = "+-.,:/!?()'";
static const unsigned char kPunctuationRows[11][7] = {
    {0,4,4,31,4,4,0}, {0,0,0,31,0,0,0}, {0,0,0,0,0,12,12}, {0,0,0,0,12,4,8},
    {0,12,12,0,12,12,0}, {0,1,2,4,8,16,0}, {4,4,4,4,4,0,4}, {14,17,1,2,4,0,4},
    {2,4,8,8,8,4,2}, {8,4,2,2,2,4,8}, {4,4,8,0,0,0,0}
};
// Triangle, circle, cross, square, the Move button, the T trigger, Start.
static const unsigned char kIcons[MENU_ICON_COUNT - 1][7] = {
    {8,20,20,34,34,65,127}, {28,34,65,65,65,34,28}, {65,34,20,8,20,34,65},
    {127,65,65,65,65,65,127}, {28,62,127,127,127,62,28},
    {127,65,93,73,73,65,127}, {32,48,56,60,56,48,32}
};
static const uint32_t kIconColors[MENU_ICON_COUNT - 1] = {
    0xff40d8a0u, 0xffff5a6au, 0xff7aa8ffu, 0xffe884d0u, 0xffe8e4f4u,
    0xffc8d0e0u, 0xffc8d0e0u
};

} // namespace menu_font_detail

// Reads one character of UTF-8 menu text and advances `cursor` past it.
// Lowercase letters draw as capitals; anything without a glyph is a blank
// letter-wide gap.
inline MenuGlyph nextMenuGlyph(const char*& cursor) {
    using namespace menu_font_detail;
    MenuGlyph glyph;
    glyph.rows = 0;
    glyph.columns = 5;
    glyph.advance = 6;
    glyph.icon_color = 0;
    const unsigned char lead = static_cast<unsigned char>(*cursor++);
    unsigned int code = lead;
    if (lead >= 0xc0) {
        int extra = lead >= 0xf0 ? 3 : (lead >= 0xe0 ? 2 : 1);
        code = lead & (0x3fu >> extra);
        for (; extra > 0 && (static_cast<unsigned char>(*cursor) & 0xc0) == 0x80; --extra)
            code = (code << 6) | (static_cast<unsigned char>(*cursor++) & 0x3fu);
    }
    if (code >= MENU_ICON_TRIANGLE && code < MENU_ICON_COUNT) {
        glyph.rows = kIcons[code - 1];
        glyph.columns = 7;
        glyph.advance = 9;
        glyph.icon_color = kIconColors[code - 1];
    } else if (code >= 'a' && code <= 'z') {
        glyph.rows = kLatin[code - 'a'];
    } else if (code >= 'A' && code <= 'Z') {
        glyph.rows = kLatin[code - 'A'];
    } else if (code >= '0' && code <= '9') {
        glyph.rows = kDigits[code - '0'];
    } else if (code >= 0x410 && code <= 0x42f) {
        glyph.rows = kCyrillic[code - 0x410];
    } else if (code >= 0x430 && code <= 0x44f) {
        glyph.rows = kCyrillic[code - 0x430];
    } else if (code == 0x401 || code == 0x451) {
        glyph.rows = kCyrillic[32];
    } else if (code < 0x80) {
        for (int index = 0; kPunctuation[index]; ++index)
            if (kPunctuation[index] == static_cast<char>(code))
                glyph.rows = kPunctuationRows[index];
    }
    return glyph;
}

// Width in pixels of menu text at `scale`.
inline int menuTextWidth(const char* label, int scale) {
    int width = 0;
    for (const char* cursor = label; *cursor;) width += nextMenuGlyph(cursor).advance;
    return width * scale;
}

// A chart difficulty as the menus show it.
inline std::string difficultyLabel(const std::string& difficulty) {
    std::string folded;
    for (std::size_t index = 0; index < difficulty.size(); ++index) {
        const char letter = difficulty[index];
        if (letter >= 'A' && letter <= 'Z') folded.push_back(static_cast<char>(letter + 32));
        else if (letter >= 'a' && letter <= 'z') folded.push_back(letter);
    }
    if (folded == "easy") return tr("EASY", "ЛЕГКО");
    if (folded == "normal") return tr("NORMAL", "НОРМА");
    if (folded == "hard") return tr("HARD", "СЛОЖНО");
    if (folded == "expert") return tr("EXPERT", "ЭКСПЕРТ");
    if (folded == "expertplus") return tr("EXPERT+", "ЭКСПЕРТ+");
    std::string label;
    for (std::size_t index = 0; index < folded.size() && label.size() < 18; ++index)
        label.push_back(static_cast<char>(folded[index] - 32));
    return label.empty() ? std::string(tr("UNKNOWN", "НЕИЗВЕСТНО")) : label;
}

} // namespace ps3
} // namespace movebeat

#endif
