#pragma once
#include <cstdint>

namespace graphic {
class Surface {
   public:
    Surface() = default;
    Surface(uint8_t* pixels, uint32_t width, uint32_t height, uint32_t pitch,
            uint8_t bits, uint8_t red, uint8_t green, uint8_t blue)
        : pixels_(pixels),
          width_(width),
          height_(height),
          pitch_(pitch),
          bitsPerPixel_(bits),
          redPosition_(red),
          greenPosition_(green),
          bluePosition_(blue) {}
    bool valid() const;
    void fill(int32_t x, int32_t y, uint32_t width, uint32_t height,
              uint32_t color) const;
    void copy(int32_t x, int32_t y, const uint32_t* pixels, uint32_t width,
              uint32_t height, uint32_t stride) const;
    uint8_t* getPixels() const { return pixels_; }
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    uint32_t getPitch() const { return pitch_; }
    uint8_t getBitsPerPixel() const { return bitsPerPixel_; }
    uint8_t getRedPosition() const { return redPosition_; }
    uint8_t getGreenPosition() const { return greenPosition_; }
    uint8_t getBluePosition() const { return bluePosition_; }

   private:
    uint8_t* pixels_{};
    uint32_t width_{};
    uint32_t height_{};
    uint32_t pitch_{};
    uint8_t bitsPerPixel_{};
    uint8_t redPosition_{};
    uint8_t greenPosition_{};
    uint8_t bluePosition_{};
};
}  // namespace graphic
