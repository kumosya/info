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

#include <cstdarg>
#include <cstddef>
#include <cstdint>

#ifndef OUTPUT_TO_SERIAL
#define OUTPUT_TO_SERIAL true
#endif

#ifndef ENABLE_TEXT_OUTPUT
#define ENABLE_TEXT_OUTPUT false
#endif

namespace tty {

int Proc(int argc, char *argv[]);

namespace video {
static std::uint32_t size;

static std::uint32_t type;

static std::uint32_t height;
static std::uint32_t width;

void Init(std::uint8_t *addr);
void Putchar(char c, std::uint8_t color);
}  // namespace video
int printf(const char *format, ...);
void Panic(const char *format, ...);
}  // namespace tty
#endif /* INFO_KERNEL_TTY_H_ */