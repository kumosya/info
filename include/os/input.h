#pragma once

#include <stdint.h>

namespace os {

enum class InputEventType : uint32_t {
    None,
    KeyDown,
    KeyUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel
};

enum class KeyCode : uint32_t {
    Unknown,
    Escape,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Digit0,
    Backspace,
    Tab,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Enter,
    LeftControl,
    RightControl,
    LeftShift,
    RightShift,
    LeftAlt,
    RightAlt,
    Space,
    CapsLock,
    Minus,
    Equal,
    LeftBracket,
    RightBracket,
    Semicolon,
    Apostrophe,
    Grave,
    Backslash,
    Comma,
    Period,
    Slash,
    Up,
    Down,
    Left,
    Right,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    NumLock,
    ScrollLock,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Keypad0,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    KeypadDecimal,
    KeypadDivide,
    KeypadMultiply,
    KeypadMinus,
    KeypadPlus,
    KeypadEnter
};

constexpr uint32_t keyModifierShift      = 1U << 0;
constexpr uint32_t keyModifierControl    = 1U << 1;
constexpr uint32_t keyModifierAlt        = 1U << 2;
constexpr uint32_t keyModifierCapsLock   = 1U << 3;
constexpr uint32_t keyModifierNumLock    = 1U << 4;
constexpr uint32_t keyModifierScrollLock = 1U << 5;

struct KeyEvent {
    KeyCode keyCode;
    uint32_t modifiers;
    bool pressed;
    uint8_t reserved[3];
};

struct MouseEvent {
    int32_t deltaX;
    int32_t deltaY;
    int32_t wheel;
    uint32_t buttons;
};

struct InputEvent {
    InputEventType type;
    uint32_t reserved;
    uint64_t timestamp;
    union {
        KeyEvent key;
        MouseEvent mouse;
    };
};

enum class InputRequest : uint64_t { GetEvent = 0x200, GetState, Subscribe };

constexpr const char* inputServiceName    = "input";
constexpr uint64_t inputEventNotification = 0x494e505554;

class InputClient {
   public:
    InputClient() = default;
    ~InputClient();

    InputClient(const InputClient&)            = delete;
    InputClient& operator=(const InputClient&) = delete;

    bool connect(uint64_t service);
    bool wait();
    bool receive(InputEvent& event);

   private:
    uint64_t service_      = 0;
    uint64_t wakeEndpoint_ = 0;
};

}  // namespace os
