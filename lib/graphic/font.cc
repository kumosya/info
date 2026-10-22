#include "kernel/graphic/font.h"
namespace graphic {
uint8_t glyphRow(uint8_t character, uint32_t row) {
    return row < consoleFontHeight ? consoleFont[character][row] : 0;
}
void drawGlyph(const Surface& surface, int32_t x, int32_t y, uint8_t character,
               uint32_t foreground, uint32_t background, bool underline) {
    fill(surface, x, y, consoleFontWidth, consoleFontHeight, background);
    for (uint32_t row = 0; row < consoleFontHeight; ++row) {
        const uint8_t bits =
            underline && row == consoleFontHeight - 2 ? 0xff : glyphRow(character, row);
        for (uint32_t column = 0; column < consoleFontWidth;) {
            if (!(bits & (0x80U >> column))) {
                ++column;
                continue;
            }
            const uint32_t start = column++;
            while (column < consoleFontWidth && (bits & (0x80U >> column))) ++column;
            fill(surface, x + int32_t(start), y + int32_t(row), column - start, 1,
                 foreground);
        }
    }
}
}  // namespace graphic
