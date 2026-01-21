#ifndef INFO_KERNEL_TTY_H_
#define INFO_KERNEL_TTY_H_

/*  The attribute of an character. */
#define ATTRIBUTE 7
#define VIDEO_ADDR 0xb8000
#define VIDEO_WIDTH 80
#define VIDEO_HEIGHT 25

#define LEFT (1 << 0)    /* 左对齐标志 */
#define PLUS (1 << 1)    /* 显示加号标志 */
#define SPACE (1 << 2)   /* 空格填充标志 */
#define SPECIAL (1 << 3) /* 特殊格式标志（如0x前缀） */
#define ZEROPAD (1 << 4) /* 零填充标志 */
#define SIGN (1 << 5)    /* 有符号数标志 */
#define SMALL (1 << 6)   /* 小写字母标志 */

#define FRAMEBUFFER_TYPE_INDEXED (1 << 0)
#define FRAMEBUFFER_TYPE_RGB (1 << 1)
#define FRAMEBUFFER_TYPE_EGA_TEXT (1 << 2)

/* OS Info 显存映射地址 0xffff a000 0000 0000 - 0xffff a010 0000 0000 */

#define FRAMEBUFFER_BASE 0xffffa00000000000ULL  // 映射后的显存起始地址
#define FRAMEBUFFER_LEN 0x1000000000ULL         // 支持的最大显存 (64GiB)

#define DEFAULT_COLOR 0x8f8f8f
#define DEFAULT_BG_COLOR 0x121212

#define NUM_TTYS 6

#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "kernel/font.h"
#include "kernel/mm.h"

#ifndef OUTPUT_TO_SERIAL
#define OUTPUT_TO_SERIAL true
#endif

#ifndef ENABLE_TEXT_OUTPUT
#define ENABLE_TEXT_OUTPUT false
#endif

namespace tty {

int Service(int argc, char *argv[]);

extern FontData *fontdata;

namespace video {
extern std::uint32_t size;

extern std::uint32_t type;

extern std::uint32_t height;
extern std::uint32_t width;

void Init(std::uint8_t *addr);
void Putchar(char c, std::uint8_t color);
}  // namespace video

class Console {
public:
    void Init();
    void SetFont(FontData *font);
    void PutChar(int tty_num, char c, std::uint32_t color);
    void Puts(int tty_num, const char *str, std::uint32_t color);
    void SwitchTTY(int tty_num);
    void Redraw();

    bool IsAltPressed() const { return alt_pressed_; }
    void SetAltPressed(bool pressed) { alt_pressed_ = pressed; }

private:
    void TTYSwitchHandler(int tty_num) {
        SwitchTTY(tty_num);
    }
    class TTYState {
        public:
        int xpos = 0;
        int ypos = 0;
        std::uint32_t *screen_buffer = nullptr;
        bool need_redraw = false;

        TTYState() {
            screen_buffer = reinterpret_cast<std::uint32_t *>(mm::page::Alloc(video::width * video::height * video::size));
        }

        ~TTYState() {
            if (screen_buffer) {
                mm::page::Free(screen_buffer);
                screen_buffer = nullptr;
            }
        }

        TTYState(const TTYState &) = delete;
        TTYState &operator=(const TTYState &) = delete;

        TTYState(TTYState &&) = delete;
        TTYState &operator=(TTYState &&) = delete;
    };

    //Console() = default;

    TTYState ttys_[NUM_TTYS];
    int current_tty_ = 1;
    bool alt_pressed_ = false;
    FontData *fontdata_ = nullptr;
};

int printk(const char *format, ...);
void Panic(const char *format, ...);
}  // namespace tty
#endif /* INFO_KERNEL_TTY_H_ */