#include "os/abi.h"
#include "osgui/font.h"
#include "osgui/gui.h"
#include "os/input.h"
#include "os/service.h"
#include "os/sync.h"
#include "widget.h"

namespace {

constexpr uint32_t windowCapacity           = 8;
constexpr uint32_t eventCapacity            = 32;
constexpr uint32_t maximumSurfaceWidth      = 704;
constexpr uint32_t maximumSurfaceHeight     = 448;
constexpr uint32_t titleBarHeight           = 22;
constexpr uint64_t framebufferAddress       = 0x0000000050000000ULL;
constexpr uint64_t frameIntervalNanoseconds = 16666667ULL;
constexpr uint64_t compositorStackAddress   = 0x0000000062000000ULL;
constexpr uint64_t serviceThreadStackSize   = 64 * 1024;

uint32_t surfaceStorage[windowCapacity]
                       [maximumSurfaceWidth * maximumSurfaceHeight];

struct Rectangle {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

bool intersects(const Rectangle& left, const Rectangle& right) {
    return left.x < right.x + right.width && right.x < left.x + left.width &&
           left.y < right.y + right.height && right.y < left.y + left.height;
}

Rectangle intersection(const Rectangle& left, const Rectangle& right) {
    const int32_t x          = left.x > right.x ? left.x : right.x;
    const int32_t y          = left.y > right.y ? left.y : right.y;
    const int32_t rightEdge  = left.x + left.width < right.x + right.width
                                   ? left.x + left.width
                                   : right.x + right.width;
    const int32_t bottomEdge = left.y + left.height < right.y + right.height
                                   ? left.y + left.height
                                   : right.y + right.height;
    return {x, y, rightEdge > x ? rightEdge - x : 0,
            bottomEdge > y ? bottomEdge - y : 0};
}

int32_t absolute(int32_t value) { return value < 0 ? -value : value; }

class DisplayDriver {
   public:
    bool init() {
        return os::mapFramebuffer(framebufferAddress, info_) == 0 &&
               info_.address != 0 && info_.width != 0 && info_.height != 0 &&
               (info_.bitsPerPixel == 24 || info_.bitsPerPixel == 32);
    }

    const os::DisplayInfo& info() const { return info_; }

    void drawPixel(int32_t x, int32_t y, uint32_t color) {
        if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= info_.width ||
            static_cast<uint32_t>(y) >= info_.height) {
            return;
        }
        const uint8_t red   = static_cast<uint8_t>(color >> 16);
        const uint8_t green = static_cast<uint8_t>(color >> 8);
        const uint8_t blue  = static_cast<uint8_t>(color);
        uint32_t packed =
            (static_cast<uint32_t>(red) << info_.redPosition) |
            (static_cast<uint32_t>(green) << info_.greenPosition) |
            (static_cast<uint32_t>(blue) << info_.bluePosition);
        uint8_t* pixel = reinterpret_cast<uint8_t*>(info_.address) +
                         static_cast<uint64_t>(y) * info_.pitch +
                         static_cast<uint64_t>(x) * (info_.bitsPerPixel / 8);
        pixel[0] = static_cast<uint8_t>(packed);
        pixel[1] = static_cast<uint8_t>(packed >> 8);
        pixel[2] = static_cast<uint8_t>(packed >> 16);
        if (info_.bitsPerPixel == 32) {
            pixel[3] = static_cast<uint8_t>(packed >> 24);
        }
    }

    void fillRectangle(int32_t x, int32_t y, uint32_t width, uint32_t height,
                       uint32_t color) {
        for (uint32_t row = 0; row < height; ++row) {
            for (uint32_t column = 0; column < width; ++column) {
                drawPixel(x + static_cast<int32_t>(column),
                          y + static_cast<int32_t>(row), color);
            }
        }
    }

   private:
    os::DisplayInfo info_{};
};

class Surface : public window::WidgetCanvas {
   public:
    void init(uint32_t* pixels) { pixels_ = pixels; }

    bool resize(uint32_t width, uint32_t height) {
        if (pixels_ == nullptr || width == 0 || height == 0 ||
            width > maximumSurfaceWidth || height > maximumSurfaceHeight) {
            return false;
        }
        width_  = width;
        height_ = height;
        stride_ = maximumSurfaceWidth;
        for (uint32_t row = 0; row < height_; ++row) {
            for (uint32_t column = 0; column < width_; ++column) {
                pixels_[row * stride_ + column] = 0xfff4f4f4U;
            }
        }
        return true;
    }

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    void drawPixel(int32_t x, int32_t y, uint32_t color) {
        if (x >= 0 && y >= 0 && static_cast<uint32_t>(x) < width_ &&
            static_cast<uint32_t>(y) < height_) {
            pixels_[static_cast<uint32_t>(y) * stride_ +
                    static_cast<uint32_t>(x)] = color;
        }
    }

