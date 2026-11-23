/* 显存替身只保存像素，验证整帧完成之前不会提前修改设备画面。 */
#include "../../../guiw/server/display.h"
#include <cassert>

static uint32_t framebuffer[16 * 8];

os::Device::Frame::Frame() = default;
os::Device::Frame::~Frame() = default;
int os::Device::Frame::open(uint64_t) {
    address_ = reinterpret_cast<uint64_t>(framebuffer);
    width_ = 16;
    height_ = 8;
    pitch_ = 64;
    bitsPerPixel_ = 32;
    redPosition_ = 16;
    greenPosition_ = 8;
    bluePosition_ = 0;
    return 0;
}
uint64_t os::Device::Frame::getAddress() const { return address_; }
uint32_t os::Device::Frame::getWidth() const { return width_; }
uint32_t os::Device::Frame::getHeight() const { return height_; }
uint32_t os::Device::Frame::getPitch() const { return pitch_; }
uint8_t os::Device::Frame::getBitsPerPixel() const { return bitsPerPixel_; }
uint8_t os::Device::Frame::getRedPosition() const { return redPosition_; }
uint8_t os::Device::Frame::getGreenPosition() const { return greenPosition_; }
uint8_t os::Device::Frame::getBluePosition() const { return bluePosition_; }

guiw::server::Rectangle guiw::server::geometry::intersection(
    const Rectangle& left, const Rectangle& right) {
    return osgui::detail::intersect(left, right);
}

int main() {
    guiw::server::DisplayDriver display;
    assert(display.init());
    display.beginFrame();
    display.fillRectangle(0, 0, 16, 4, 0x123456);
    display.present({0, 0, 16, 4});
    display.fillRectangle(0, 4, 16, 4, 0xabcdef);
    display.present({0, 4, 16, 4});
    for (uint32_t pixel : framebuffer) {
        assert(pixel == 0);
    }
    display.endFrame();
    for (unsigned index = 0; index < 128; ++index) {
        assert(framebuffer[index] == (index < 64 ? 0x123456U : 0xabcdefU));
    }
    display.beginFrame();
    display.fillRectangle(0, 0, 16, 8, 0x111111);
    display.present({0, 0, 16, 8});
    display.cancelFrame();
    assert(framebuffer[0] == 0x123456 && framebuffer[127] == 0xabcdef);
}
