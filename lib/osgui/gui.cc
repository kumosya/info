#include "osgui/gui.h"

#include <mutex>

#include "os/abi.h"

namespace {

constexpr uint32_t subscriptionCapacity = 16;

struct EventSubscription {
    uint64_t service;
    os::WindowHandle window;
    uint64_t endpoint;
};

EventSubscription subscriptions[subscriptionCapacity]{};
std::mutex subscriptionLock;

uint64_t eventEndpoint(uint64_t service, os::WindowHandle window) {
    std::lock_guard<std::mutex> guard(subscriptionLock);
    for (const EventSubscription& subscription : subscriptions) {
        if (subscription.service == service && subscription.window == window) {
            return subscription.endpoint;
        }
    }
    for (EventSubscription& subscription : subscriptions) {
        if (subscription.endpoint != 0) continue;
        const uint64_t endpoint = os::createEndpoint();
        if (endpoint == 0) return 0;
        os::IpcMessage request{};
        request.type =
            static_cast<uint64_t>(os::WindowRequest::SubscribeEvents);
        request.data[0]     = window;
        request.length      = 1;
        request.handleCount = 1;
        request.handles[0]  = endpoint;
        os::IpcMessage reply{};
        if (os::callService(service, request, reply) != 0 ||
            reply.data[0] != 0) {
            os::closeHandle(endpoint);
            return 0;
        }
        subscription = {service, window, endpoint};
        return endpoint;
    }
    return 0;
}

void removeSubscription(uint64_t service, os::WindowHandle window) {
    std::lock_guard<std::mutex> guard(subscriptionLock);
    for (EventSubscription& subscription : subscriptions) {
        if (subscription.service != service || subscription.window != window) {
            continue;
        }
        os::closeHandle(subscription.endpoint);
        subscription = {};
        return;
    }
}

bool command(uint64_t service, os::WindowRequest operation,
             os::IpcMessage& message) {
    message.type = static_cast<uint64_t>(operation);
    os::IpcMessage reply{};
    return os::callService(service, message, reply) == 0 && reply.data[0] == 0;
}

}  // namespace