    uint32_t pixel(uint32_t x, uint32_t y) const {
        return pixels_[y * stride_ + x];
    }

    void fillRectangle(int32_t x, int32_t y, uint32_t width, uint32_t height,
                       uint32_t color) override {
        for (uint32_t row = 0; row < height; ++row) {
            for (uint32_t column = 0; column < width; ++column) {
                drawPixel(x + static_cast<int32_t>(column),
                          y + static_cast<int32_t>(row), color);
            }
        }
    }

    void drawText(int32_t x, int32_t y, const char* text,
                  uint32_t color) override {
        for (uint32_t character = 0; text != nullptr && text[character] != '\0';
             ++character) {
            const uint8_t glyph = static_cast<uint8_t>(text[character]);
            for (uint32_t row = 0; row < os::consoleFontHeight; ++row) {
                const uint8_t bits = os::consoleFont[glyph][row];
                for (uint32_t column = 0; column < os::consoleFontWidth;
                     ++column) {
                    if ((bits & (0x80U >> column)) != 0) {
                        drawPixel(
                            x + static_cast<int32_t>(
                                    character * os::consoleFontWidth + column),
                            y + static_cast<int32_t>(row), color);
                    }
                }
            }
        }
    }

    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                  uint32_t color) {
        const int32_t deltaX = absolute(x1 - x0);
        const int32_t stepX  = x0 < x1 ? 1 : -1;
        const int32_t deltaY = -absolute(y1 - y0);
        const int32_t stepY  = y0 < y1 ? 1 : -1;
        int32_t error        = deltaX + deltaY;
        while (true) {
            drawPixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int32_t doubled = 2 * error;
            if (doubled >= deltaY) {
                error += deltaY;
                x0 += stepX;
            }
            if (doubled <= deltaX) {
                error += deltaX;
                y0 += stepY;
            }
        }
    }

   private:
    uint32_t width_   = 0;
    uint32_t height_  = 0;
    uint32_t stride_  = 0;
    uint32_t* pixels_ = nullptr;
};

class EventQueue {
   public:
    void clear() { count_ = 0; }

    bool push(const os::WindowEvent& event) {
        if (count_ != 0 && event.type == os::WindowEventType::MouseMove &&
            events_[count_ - 1].type == os::WindowEventType::MouseMove) {
            events_[count_ - 1] = event;
            return true;
        }
        if (count_ == eventCapacity) {
            if (event.type == os::WindowEventType::MouseMove &&
                events_[count_ - 1].type == os::WindowEventType::MouseMove) {
                events_[count_ - 1] = event;
                return true;
            }
            return false;
        }
        events_[count_++] = event;
        return true;
    }

    bool pop(os::WindowEvent& event) {
        if (count_ == 0) {
            return false;
        }
        event = events_[0];
        for (uint32_t index = 1; index < count_; ++index) {
            events_[index - 1] = events_[index];
        }
        --count_;
        return true;
    }

   private:
    os::WindowEvent events_[eventCapacity]{};
    uint32_t count_ = 0;
};

struct Window {
    uint64_t handle;
    uint64_t ownerPid;
    int32_t x;
    int32_t y;
    bool active;
    bool visible;
    os::WindowLayer layer;
    uint64_t zOrder;
    char title[48];
    Surface surface;
    EventQueue events;
    uint64_t eventEndpoint;
    bool eventWaiting;
};

class GraphicsServer {
   public:
    bool init() { return display_.init(); }

    DisplayDriver& display() { return display_; }

    void compose(Window* windows, uint32_t count, int32_t cursorX,
                 int32_t cursorY, uint64_t focusedWindow,
                 const Rectangle& dirty) {
        const Rectangle screen  = {0, 0,
                                   static_cast<int32_t>(display_.info().width),
                                   static_cast<int32_t>(display_.info().height)};
        const Rectangle clipped = intersection(dirty, screen);
        if (clipped.width == 0 || clipped.height == 0) {
            return;
        }
        display_.fillRectangle(
            clipped.x, clipped.y, static_cast<uint32_t>(clipped.width),
            static_cast<uint32_t>(clipped.height), 0x00272a30U);
        uint64_t previousKey = 0;
        for (uint32_t drawn = 0; drawn < count; ++drawn) {
            Window* next     = nullptr;
            uint64_t nextKey = ~0ULL;
            for (uint32_t index = 0; index < count; ++index) {
                Window& candidate = windows[index];
                if (!candidate.active || !candidate.visible) {
                    continue;
                }
                const uint64_t key =
                    (static_cast<uint64_t>(candidate.layer) << 56) |
                    candidate.zOrder;
                if (key >= previousKey && key < nextKey) {
                    next    = &candidate;
                    nextKey = key;
                }
            }
            if (next == nullptr) {
                break;
            }
            drawWindow(*next, clipped, next->handle == focusedWindow);
            previousKey = nextKey + 1;
        }
        drawCursor(cursorX, cursorY, clipped);
    }

