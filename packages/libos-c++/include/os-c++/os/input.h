#pragma once
#include <cstdint>
namespace os {
class Input {
   public:
    enum class Key : uint32_t {
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
    enum Modifier : uint32_t {
        Shift      = 1,
        Control    = 2,
        Alt        = 4,
        CapsLock   = 8,
        NumLock    = 16,
        ScrollLock = 32
    };
    class Event {
       public:
        enum class Type : uint32_t {
            None            = 0,
            KeyDown         = 1,
            KeyUp           = 2,
            MouseMove       = 3,
            MouseButtonDown = 4,
            MouseButtonUp   = 5,
            MouseWheel      = 6
        };
        Type getType() const;
        uint64_t getTimestamp() const;
        Key getKeyCode() const;
        uint32_t getModifiers() const;
        bool getPressed() const;
        int32_t getDeltaX() const;
        int32_t getDeltaY() const;
        int32_t getWheel() const;
        uint32_t getButtons() const;

       private:
        friend class Input;
        Type type_{};
        uint64_t timestamp_{};
        Key key_{};
        uint32_t modifiers_{};
        bool pressed_{};
        int32_t deltaX_{}, deltaY_{}, wheel_{};
        uint32_t buttons_{};
    };
    Input();
    ~Input();
    Input(const Input&)            = delete;
    Input& operator=(const Input&) = delete;
    int open();
    int close();
    int wait(uint64_t deadline);
    bool receive(Event& event);
    int refresh();
    bool getConnected() const;
    bool getKeyPressed(Key key) const;
    uint32_t getModifiers() const;
    uint32_t getButtons() const;
    bool getValid() const;
    int getError() const;

   private:
    uint64_t service_{}, wakeEndpoint_{};
    bool connected_{}, subscriptionPossible_{};
    uint8_t keys_[128]{};
    uint32_t modifiers_{}, buttons_{};
    int error_{};
};
}  // namespace os
