#pragma once

#include <stdint.h>

#include "os/input.h"

namespace window {

struct WidgetBounds {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

class WidgetCanvas {
   public:
    virtual void fillRectangle(int32_t x, int32_t y, uint32_t width,
                               uint32_t height, uint32_t color) = 0;
    virtual void drawText(int32_t x, int32_t y, const char* text,
                          uint32_t color) = 0;
};

class Widget {
   public:
    void init(const WidgetBounds& bounds);
    virtual void render(WidgetCanvas& canvas) const = 0;
    virtual bool handlePointer(int32_t x, int32_t y, bool pressed);
    virtual bool handleKey(os::KeyCode keyCode, uint32_t modifiers);
    bool contains(int32_t x, int32_t y) const;

   protected:
    WidgetBounds bounds_{};
};

class Label final : public Widget {
   public:
    void init(const WidgetBounds& bounds, const char* text,
                    uint32_t color);
    void render(WidgetCanvas& canvas) const override;

   private:
    char text_[48]{};
    uint32_t color_ = 0;
};

class Button final : public Widget {
   public:
    void init(const WidgetBounds& bounds, const char* text);
    void render(WidgetCanvas& canvas) const override;
    bool handlePointer(int32_t x, int32_t y, bool pressed) override;
    bool pressed() const;

   private:
    char text_[32]{};
    bool pressed_ = false;
};

class TextBox final : public Widget {
   public:
    void init(const WidgetBounds& bounds, const char* text);
    void render(WidgetCanvas& canvas) const override;
    bool handlePointer(int32_t x, int32_t y, bool pressed) override;
    bool handleKey(os::KeyCode keyCode, uint32_t modifiers) override;

   private:
    char translate(os::KeyCode keyCode, uint32_t modifiers) const;

    char text_[32]{};
    uint32_t length_ = 0;
    bool focused_ = false;
};

}  // namespace window