   private:
    static uint8_t glyphRow(char character, uint32_t row) {
        struct Glyph {
            char character;
            uint8_t rows[7];
        };
        constexpr Glyph glyphs[] = {{'O', {14, 17, 17, 17, 17, 17, 14}},
                                    {'S', {15, 16, 16, 14, 1, 1, 30}},
                                    {'I', {31, 4, 4, 4, 4, 4, 31}},
                                    {'N', {17, 25, 21, 19, 17, 17, 17}},
                                    {'F', {31, 16, 16, 30, 16, 16, 16}},
                                    {'W', {17, 17, 17, 21, 21, 21, 10}},
                                    {'D', {30, 17, 17, 17, 17, 17, 30}},
                                    {'E', {31, 16, 16, 30, 16, 16, 31}},
                                    {'M', {17, 27, 21, 21, 17, 17, 17}},
                                    {' ', {0, 0, 0, 0, 0, 0, 0}}};
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
        for (const Glyph& glyph : glyphs) {
            if (glyph.character == character) {
                return glyph.rows[row];
            }
        }
        return row == 0 || row == 6 ? 31 : 17;
    }

    void drawWindow(const Window& window, const Rectangle& dirty,
                    bool focused) {
        const Rectangle frame = {
            window.x - 2, window.y - static_cast<int32_t>(titleBarHeight) - 2,
            static_cast<int32_t>(window.surface.width()) + 4,
            static_cast<int32_t>(window.surface.height() + titleBarHeight) + 4};
        if (!intersects(frame, dirty)) {
            return;
        }
        fillClipped(frame, dirty, focused ? 0x003aa675U : 0x006a7079U);
        const Rectangle title = {
            window.x, window.y - static_cast<int32_t>(titleBarHeight),
            static_cast<int32_t>(window.surface.width()),
            static_cast<int32_t>(titleBarHeight)};
        fillClipped(title, dirty, focused ? 0x001c704dU : 0x00464b52U);
        drawTitle(window.title, window.x + 7,
                  window.y - static_cast<int32_t>(titleBarHeight) + 7,
                  0x00ffffffU, dirty);
        const Rectangle content = {
            window.x, window.y, static_cast<int32_t>(window.surface.width()),
            static_cast<int32_t>(window.surface.height())};
        const Rectangle pixels = intersection(content, dirty);
        for (int32_t y = pixels.y; y < pixels.y + pixels.height; ++y) {
            for (int32_t x = pixels.x; x < pixels.x + pixels.width; ++x) {
                display_.drawPixel(
                    x, y,
                    window.surface.pixel(static_cast<uint32_t>(x - window.x),
                                         static_cast<uint32_t>(y - window.y)));
            }
        }
    }

    void fillClipped(const Rectangle& rectangle, const Rectangle& dirty,
                     uint32_t color) {
        const Rectangle clipped = intersection(rectangle, dirty);
        if (clipped.width > 0 && clipped.height > 0) {
            display_.fillRectangle(
                clipped.x, clipped.y, static_cast<uint32_t>(clipped.width),
                static_cast<uint32_t>(clipped.height), color);
        }
    }

    void drawTitle(const char* title, int32_t x, int32_t y, uint32_t color,
                   const Rectangle& dirty) {
        for (uint32_t character = 0; title[character] != '\0' && character < 20;
             ++character) {
            for (uint32_t row = 0; row < 7; ++row) {
                const uint8_t bits = glyphRow(title[character], row);
                for (uint32_t column = 0; column < 5; ++column) {
                    if ((bits & (1U << (4 - column))) != 0) {
                        const int32_t pixelX =
                            x + static_cast<int32_t>(character * 6 + column);
                        const int32_t pixelY = y + static_cast<int32_t>(row);
                        if (pixelX >= dirty.x &&
                            pixelX < dirty.x + dirty.width &&
                            pixelY >= dirty.y &&
                            pixelY < dirty.y + dirty.height) {
                            display_.drawPixel(pixelX, pixelY, color);
                        }
                    }
                }
            }
        }
    }

