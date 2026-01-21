#ifndef KERNEL_FONT_H
#define KERNEL_FONT_H

#include <cstdint>

struct FontData {
    struct {
        std::uint32_t offs;
        std::uint32_t len;
        std::uint32_t max;
        std::uint32_t pref;
        std::uint32_t width;
        std::uint32_t height;
    } hdr;
    std::uint8_t data[];
};

extern const FontData fontdata_8x16;

#endif  // KERNEL_FONT_H