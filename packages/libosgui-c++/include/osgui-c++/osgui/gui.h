#ifndef _OSGUI_GUI_H
#define _OSGUI_GUI_H

#include <cstdint>

#include "os/input.h"

namespace os {

using WindowHandle = uint64_t;

enum class WindowLayer : uint32_t { Background, Normal, Overlay };

enum class WindowRequest : uint64_t {
    Create = 0x300,
    Destroy,
    Show,
    Hide,
    Resize,
    Move,
    DrawPixel,
    DrawLine,
    FillRectangle,
    DrawText,
    Present,
    GetEvent,
    GetDisplayInfo,
    SetSessionActive,
    SetLayer,
    Raise,
    SubscribeEvents
};

enum class WindowEventType : uint32_t {
    None,
    KeyDown,
    KeyUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    WindowResize,
    WindowClose,
    WindowFocus
};

struct WindowEvent {
    WindowEventType type;
    uint32_t modifiers;
    uint64_t timestamp;
    int32_t x;
    int32_t y;
    int32_t delta;
    KeyCode keyCode;
    uint32_t buttons;
    uint32_t reserved;
};

constexpr const char* guiwServiceName      = "guiw";
constexpr uint64_t windowEventNotification = 0x57494e4556454e54ULL;

class Window {
   public:
    static bool raiseWindow(uint64_t windowService, WindowHandle window);
    static bool setWindowLayer(uint64_t windowService, WindowHandle window,
                    WindowLayer layer);
    static bool setWindowSessionActive(uint64_t windowService, bool active);
    static bool getNextWindowEvent(uint64_t windowService, WindowHandle window,
                        WindowEvent& event);
    static bool presentWindow(uint64_t windowService, WindowHandle window);
    static bool drawText(uint64_t windowService, WindowHandle window, int32_t x,
              int32_t y, const char* text, uint32_t color);
    static bool fillRectangle(uint64_t windowService, WindowHandle window, int32_t x,
                   int32_t y, uint32_t width, uint32_t height, uint32_t color);
    static bool drawLine(uint64_t windowService, WindowHandle window, int32_t x0,
              int32_t y0, int32_t x1, int32_t y1, uint32_t color);
    static bool drawPixel(uint64_t windowService, WindowHandle window, int32_t x,
               int32_t y, uint32_t color);
    static bool moveWindow(uint64_t windowService, WindowHandle window, int32_t x,
                int32_t y);
    static bool resizeWindow(uint64_t windowService, WindowHandle window, uint32_t width,
                  uint32_t height);
    static bool hideWindow(uint64_t windowService, WindowHandle window);
    static bool showWindow(uint64_t windowService, WindowHandle window);
    static bool destroyWindow(uint64_t windowService, WindowHandle window);
    static WindowHandle createWindow(uint64_t windowService, uint32_t width,
                          uint32_t height, const char* title);
};

}  // namespace os

#endif
