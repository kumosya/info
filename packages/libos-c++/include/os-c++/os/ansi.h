#ifndef _OS_ANSI_H
#define _OS_ANSI_H

#include <stdint.h>

namespace os::terminal {

// CSI state survives writes split across IPC/PTY reads.
class EscapeParser {
   public:
    enum class Result { Text, Pending, Command };
    Result feed(uint8_t value) {
        if (value == 0x1b) { state_ = 1; return Result::Pending; }
        if (state_ == 0) return Result::Text;
        if (state_ == 1) {
            state_ = value == '[' ? 2 : 0;
            count_ = 1;
            for (auto& parameter : parameters_) parameter = 0;
            return Result::Pending;
        }
        if (value >= '0' && value <= '9') {
            uint16_t& parameter = parameters_[count_ - 1];
            parameter = parameter > 999 ? 9999 : parameter * 10 + value - '0';
            return Result::Pending;
        }
        if (value == ';') {
            if (count_ < 8) ++count_;
            else state_ = 3;
            return Result::Pending;
        }
        if (value >= 0x40 && value <= 0x7e) {
            const bool valid = state_ == 2;
            state_ = 0;
            command_ = value;
            return valid ? Result::Command : Result::Pending;
        }
        state_ = 3;
        return Result::Pending;
    }
    char command() const { return command_; }
    uint32_t count() const { return count_; }
    uint16_t parameter(uint32_t index) const {
        return index < count_ ? parameters_[index] : 0;
    }

   private:
    uint16_t parameters_[8]{};
    uint8_t count_ = 0;
    uint8_t state_ = 0;
    char command_ = 0;
};

class Style {
   public:
    void apply(uint16_t value) {
        if (value == 0) { foreground_ = 7; background_ = 0; attributes_ = 0; }
        else if (value == 1) attributes_ |= 1;
        else if (value == 4) attributes_ |= 2;
        else if (value == 7) attributes_ |= 4;
        else if (value == 22) attributes_ &= ~1U;
        else if (value == 24) attributes_ &= ~2U;
        else if (value == 27) attributes_ &= ~4U;
        else if (value == 39) foreground_ = 7;
        else if (value == 49) background_ = 0;
        else if (value >= 30 && value <= 37) foreground_ = value - 30;
        else if (value >= 40 && value <= 47) background_ = value - 40;
        else if (value >= 90 && value <= 97) foreground_ = value - 90 + 8;
        else if (value >= 100 && value <= 107) background_ = value - 100 + 8;
    }
    uint8_t foreground() const { return foreground_; }
    uint8_t background() const { return background_; }
    uint8_t attributes() const { return attributes_; }

   private:
    uint8_t foreground_ = 7;
    uint8_t background_ = 0;
    uint8_t attributes_ = 0;
};

class Palette {
 public:
static uint32_t color(uint8_t index, bool bright = false) {
    constexpr uint32_t palette[] = {
        0x171a1f, 0xb84040, 0x4aa36b, 0xc49a44, 0x4a82bd, 0x9c5caf, 0x3aa6a0, 0xd9dde3,
        0x646b75, 0xef6666, 0x6ed596, 0xf0c674, 0x7fb7e8, 0xc792ea, 0x5fd7d1, 0xffffff};
    return palette[(index & 15U) | (bright ? 8U : 0U)];
}

};

}  // namespace os::terminal

#endif
