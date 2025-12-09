#ifndef _TTY_H_
#define _TTY_H_


/*  The attribute of an character. */
#define ATTRIBUTE 7
#define VIDEO_ADDR (uint8_t *)0xb8000
#define VIDEO_WIDTH 80
#define VIDEO_HEIGHT 25

#define LEFT (1<<0)     /* 左对齐标志 */
#define PLUS (1<<1)     /* 显示加号标志 */
#define SPACE (1<<2)    /* 空格填充标志 */
#define SPECIAL (1<<3)  /* 特殊格式标志（如0x前缀） */
#define ZEROPAD (1<<4) /* 零填充标志 */
#define SIGN (1<<5)    /* 有符号数标志 */
#define SMALL (1<<6)   /* 小写字母标志 */

#define FRAMEBUFFER_TYPE_INDEXED (1<<0)
#define FRAMEBUFFER_TYPE_RGB (1<<1)
#define FRAMEBUFFER_TYPE_EGA_TEXT (1<<2)

/* OS Info 显存映射地址 0xffff a000 0000 0000 - 0xffff a010 0000 0000 */

#define FRAMEBUFFER_BASE 0xffffa00000000000ULL      // 映射后的显存起始地址
#define FRAMEBUFFER_LEN        0x1000000000ULL      // 支持的最大显存 (64GiB)

#include <cstdarg>
#include <cstdint>
#include <cstddef>
using namespace std;

/* Here! */
#define OUTPUT_TO_SERIAL    true
#define ENABLE_TEXT_OUTPUT  false

namespace tty {
namespace video {
static uint32_t size;

static uint32_t type;

static uint32_t height;
static uint32_t width;

void init(uint8_t *addr);
void putchar(char c, uint8_t color);
} // namespace video
static void puts(const char *s, uint8_t color);
int printf(const char *format, ...);
void panic(const char *format, ...);
} // namespace tty
#endif /* _VIDEO_H_ */