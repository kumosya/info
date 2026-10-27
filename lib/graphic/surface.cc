#include "kernel/graphic/surface.h"
namespace graphic {
bool Surface::valid() const {
    const auto& s = *this;
    if (!s.getPixels() || !s.getWidth() || !s.getHeight() ||
        (s.getBitsPerPixel() != 24 && s.getBitsPerPixel() != 32) ||
        uint64_t(s.getWidth()) * (s.getBitsPerPixel() / 8) > s.getPitch())
        return false;
    if (s.getRedPosition() > s.getBitsPerPixel() - 8 ||
        s.getGreenPosition() > s.getBitsPerPixel() - 8 ||
        s.getBluePosition() > s.getBitsPerPixel() - 8)
        return false;
    const uint32_t r = 255U << s.getRedPosition(),
                   g = 255U << s.getGreenPosition(),
                   b = 255U << s.getBluePosition();
    return !(r & g) && !(r & b) && !(g & b);
}
namespace {
uint32_t pack(const Surface& s, uint32_t color) {
    return ((color >> 16) & 255U) << s.getRedPosition() |
           ((color >> 8) & 255U) << s.getGreenPosition() |
           (color & 255U) << s.getBluePosition();
}
void store(uint8_t* p, uint32_t color, uint32_t bytes) {
    p[0] = color;
    p[1] = color >> 8;
    p[2] = color >> 16;
    if (bytes == 4) p[3] = color >> 24;
}
}  // namespace
void Surface::fill(int32_t x, int32_t y, uint32_t width, uint32_t height,
                   uint32_t color) const {
    const auto& s = *this;
    if (!s.valid()) return;
    const int64_t right = int64_t(x) + width, bottom = int64_t(y) + height;
    const int64_t left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    const int64_t endX = right < s.getWidth() ? right : s.getWidth(),
                  endY = bottom < s.getHeight() ? bottom : s.getHeight();
    if (left >= endX || top >= endY) return;
    const uint32_t bytes = s.getBitsPerPixel() / 8, packed = pack(s, color);
    for (int64_t row = top; row < endY; ++row) {
        uint8_t* output = s.getPixels() + uint64_t(row) * s.getPitch() +
                          uint64_t(left) * bytes;
        if (bytes == 4 && !(reinterpret_cast<uint64_t>(output) & 3)) {
            auto* words = reinterpret_cast<uint32_t*>(output);
            for (int64_t column = left; column < endX; ++column)
                *words++ = packed;
        } else {
            for (int64_t column = left; column < endX;
                 ++column, output += bytes)
                store(output, packed, bytes);
        }
    }
}
void Surface::copy(int32_t x, int32_t y, const uint32_t* pixels, uint32_t width,
                   uint32_t height, uint32_t stride) const {
    const auto& s = *this;
    if (!s.valid() || !pixels || stride < width) return;
    const int64_t left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    const int64_t right = int64_t(x) + width, bottom = int64_t(y) + height;
    const int64_t endX = right < s.getWidth() ? right : s.getWidth(),
                  endY = bottom < s.getHeight() ? bottom : s.getHeight();
    if (left >= endX || top >= endY) return;
    const uint32_t bytes = s.getBitsPerPixel() / 8;
    for (int64_t row = top; row < endY; ++row) {
        const uint32_t* input =
            pixels + uint64_t(row - y) * stride + uint64_t(left - x);
        uint8_t* output = s.getPixels() + uint64_t(row) * s.getPitch() +
                          uint64_t(left) * bytes;
        if (bytes == 4 && s.getRedPosition() == 16 &&
            s.getGreenPosition() == 8 && s.getBluePosition() == 0 &&
            !(reinterpret_cast<uint64_t>(output) & 3)) {
            auto* words = reinterpret_cast<uint32_t*>(output);
            for (int64_t column = left; column < endX; ++column)
                *words++ = *input++ & 0xffffffU;
        } else {
            for (int64_t column = left; column < endX;
                 ++column, output += bytes)
                store(output, pack(s, *input++), bytes);
        }
    }
}
}  // namespace graphic