    void drawCursor(int32_t x, int32_t y, const Rectangle& dirty) {
        for (int32_t offset = -6; offset <= 6; ++offset) {
            if (x + offset >= dirty.x && x + offset < dirty.x + dirty.width &&
                y >= dirty.y && y < dirty.y + dirty.height) {
                display_.drawPixel(x + offset, y, 0x00ffffffU);
            }
            if (x >= dirty.x && x < dirty.x + dirty.width &&
                y + offset >= dirty.y && y + offset < dirty.y + dirty.height) {
                display_.drawPixel(x, y + offset, 0x00ffffffU);
            }
        }
        if (x >= dirty.x && x < dirty.x + dirty.width && y >= dirty.y &&
            y < dirty.y + dirty.height) {
            display_.drawPixel(x, y, 0x00000000U);
        }
    }

    DisplayDriver display_;
};

class WindowServer {
   public:
    bool init() {
        if (!graphics_.init()) {
            return false;
        }
        for (uint32_t index = 0; index < windowCapacity; ++index) {
            windows_[index].surface.init(surfaceStorage[index]);
        }
        cursorX_ = static_cast<int32_t>(graphics_.display().info().width / 2);
        cursorY_ = static_cast<int32_t>(graphics_.display().info().height / 2);
        initializeDesktopLocked();
        return true;
    }

