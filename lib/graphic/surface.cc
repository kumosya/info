#include "kernel/graphic/surface.h"
namespace graphic {
bool valid(const Surface& s) {
    if (!s.pixels || !s.width || !s.height ||
        (s.bitsPerPixel != 24 && s.bitsPerPixel != 32) ||
        uint64_t(s.width) * (s.bitsPerPixel / 8) > s.pitch)
        return false;
    if (s.redPosition > s.bitsPerPixel - 8 || s.greenPosition > s.bitsPerPixel - 8 ||
        s.bluePosition > s.bitsPerPixel - 8)
        return false;
    const uint32_t r = 255U << s.redPosition, g = 255U << s.greenPosition,
                   b = 255U << s.bluePosition;
    return !(r & g) && !(r & b) && !(g & b);
}
namespace {
uint32_t pack(const Surface& s, uint32_t color) {
    return ((color >> 16) & 255U) << s.redPosition |
           ((color >> 8) & 255U) << s.greenPosition | (color & 255U) << s.bluePosition;
}
void store(uint8_t* p, uint32_t color, uint32_t bytes) {
    p[0] = color;
    p[1] = color >> 8;
    p[2] = color >> 16;
    if (bytes == 4) p[3] = color >> 24;
}
}  // namespace
void fill(const Surface& s, int32_t x, int32_t y, uint32_t width, uint32_t height,
          uint32_t color) {
    if (!valid(s)) return;
    const int64_t right = int64_t(x) + width, bottom = int64_t(y) + height;
    const int64_t left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    const int64_t endX = right < s.width ? right : s.width,
                  endY = bottom < s.height ? bottom : s.height;
    if (left >= endX || top >= endY) return;
    const uint32_t bytes = s.bitsPerPixel / 8, packed = pack(s, color);
    for (int64_t row = top; row < endY; ++row) {
        uint8_t* output = s.pixels + uint64_t(row) * s.pitch + uint64_t(left) * bytes;
        if (bytes == 4 && !(reinterpret_cast<uint64_t>(output) & 3)) {
            auto* words = reinterpret_cast<uint32_t*>(output);
            for (int64_t column = left; column < endX; ++column) *words++ = packed;
        } else {
            for (int64_t column = left; column < endX; ++column, output += bytes)
                store(output, packed, bytes);
        }
    }
}
void copy(const Surface& s, int32_t x, int32_t y, const uint32_t* pixels,
          uint32_t width, uint32_t height, uint32_t stride) {
    if (!valid(s) || !pixels || stride < width) return;
    const int64_t left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    const int64_t right = int64_t(x) + width, bottom = int64_t(y) + height;
    const int64_t endX = right < s.width ? right : s.width,
                  endY = bottom < s.height ? bottom : s.height;
    if (left >= endX || top >= endY) return;
    const uint32_t bytes = s.bitsPerPixel / 8;
    for (int64_t row = top; row < endY; ++row) {
        const uint32_t* input =
            pixels + uint64_t(row - y) * stride + uint64_t(left - x);
        uint8_t* output = s.pixels + uint64_t(row) * s.pitch + uint64_t(left) * bytes;
        if (bytes == 4 && s.redPosition == 16 && s.greenPosition == 8 &&
            s.bluePosition == 0 && !(reinterpret_cast<uint64_t>(output) & 3)) {
            auto* words = reinterpret_cast<uint32_t*>(output);
            for (int64_t column = left; column < endX; ++column)
                *words++ = *input++ & 0xffffffU;
        } else {
            for (int64_t column = left; column < endX; ++column, output += bytes)
                store(output, pack(s, *input++), bytes);
        }
    }
}
}  // namespace graphic
