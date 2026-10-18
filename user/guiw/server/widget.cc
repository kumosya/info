#include "widget.h"

#include "os/abi.h"

namespace window {

void Widget::init(const WidgetBounds& bounds) { bounds_ = bounds; }

bool Widget::handlePointer(int32_t, int32_t, bool) { return false; }

bool Widget::handleKey(os::KeyCode, uint32_t) { return false; }

bool Widget::contains(int32_t x, int32_t y) const {
    return x >= bounds_.x && y >= bounds_.y &&
           x < bounds_.x + static_cast<int32_t>(bounds_.width) &&
           y < bounds_.y + static_cast<int32_t>(bounds_.height);
}

void Label::init(const WidgetBounds& bounds, const char* text,
                       uint32_t color) {
    Widget::init(bounds);
    os::Runtime::copyString(text_, text, sizeof(text_));
    color_ = color;
}

void Label::render(WidgetCanvas& canvas) const {
    canvas.drawText(bounds_.x, bounds_.y, text_, color_);
}

void Button::init(const WidgetBounds& bounds, const char* text) {
    Widget::init(bounds);
    os::Runtime::copyString(text_, text, sizeof(text_));
    pressed_ = false;
}

void Button::render(WidgetCanvas& canvas) const {
    canvas.fillRectangle(bounds_.x, bounds_.y, bounds_.width, bounds_.height,
                         pressed_ ? 0x001f6f50U : 0x002b8a65U);
    canvas.fillRectangle(bounds_.x + 1, bounds_.y + 1, bounds_.width - 2, 1,
                         0x006fd0aaU);
    canvas.drawText(bounds_.x + 12, bounds_.y + 10, text_, 0x00ffffffU);
}

bool Button::handlePointer(int32_t x, int32_t y, bool pressed) {
    const bool next = pressed && contains(x, y);
    if (next == pressed_) {
        return false;
    }
    pressed_ = next;
    return true;
}

bool Button::pressed() const { return pressed_; }

void TextBox::init(const WidgetBounds& bounds, const char* text) {
    Widget::init(bounds);
    os::Runtime::copyString(text_, text, sizeof(text_));
    length_ = static_cast<uint32_t>(os::Runtime::stringLength(text_));
    focused_ = false;
}

void TextBox::render(WidgetCanvas& canvas) const {
    canvas.fillRectangle(bounds_.x, bounds_.y, bounds_.width, bounds_.height,
                         focused_ ? 0x00ffffffU : 0x00edf0f2U);
    const uint32_t border = focused_ ? 0x002b8a65U : 0x00949ba3U;
    canvas.fillRectangle(bounds_.x, bounds_.y, bounds_.width, 2, border);
    canvas.fillRectangle(bounds_.x, bounds_.y + bounds_.height - 2,
                         bounds_.width, 2, border);
    canvas.fillRectangle(bounds_.x, bounds_.y, 2, bounds_.height, border);
    canvas.fillRectangle(bounds_.x + bounds_.width - 2, bounds_.y, 2,
                         bounds_.height, border);
    canvas.drawText(bounds_.x + 8, bounds_.y + 9, text_, 0x0021262cU);
}

bool TextBox::handlePointer(int32_t x, int32_t y, bool pressed) {
    if (!pressed) {
        return false;
    }
    const bool next = contains(x, y);
    if (next == focused_) {
        return false;
    }
    focused_ = next;
    return true;
}

bool TextBox::handleKey(os::KeyCode keyCode, uint32_t modifiers) {
    if (!focused_) {
        return false;
    }
    if (keyCode == os::KeyCode::Backspace) {
        if (length_ != 0) {
            text_[--length_] = '\0';
            return true;
        }
        return false;
    }
    const char value = translate(keyCode, modifiers);
    if (value == '\0' || length_ + 1 >= sizeof(text_)) {
        return false;
    }
    text_[length_++] = value;
    text_[length_] = '\0';
    return true;
}

char TextBox::translate(os::KeyCode keyCode, uint32_t modifiers) const {
    if (keyCode == os::KeyCode::Space) {
        return ' ';
    }
    if (keyCode >= os::KeyCode::A && keyCode <= os::KeyCode::Z) {
        const bool shift = (modifiers & os::keyModifierShift) != 0;
        const bool caps = (modifiers & os::keyModifierCapsLock) != 0;
        const char base = shift != caps ? 'A' : 'a';
        return static_cast<char>(base + static_cast<uint32_t>(keyCode) -
                                            static_cast<uint32_t>(os::KeyCode::A));
    }
    if (keyCode >= os::KeyCode::Digit1 && keyCode <= os::KeyCode::Digit9) {
        return static_cast<char>('1' + static_cast<uint32_t>(keyCode) -
                                           static_cast<uint32_t>(os::KeyCode::Digit1));
    }
    return keyCode == os::KeyCode::Digit0 ? '0' : '\0';
}

}  // namespace window
