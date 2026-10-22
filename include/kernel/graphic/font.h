#pragma once
#include <cstdint>

#include "kernel/graphic/surface.h"
namespace graphic {
inline constexpr uint32_t consoleFontWidth = 8;
inline constexpr uint32_t consoleFontHeight = 16;
extern const uint8_t consoleFont[256][consoleFontHeight];
uint8_t glyphRow(uint8_t character, uint32_t row);
void drawGlyph(const Surface& surface, int32_t x, int32_t y, uint8_t character,
               uint32_t foreground, uint32_t background, bool underline = false);
}  // namespace graphic
