#ifndef INFO_KERNEL_KEYBOARD_H_
#define INFO_KERNEL_KEYBOARD_H_

#include <cstdint>
#include <cstring>

namespace keyboard {

#define KBD_BUFFER_SIZE 128

const char keymap_normal[] = {
    0,   0,    '1',  '2', '3',  '4', '5', '6', '7', '8', '9', '0', '-',
    '=', '\b', '\t', 'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    '[', ']',  '\n', 0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';', '\'', '`',  0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
    '.', '/',  0,    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0',  '.', 0,   0,   0,   0,
};

const char keymap_shift[] = {
    0,   0,    '!',  '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
    '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '"',  '~',  0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<',
    '>', '?',  0,    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0', '.', 0,   0,   0,   0,
};

extern bool shift_pressed;
extern bool ctrl_pressed;
extern bool alt_pressed;
extern bool caps_lock;

typedef void (*TTYSwitchCallback)(int tty_num);

void SetTTYSwitchCallback(TTYSwitchCallback callback);

class InputQueue {
   public:
    InputQueue() : head(0), tail(0), count(0) {
        memset(buffer, 0, sizeof(buffer));
    }
    ~InputQueue() {}
    void Init() {
        head  = 0;
        tail  = 0;
        count = 0;
        memset(buffer, 0, sizeof(buffer));
    }
    void Insert(char c);
    bool Peek(char *c);
    bool HasData();
    void ProcessScancode(std::uint8_t sc);
    char ScancodeToChar(std::uint8_t scancode, bool shift);
    std::uint64_t ReadBuffer(void *buf, std::uint64_t size);

   private:
    char buffer[KBD_BUFFER_SIZE];
    std::uint64_t head;
    std::uint64_t tail;
    std::uint64_t count;
};

extern InputQueue kbd_buffer;

void Init();

}  // namespace keyboard

#endif  // INFO_KERNEL_KEYBOARD_H_