    void handle(const os::IpcMessage& request, os::IpcMessage& reply) {
        const auto operation = static_cast<os::WindowRequest>(request.type);
        if (operation == os::WindowRequest::GetEvent) {
            getEvent(request.senderPid, request.data[0], reply);
            return;
        }
        if (operation == os::WindowRequest::SubscribeEvents) {
            subscribeEvents(request, reply);
            return;
        }
        os::SpinLockGuard guard(lock_);
        Window* window = findOwned(request.senderPid, request.data[0]);
        reply.data[0]  = 0;
        switch (operation) {
            case os::WindowRequest::Create:
                create(request, reply);
                break;
            case os::WindowRequest::Destroy:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    markWindowDirtyLocked(*window);
                    if (window->eventEndpoint != 0) {
                        os::closeHandle(window->eventEndpoint);
                        window->eventEndpoint = 0;
                    }
                    window->eventWaiting = false;
                    window->active       = false;
                    window->visible      = false;
                    if (focusedWindow_ == window->handle) {
                        focusedWindow_ = 0;
                    }
                }
                break;
            case os::WindowRequest::Show:
            case os::WindowRequest::Hide:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    markWindowDirtyLocked(*window);
                    window->visible = operation == os::WindowRequest::Show;
                    if (window->visible) {
                        setFocusLocked(*window);
                    }
                    markWindowDirtyLocked(*window);
                }
                break;
            case os::WindowRequest::Resize:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    markWindowDirtyLocked(*window);
                    if (!window->surface.resize(
                            static_cast<uint32_t>(request.data[1]),
                            static_cast<uint32_t>(request.data[2]))) {
                        reply.data[0] = 1;
                    } else {
                        os::WindowEvent event{};
                        event.type      = os::WindowEventType::WindowResize;
                        event.timestamp = os::getMonotonicTime();
                        event.x         = static_cast<int32_t>(request.data[1]);
                        event.y         = static_cast<int32_t>(request.data[2]);
                        window->events.push(event);
                        notifyEventWaiterLocked(*window);
                        clampWindowLocked(*window);
                        markWindowDirtyLocked(*window);
                    }
                }
                break;
            case os::WindowRequest::Move:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    markWindowDirtyLocked(*window);
                    window->x = static_cast<int32_t>(request.data[1]);
                    window->y = static_cast<int32_t>(request.data[2]);
                    clampWindowLocked(*window);
                    markWindowDirtyLocked(*window);
                }
                break;
            case os::WindowRequest::DrawPixel:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    window->surface.drawPixel(
                        static_cast<int32_t>(request.data[1]),
                        static_cast<int32_t>(request.data[2]), request.data[3]);
                }
                break;
            case os::WindowRequest::DrawLine:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    window->surface.drawLine(
                        static_cast<int32_t>(request.data[1]),
                        static_cast<int32_t>(request.data[2]),
                        static_cast<int32_t>(request.data[3]),
                        static_cast<int32_t>(request.data[4]), request.data[5]);
                }
                break;
            case os::WindowRequest::FillRectangle:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    window->surface.fillRectangle(
                        static_cast<int32_t>(request.data[1]),
                        static_cast<int32_t>(request.data[2]),
                        static_cast<uint32_t>(request.data[3]),
                        static_cast<uint32_t>(request.data[4]),
                        request.data[5]);
                }
                break;
            case os::WindowRequest::DrawText:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    window->surface.drawText(
                        static_cast<int32_t>(request.data[1]),
                        static_cast<int32_t>(request.data[2]),
                        reinterpret_cast<const char*>(&request.data[4]),
                        static_cast<uint32_t>(request.data[3]));
                }
                break;
            case os::WindowRequest::Present:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    markWindowDirtyLocked(*window);
                }
                break;
            case os::WindowRequest::GetDisplayInfo:
                reply.data[1] = graphics_.display().info().width;
                reply.data[2] = graphics_.display().info().height;
                reply.data[3] = graphics_.display().info().bitsPerPixel;
                reply.data[4] =
                    static_cast<uint64_t>(graphics_.display().info().format);
                reply.length = 5;
                break;
            case os::WindowRequest::SetSessionActive:
                if (!sessionActive_ && request.data[0] != 0) {
                    sessionActive_ = true;
                    markScreenDirtyLocked();
                    nextFrameTime_ = 0;
                } else if (request.data[0] == 0) {
                    sessionActive_ = false;
                }
                break;
            case os::WindowRequest::SetLayer:
                if (window == nullptr ||
                    request.data[1] >
                        static_cast<uint64_t>(os::WindowLayer::Overlay)) {
                    reply.data[0] = 1;
                } else {
                    markWindowDirtyLocked(*window);
                    window->layer =
                        static_cast<os::WindowLayer>(request.data[1]);
                    window->zOrder = nextZOrder_++;
                    markWindowDirtyLocked(*window);
                }
                break;
            case os::WindowRequest::Raise:
                if (window == nullptr) {
                    reply.data[0] = 1;
                } else {
                    setFocusLocked(*window);
                    markWindowDirtyLocked(*window);
                }
                break;
            default:
                reply.data[0] = 1;
                break;
        }
        if (reply.length == 0) {
            reply.length = 1;
        }
    }

    void handleInput(const os::InputEvent& input) {
        {
            os::SpinLockGuard guard(lock_);
            if (!sessionActive_) {
                return;
            }
            Window* target = find(focusedWindow_);
            if (input.type == os::InputEventType::MouseMove) {
                markCursorDirtyLocked(cursorX_, cursorY_);
                cursorX_ += input.mouse.deltaX;
                cursorY_ += input.mouse.deltaY;
                clampCursorLocked();
                markCursorDirtyLocked(cursorX_, cursorY_);
                target = hitTestLocked(cursorX_, cursorY_);
            } else if (input.type == os::InputEventType::MouseButtonDown) {
                Window* hit = hitTestLocked(cursorX_, cursorY_);
                if (hit != nullptr) {
                    setFocusLocked(*hit);
                    target = hit;
                }
            }
            if (target != nullptr && target->ownerPid == 0) {
                bool changed = false;
                if (target->handle == welcomeWindowHandle_) {
                    const int32_t x = cursorX_ - target->x;
                    const int32_t y = cursorY_ - target->y;
                    if (input.type == os::InputEventType::MouseButtonDown) {
                        changed = actionButton_.handlePointer(x, y, true) ||
                                  inputBox_.handlePointer(x, y, true);
                    } else if (input.type ==
                               os::InputEventType::MouseButtonUp) {
                        changed = actionButton_.handlePointer(x, y, false);
                    } else if (input.type == os::InputEventType::KeyDown) {
                        changed = inputBox_.handleKey(input.key.keyCode,
                                                      input.key.modifiers);
                    }
                    if (changed) {
                        renderDesktopWindowLocked(*target);
                        markWindowDirtyLocked(*target);
                    }
                }
            }
            if (target != nullptr && target->ownerPid != 0) {
                os::WindowEvent event{};
                event.timestamp = input.timestamp;
                event.x         = cursorX_ - target->x;
                event.y         = cursorY_ - target->y;
                event.buttons   = input.mouse.buttons;
                event.delta     = input.mouse.wheel;
                event.keyCode   = input.key.keyCode;
                event.modifiers = input.key.modifiers;
                event.type      = translateEventType(input.type);
                target->events.push(event);
                notifyEventWaiterLocked(*target);
            }
        }
    }

    void refresh() {
        os::SpinLockGuard guard(lock_);
        if (!sessionActive_ || !dirtyValid_) {
            return;
        }
        const uint64_t now = os::getMonotonicTime();
        if (now < nextFrameTime_) {
            return;
        }
        composeDirtyLocked();
        nextFrameTime_ = now + frameIntervalNanoseconds;
    }

   private:
    void initializeDesktopLocked() {
        initializeSystemWindow(windows_[0], 80, 105, 360, 220,
                               "Welcome to osinfo");
        welcomeWindowHandle_ = windows_[0].handle;
        welcomeLabel_.init({20, 20, 280, 20}, "OSINFO DESKTOP",
                                 0x0021262cU);
        inputBox_.init({20, 70, 300, 36}, "Type here");
        actionButton_.init({20, 132, 132, 38}, "OPEN");

        initializeSystemWindow(windows_[1], 485, 175, 260, 160,
                               "System status");
        systemWindowHandle_ = windows_[1].handle;
        systemLabel_.init({18, 22, 220, 20}, "SYSTEM ONLINE",
                                0x0021262cU);
        detailLabel_.init({18, 58, 220, 20}, "4 CPU  64-BIT",
                                0x00545c66U);
        renderDesktopWindowLocked(windows_[0]);
        renderDesktopWindowLocked(windows_[1]);
        focusedWindow_ = welcomeWindowHandle_;
    }

    void initializeSystemWindow(Window& window, int32_t x, int32_t y,
                                uint32_t width, uint32_t height,
                                const char* title) {
        window.surface.resize(width, height);
        window.active        = true;
        window.visible       = true;
        window.layer         = os::WindowLayer::Normal;
        window.zOrder        = nextZOrder_++;
        window.handle        = nextHandle_++;
        window.ownerPid      = 0;
        window.eventEndpoint = 0;
        window.eventWaiting  = false;
        window.x             = x;
        window.y             = y;
        window.events.clear();
        os::copyString(window.title, title, sizeof(window.title));
        clampWindowLocked(window);
    }

    void renderDesktopWindowLocked(Window& window) {
        window.surface.fillRectangle(0, 0, window.surface.width(),
                                     window.surface.height(), 0x00f4f6f7U);
        if (window.handle == welcomeWindowHandle_) {
            welcomeLabel_.render(window.surface);
            inputBox_.render(window.surface);
            actionButton_.render(window.surface);
        } else if (window.handle == systemWindowHandle_) {
            systemLabel_.render(window.surface);
            detailLabel_.render(window.surface);
            window.surface.fillRectangle(18, 98, 190, 8, 0x00d9dfe3U);
            window.surface.fillRectangle(18, 98, 138, 8, 0x002b8a65U);
        }
    }

    void create(const os::IpcMessage& request, os::IpcMessage& reply) {
        const uint32_t width  = static_cast<uint32_t>(request.data[0]);
        const uint32_t height = static_cast<uint32_t>(request.data[1]);
        for (uint32_t index = 0; index < windowCapacity; ++index) {
            Window& window = windows_[index];
            if (!window.active && window.surface.resize(width, height)) {
                window.active        = true;
                window.visible       = false;
                window.layer         = os::WindowLayer::Normal;
                window.zOrder        = nextZOrder_++;
                window.handle        = nextHandle_++;
                window.ownerPid      = request.senderPid;
                window.eventEndpoint = 0;
                window.eventWaiting  = false;
                window.x             = 40 + static_cast<int32_t>(index * 28);
                window.y             = 70 + static_cast<int32_t>(index * 24);
                window.events.clear();
                os::copyString(window.title,
                               reinterpret_cast<const char*>(&request.data[2]),
                               sizeof(window.title));
                reply.data[1] = window.handle;
                reply.length  = 2;
                return;
            }
        }
        reply.data[0] = 1;
    }

    void getEvent(uint64_t ownerPid, uint64_t handle, os::IpcMessage& reply) {
        os::WindowEvent event{};
        os::SpinLockGuard guard(lock_);
        Window* window = findOwned(ownerPid, handle);
        if (window == nullptr || window->eventEndpoint == 0) {
            reply.data[0] = 1;
            reply.length  = 1;
            return;
        }
        if (!window->events.pop(event)) {
            window->eventWaiting = true;
            reply.data[0]        = static_cast<uint64_t>(
                static_cast<int64_t>(os::ErrorCode::WouldBlock));
            reply.length = 1;
            return;
        }
        reply.data[0]         = 0;
        const uint8_t* source = reinterpret_cast<const uint8_t*>(&event);
        for (uint32_t index = 0; index < sizeof(event); ++index) {
            reinterpret_cast<uint8_t*>(&reply.data[1])[index] = source[index];
        }
        reply.length = 1 + sizeof(event) / sizeof(uint64_t);
    }

    void subscribeEvents(const os::IpcMessage& request, os::IpcMessage& reply) {
        os::SpinLockGuard guard(lock_);
        Window* window = findOwned(request.senderPid, request.data[0]);
        if (window == nullptr || request.handleCount != 1) {
            for (uint32_t index = 0; index < request.handleCount; ++index) {
                os::closeHandle(request.handles[index]);
            }
            reply.data[0] = 1;
            reply.length  = 1;
            return;
        }
        if (window->eventEndpoint != 0) {
            os::closeHandle(window->eventEndpoint);
        }
        window->eventEndpoint = request.handles[0];
        window->eventWaiting  = false;
        reply.data[0]         = 0;
        reply.length          = 1;
    }

    void notifyEventWaiterLocked(Window& window) {
        if (!window.eventWaiting || window.eventEndpoint == 0) return;
        window.eventWaiting = false;
        os::IpcMessage notification{};
        notification.type = os::windowEventNotification;
        os::sendMessage(window.eventEndpoint, notification);
    }

    Window* find(uint64_t handle) {
        for (Window& window : windows_) {
            if (window.active && window.handle == handle) {
                return &window;
            }
        }
        return nullptr;
    }

    Window* findOwned(uint64_t ownerPid, uint64_t handle) {
        Window* window = find(handle);
        return window != nullptr && window->ownerPid == ownerPid ? window
                                                                 : nullptr;
    }

    Window* hitTestLocked(int32_t x, int32_t y) {
        Window* result     = nullptr;
        uint64_t resultKey = 0;
        for (uint32_t index = 0; index < windowCapacity; ++index) {
            Window& window = windows_[index];
            if (window.active && window.visible && x >= window.x &&
                y >= window.y - static_cast<int32_t>(titleBarHeight) &&
                x < window.x + static_cast<int32_t>(window.surface.width()) &&
                y < window.y + static_cast<int32_t>(window.surface.height())) {
                const uint64_t key =
                    (static_cast<uint64_t>(window.layer) << 56) | window.zOrder;
                if (result == nullptr || key > resultKey) {
                    result    = &window;
                    resultKey = key;
                }
            }
        }
        return result;
    }

    void setFocusLocked(Window& window) {
        if (focusedWindow_ == window.handle) {
            markWindowDirtyLocked(window);
            window.zOrder = nextZOrder_++;
            return;
        }
        Window* previous = find(focusedWindow_);
        if (previous != nullptr) {
            markWindowDirtyLocked(*previous);
        }
        focusedWindow_ = window.handle;
        window.zOrder  = nextZOrder_++;
        markWindowDirtyLocked(window);
        os::WindowEvent event{};
        event.type      = os::WindowEventType::WindowFocus;
        event.timestamp = os::getMonotonicTime();
        window.events.push(event);
        notifyEventWaiterLocked(window);
    }

    void clampCursorLocked() {
        if (cursorX_ < 0) cursorX_ = 0;
        if (cursorY_ < 0) cursorY_ = 0;
        const int32_t maximumX =
            static_cast<int32_t>(graphics_.display().info().width) - 1;
        const int32_t maximumY =
            static_cast<int32_t>(graphics_.display().info().height) - 1;
        if (cursorX_ > maximumX) cursorX_ = maximumX;
        if (cursorY_ > maximumY) cursorY_ = maximumY;
    }

    void clampWindowLocked(Window& window) {
        const int32_t maximumX = static_cast<int32_t>(
            graphics_.display().info().width - window.surface.width());
        const int32_t maximumY = static_cast<int32_t>(
            graphics_.display().info().height - window.surface.height());
        if (window.x < 0) window.x = 0;
        if (window.y < static_cast<int32_t>(titleBarHeight)) {
            window.y = titleBarHeight;
        }
        if (window.x > maximumX) window.x = maximumX;
        if (window.y > maximumY) window.y = maximumY;
    }

    static os::WindowEventType translateEventType(os::InputEventType type) {
        switch (type) {
            case os::InputEventType::KeyDown:
                return os::WindowEventType::KeyDown;
            case os::InputEventType::KeyUp:
                return os::WindowEventType::KeyUp;
            case os::InputEventType::MouseMove:
                return os::WindowEventType::MouseMove;
            case os::InputEventType::MouseButtonDown:
                return os::WindowEventType::MouseButtonDown;
            case os::InputEventType::MouseButtonUp:
                return os::WindowEventType::MouseButtonUp;
            case os::InputEventType::MouseWheel:
                return os::WindowEventType::MouseWheel;
            default:
                return os::WindowEventType::None;
        }
    }

    Rectangle windowBounds(const Window& window) const {
        return {
            window.x - 2, window.y - static_cast<int32_t>(titleBarHeight) - 2,
            static_cast<int32_t>(window.surface.width()) + 4,
            static_cast<int32_t>(window.surface.height() + titleBarHeight) + 4};
    }

    void markWindowDirtyLocked(const Window& window) {
        if (window.active && window.visible) {
            markDirtyLocked(windowBounds(window));
        }
    }

    void markCursorDirtyLocked(int32_t x, int32_t y) {
        markDirtyLocked({x - 6, y - 6, 13, 13});
    }

    void markScreenDirtyLocked() {
        markDirtyLocked(
            {0, 0, static_cast<int32_t>(graphics_.display().info().width),
             static_cast<int32_t>(graphics_.display().info().height)});
    }

    void markDirtyLocked(const Rectangle& rectangle) {
        const Rectangle screen = {
            0, 0, static_cast<int32_t>(graphics_.display().info().width),
            static_cast<int32_t>(graphics_.display().info().height)};
        const Rectangle clipped = intersection(rectangle, screen);
        if (clipped.width <= 0 || clipped.height <= 0) {
            return;
        }
        if (!dirtyValid_) {
            dirty_      = clipped;
            dirtyValid_ = true;
            return;
        }
        const int32_t left = dirty_.x < clipped.x ? dirty_.x : clipped.x;
        const int32_t top  = dirty_.y < clipped.y ? dirty_.y : clipped.y;
        const int32_t right =
            dirty_.x + dirty_.width > clipped.x + clipped.width
                ? dirty_.x + dirty_.width
                : clipped.x + clipped.width;
        const int32_t bottom =
            dirty_.y + dirty_.height > clipped.y + clipped.height
                ? dirty_.y + dirty_.height
                : clipped.y + clipped.height;
        dirty_ = {left, top, right - left, bottom - top};
    }

    void composeDirtyLocked() {
        if (!dirtyValid_) {
            return;
        }
        graphics_.compose(windows_, windowCapacity, cursorX_, cursorY_,
                          focusedWindow_, dirty_);
        dirtyValid_ = false;
    }

    GraphicsServer graphics_;
    Window windows_[windowCapacity]{};
    os::SpinLock lock_;
    uint64_t nextHandle_    = 1;
    uint64_t focusedWindow_ = 0;
    uint64_t nextZOrder_    = 1;
    uint64_t nextFrameTime_ = 0;
    int32_t cursorX_        = 0;
    int32_t cursorY_        = 0;
    Rectangle dirty_{};
    bool dirtyValid_              = false;
    bool sessionActive_           = false;
    uint64_t welcomeWindowHandle_ = 0;
    uint64_t systemWindowHandle_  = 0;
    window::Label welcomeLabel_;
    window::Label systemLabel_;
    window::Label detailLabel_;
    window::Button actionButton_;
    window::TextBox inputBox_;
};

