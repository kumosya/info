#pragma once
#include <cstdint>
namespace graphic {
struct Surface {
    uint8_t* pixels;
    uint32_t width, height, pitch;
    uint8_t bitsPerPixel, redPosition, greenPosition, bluePosition;
};
bool valid(const Surface& surface);
void fill(const Surface& surface, int32_t x, int32_t y, uint32_t width, uint32_t height,
          uint32_t color);
void copy(const Surface& surface, int32_t x, int32_t y, const uint32_t* pixels,
          uint32_t width, uint32_t height, uint32_t stride);
}  // namespace graphic