namespace os {

WindowHandle createWindow(uint64_t service, uint32_t width, uint32_t height,
                          const char* title) {
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(WindowRequest::Create);
    request.data[0] = width;
    request.data[1] = height;
    copyString(reinterpret_cast<char*>(&request.data[2]), title,
               6 * sizeof(uint64_t));
    request.length = 8;
    IpcMessage reply{};
    return callService(service, request, reply) == 0 && reply.data[0] == 0
               ? reply.data[1]
               : 0;
}

bool destroyWindow(uint64_t service, WindowHandle window) {
    IpcMessage request{};
    request.data[0]      = window;
    const bool destroyed = command(service, WindowRequest::Destroy, request);
    if (destroyed) removeSubscription(service, window);
    return destroyed;
}

bool showWindow(uint64_t service, WindowHandle window) {
    IpcMessage request{};
    request.data[0] = window;
    return command(service, WindowRequest::Show, request);
}

bool hideWindow(uint64_t service, WindowHandle window) {
    IpcMessage request{};
    request.data[0] = window;
    return command(service, WindowRequest::Hide, request);
}

bool resizeWindow(uint64_t service, WindowHandle window, uint32_t width,
                  uint32_t height) {
    IpcMessage request{};
    request.data[0] = window;
    request.data[1] = width;
    request.data[2] = height;
    return command(service, WindowRequest::Resize, request);
}

bool moveWindow(uint64_t service, WindowHandle window, int32_t x, int32_t y) {
    IpcMessage request{};
    request.data[0] = window;
    request.data[1] = static_cast<uint64_t>(static_cast<int64_t>(x));
    request.data[2] = static_cast<uint64_t>(static_cast<int64_t>(y));
    return command(service, WindowRequest::Move, request);
}

bool drawPixel(uint64_t service, WindowHandle window, int32_t x, int32_t y,
               uint32_t color) {
    IpcMessage request{};
    request.data[0] = window;
    request.data[1] = static_cast<uint64_t>(static_cast<int64_t>(x));
    request.data[2] = static_cast<uint64_t>(static_cast<int64_t>(y));
    request.data[3] = color;
    return command(service, WindowRequest::DrawPixel, request);
}

bool drawLine(uint64_t service, WindowHandle window, int32_t x0, int32_t y0,
              int32_t x1, int32_t y1, uint32_t color) {
    IpcMessage request{};
    request.data[0] = window;
    request.data[1] = static_cast<uint64_t>(static_cast<int64_t>(x0));
    request.data[2] = static_cast<uint64_t>(static_cast<int64_t>(y0));
    request.data[3] = static_cast<uint64_t>(static_cast<int64_t>(x1));
    request.data[4] = static_cast<uint64_t>(static_cast<int64_t>(y1));
    request.data[5] = color;
    return command(service, WindowRequest::DrawLine, request);
}

bool fillRectangle(uint64_t service, WindowHandle window, int32_t x, int32_t y,
                   uint32_t width, uint32_t height, uint32_t color) {
    IpcMessage request{};
    request.data[0] = window;
    request.data[1] = static_cast<uint64_t>(static_cast<int64_t>(x));
    request.data[2] = static_cast<uint64_t>(static_cast<int64_t>(y));
    request.data[3] = width;
    request.data[4] = height;
    request.data[5] = color;
    return command(service, WindowRequest::FillRectangle, request);
}

bool drawText(uint64_t service, WindowHandle window, int32_t x, int32_t y,
              const char* text, uint32_t color) {
    IpcMessage request{};
    request.data[0] = window;
    request.data[1] = static_cast<uint64_t>(static_cast<int64_t>(x));
    request.data[2] = static_cast<uint64_t>(static_cast<int64_t>(y));
    request.data[3] = color;
    copyString(reinterpret_cast<char*>(&request.data[4]), text,
               6 * sizeof(uint64_t));
    request.length = 10;
    return command(service, WindowRequest::DrawText, request);
}

bool presentWindow(uint64_t service, WindowHandle window) {
    IpcMessage request{};
    request.data[0] = window;
    return command(service, WindowRequest::Present, request);
}

bool getNextWindowEvent(uint64_t service, WindowHandle window,
                        WindowEvent& event) {
    const uint64_t endpoint = eventEndpoint(service, window);
    if (endpoint == 0) return false;
    while (true) {
        IpcMessage request{};
        request.type    = static_cast<uint64_t>(WindowRequest::GetEvent);
        request.data[0] = window;
        request.length  = 1;
        IpcMessage reply{};
        if (callService(service, request, reply) != 0) return false;
        if (static_cast<int64_t>(reply.data[0]) ==
            static_cast<int64_t>(ErrorCode::WouldBlock)) {
            IpcMessage notification{};
            do {
                if (receiveMessage(endpoint, notification) != 0) return false;
            } while (notification.type != windowEventNotification);
            continue;
        }
        if (reply.data[0] != 0) return false;
        const uint8_t* source =
            reinterpret_cast<const uint8_t*>(&reply.data[1]);
        uint8_t* destination = reinterpret_cast<uint8_t*>(&event);
        for (uint32_t index = 0; index < sizeof(event); ++index) {
            destination[index] = source[index];
        }
        return true;
    }
}

bool setWindowSessionActive(uint64_t service, bool active) {
    IpcMessage request{};
    request.data[0] = active ? 1 : 0;
    return command(service, WindowRequest::SetSessionActive, request);
}

bool setWindowLayer(uint64_t service, WindowHandle window, WindowLayer layer) {
    IpcMessage request{};
    request.data[0] = window;
    request.data[1] = static_cast<uint64_t>(layer);
    return command(service, WindowRequest::SetLayer, request);
}

bool raiseWindow(uint64_t service, WindowHandle window) {
    IpcMessage request{};
    request.data[0] = window;
    return command(service, WindowRequest::Raise, request);
}

}  // namespace os
