
#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/keyboard.h"
#include "kernel/task.h"
#include "kernel/tty.h"

extern "C" void kbd_handler_c();

extern "C" void kbd_stub(void);

namespace keyboard {
    
bool shift_pressed = false;
bool ctrl_pressed = false;
bool alt_pressed = false;
bool caps_lock = false;

static TTYSwitchCallback tty_switch_callback = nullptr;

void SetTTYSwitchCallback(TTYSwitchCallback callback) {
    tty_switch_callback = callback;
}

InputQueue kbd_buffer;
task::SpinLock kbd_lock;

void InputQueue::ProcessScancode(std::uint8_t sc) {
    bool release = (sc & 0x80) != 0;
    std::uint8_t code = sc & 0x7F;

    if (code >= sizeof(keymap_normal)) {
        return;
    }

    switch (code) {
    case 0x2A:
    case 0x36:
        shift_pressed = !release;
        return;
    case 0x1D:
        ctrl_pressed = !release;
        return;
    case 0x38:
        alt_pressed = !release;
        return;
    case 0x3A:
        if (!release) {
            caps_lock = !caps_lock;
        }
        return;
    }

    if (release) {
        return;
    }

    if (tty_switch_callback && alt_pressed && ctrl_pressed) {
        if (code >= 0x3B && code <= 0x3B + NUM_TTYS) {
            int tty_num = code - 0x3B + 1;
            tty_switch_callback(tty_num);
            return;
        }
    }

    const char *keymap = shift_pressed ? keymap_shift : keymap_normal;
    char c = keymap[code];

    if (c == 0) {
        return;
    }

    if (c >= 'a' && c <= 'z') {
        if (caps_lock || shift_pressed) {
            c = c - 'a' + 'A';
        }
    }

    if (ctrl_pressed) {
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 1;
        } else if (c == '[') {
            c = 0x1B;
        } else if (c == '\\') {
            c = 0x1C;
        } else if (c == ']') {
            c = 0x1D;
        } else if (c == '/') {
            c = 0x1F;
        } else if (c == '3') {
            c = 0x1B;
        }
    }
    
    Insert(c);
}

char InputQueue::ScancodeToChar(std::uint8_t scancode, bool shift) {
    if (scancode > 0x7F) {
        return 0;
    }

    const char *keymap = shift ? keymap_shift : keymap_normal;
    char c = keymap[scancode];

    if (c == 0) {
        return 0;
    }

    if (c >= 'a' && c <= 'z') {
        if (caps_lock || shift) {
            c = c - 'a' + 'A';
        }
    }

    return c;
}

void InputQueue::Insert(char c) {
    //kbd_lock.lock();

    if (count < KBD_BUFFER_SIZE - 1) {
        buffer[tail] = c;
        tail = (tail + 1) % KBD_BUFFER_SIZE;
        count++;
    }

    //kbd_lock.unlock();
}

bool InputQueue::Peek(char *c) {
    kbd_lock.lock();
    
    if (count == 0) {
        kbd_lock.unlock();
        return false;
    }

    *c = buffer[head];
    head = (head + 1) % KBD_BUFFER_SIZE;
    count--;

    kbd_lock.unlock();

    return true;
}

bool InputQueue::HasData() {
    bool result;
    //kbd_lock.lock();
    result = count > 0;
    //kbd_lock.unlock();
    return result;
}

std::uint64_t InputQueue::ReadBuffer(void *buf, std::uint64_t size) {
    std::uint64_t bytes_read = 0;
    char *dest = static_cast<char *>(buf);

    //kbd_lock.lock();

    while (bytes_read < size && count > 0) {
        dest[bytes_read] = buffer[head];
        head = (head + 1) % KBD_BUFFER_SIZE;
        count--;
        bytes_read++;
    }

    //kbd_lock.unlock();

    return bytes_read;
}

void Init() {
    kbd_buffer.Init();
    pic::UnmaskIrq(1);
}

}  // namespace keyboard

extern "C" void kbd_handler_c() {
    std::uint8_t sc = inb(0x60);
    keyboard::kbd_buffer.ProcessScancode(sc);
    outb(PIC1_CMD, 0x20);
}
