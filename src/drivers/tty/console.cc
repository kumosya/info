#include <cstdint>
#include <cstdio>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/keyboard.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace tty {

task::SpinLock tty_lock;

int Service(int argc, char *argv[]) {
    Console con;
    con.Init();

    keyboard::Init();

    con.SetFont(const_cast<FontData *>(&fontdata_8x16));

    for (int t = 1; t <= NUM_TTYS; t++) {
        char tty_name[16];
        std::sprintf(tty_name, "[ TTY %d ]\n", t);
        con.Puts(t, tty_name, DEFAULT_COLOR);
    }

    while (true) {
        char c;
        bool status, reply;
        task::ipc::Message msg;
        if (task::ipc::Receive(&msg)) {
            reply = true;
            status = false;
            
            switch (msg.type) {
            case 0:
                con.Puts(msg.sender->tty, msg.data, DEFAULT_COLOR);
                reply = false;
                break;
            case 1:
                while (true) {
                    status = keyboard::kbd_buffer.Peek(&c);
                    if (status) {
                        msg.dst_pid = msg.sender->pid;
                        msg.sender = task::current_proc;
                        msg.type = 0;
                        msg.num[0] = c;
                        msg.num[1] = status;
                        break;
                    }
                }
                break;
            default:
                tty::Panic("Unknown message type: %d\n", msg.type);
                break;
            }
            if (reply) {
                task::ipc::Send(&msg);
            }
        }
    }
    return 0;
}

static Console *g_console = nullptr;

void TTYSwitchCallbackImpl(int tty_num) {
    if (g_console) {
        g_console->SwitchTTY(tty_num);
    }
}

void Console::Init() {
    tty_lock.lock();
    g_console = this;
    keyboard::SetTTYSwitchCallback(TTYSwitchCallbackImpl);
    if (video::type == FRAMEBUFFER_TYPE_RGB) {
        for (int t = 0; t < NUM_TTYS; t++) {
            if (ttys_[t].screen_buffer) {
                for (int a = 0; a < video::height * video::width; a++) {
                    ttys_[t].screen_buffer[a] = (0xff << 24) | DEFAULT_BG_COLOR;
                }
            }
        }
    }
    tty_lock.unlock();
    Redraw();
}

void Console::SetFont(FontData *font) {
    fontdata_ = font;
}

void Console::SwitchTTY(int tty_num) {
    tty_lock.lock();
    if (tty_num < 1 || tty_num > NUM_TTYS) return;
    if (tty_num == current_tty_) return;

    ttys_[current_tty_ - 1].need_redraw = true;
    current_tty_ = tty_num;
    ttys_[current_tty_ - 1].need_redraw = true;

    tty_lock.unlock();

    Redraw();
}

void Console::Redraw() {
    tty_lock.lock();
    std::uint32_t *fb = (std::uint32_t *)FRAMEBUFFER_BASE;
    TTYState &tty = ttys_[current_tty_ - 1];
    
    if (tty.screen_buffer) {
        for (int i = 0; i < video::width * video::height; i++) {
            fb[i] = tty.screen_buffer[i];
        }
    }
    tty.need_redraw = false;
    tty_lock.unlock();
}

void Console::PutChar(int tty_num, char c, std::uint32_t color) {
    tty_lock.lock();
    int n = tty_num - 1;
    if (tty_num < 1 || tty_num > NUM_TTYS) {
        tty_lock.unlock();
        return;
    } else if (tty_num == 0) {
        n = current_tty_ - 1;
    }
    TTYState &tty = ttys_[n];
    std::uint32_t *fb = (std::uint32_t *)FRAMEBUFFER_BASE;

    if (!tty.screen_buffer || !fontdata_) return;

    if (tty.xpos >= video::width) {
        tty.xpos = 0;
        tty.ypos += fontdata_->hdr.height;
    }
    if (tty.ypos >= video::height) {
        tty.ypos = 0;
    }

    switch (c) {
    case '\n':
        tty.xpos = 0;
        tty.ypos += fontdata_->hdr.height;
        break;
    case '\r':
        break;
    case '\b':
        tty.xpos -= fontdata_->hdr.width;
        if (tty.xpos < 0) {
            tty.xpos = 0;
        }
        for (int y = 0; y < fontdata_->hdr.height; y++) {
            for (int x = 0; x < fontdata_->hdr.width; x++) {
                std::uint32_t pixel = (0xff << 24) | DEFAULT_BG_COLOR;
                tty.screen_buffer[(tty.ypos + y) * video::width + (tty.xpos + x)] = pixel;
                if (tty_num == current_tty_) {
                    fb[(tty.ypos + y) * video::width + (tty.xpos + x)] = pixel;
                }
            }
        }
        break;
    default:
        for (int y = 0; y < fontdata_->hdr.height; y++) {
            for (int x = 0; x < fontdata_->hdr.width; x++) {
                std::uint32_t pixel;
                if (fontdata_->data[c * fontdata_->hdr.height + y] & (1 << (7 - x))) {
                    pixel = (0xff << 24) | color;
                } else {
                    pixel = (0xff << 24) | DEFAULT_BG_COLOR;
                }
                tty.screen_buffer[(tty.ypos + y) * video::width + (tty.xpos + x)] = pixel;
                if (tty_num == current_tty_) {
                    fb[(tty.ypos + y) * video::width + (tty.xpos + x)] = pixel;
                }
            }
        }
        tty.xpos += fontdata_->hdr.width;
        break;
    }
    tty_lock.unlock();
}

void Console::Puts(int tty_num, const char *str, std::uint32_t color) {
    char *p = const_cast<char *>(str);
    while (*p) {
        PutChar(tty_num, *p, color);
        p++;
    }
}

}  // namespace tty