WindowServer windowServer;
uint64_t registryEndpoint;

void inputMain() {
    const uint64_t input =
        os::lookupService(registryEndpoint, os::inputServiceName, 5000);
    os::InputClient client;
    if (!client.connect(input)) {
        os::log("[GUIW] input service unavailable\n");
        return;
    }
    while (true) {
        if (!client.wait()) {
            os::log("[GUIW] input subscription failed\n");
            return;
        }
        os::InputEvent event{};
        while (client.receive(event)) {
            windowServer.handleInput(event);
        }
    }
}

void compositorMain() {
    while (true) {
        windowServer.refresh();
        os::sleep(1);
    }
}

}  // namespace

int main(int, char**) {
    os::log("[GUIW] Window Server started in Ring 3\n");
    registryEndpoint        = os::getBootstrapHandle();
    const uint64_t endpoint = os::createEndpoint();
    if (registryEndpoint == 0 || endpoint == 0 || !windowServer.init() ||
        !os::registerService(registryEndpoint, os::guiwServiceName,
                             endpoint)) {
        os::log("[GUIW] display or service initialization failed\n");
        return 1;
    }
    constexpr uint64_t inputStackAddress = 0x0000000061000000ULL;
    if (os::mapMemory(inputStackAddress, serviceThreadStackSize, 1) !=
            inputStackAddress ||
        os::mapMemory(compositorStackAddress, serviceThreadStackSize, 1) !=
            compositorStackAddress ||
        os::createThread(inputMain,
                         inputStackAddress + serviceThreadStackSize) == 0 ||
        os::createThread(compositorMain, compositorStackAddress +
                                             serviceThreadStackSize) == 0) {
        os::log("[GUIW] unable to start input or compositor thread\n");
        return 2;
    }
    os::log("[GRAPHICS] linear framebuffer and software compositor online\n");
    os::log("[GUIW] layered 60 Hz compositor and input dispatch online\n");
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(endpoint, request) != 0) {
            continue;
        }
        os::IpcMessage reply{};
        windowServer.handle(request, reply);
        os::replyMessage(endpoint, reply);
    }
}
