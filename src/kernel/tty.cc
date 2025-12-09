#include "tty.h"
#include "start.h"
#include "io.h"
#include "multiboot2.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

using namespace std;

namespace tty {
namespace video {
static int xpos = 0;
static int ypos = 0;

static pt_entry * get_current_pml4() {
    uint64_t cr3;
    asm __volatile__("mov %%cr3, %0" : "=r"(cr3));
    pt_entry *pml4 = (pt_entry *)cr3;
    return pml4;
}

void init(uint8_t *addr) {
    multiboot_mmap_entry *mmap;
    multiboot_tag *tag                 = (multiboot_tag *)(addr + 8);
    multiboot_tag_framebuffer *buf_tag = NULL;
    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            buf_tag = (multiboot_tag_framebuffer *)tag;
        }
        tag = (multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }

    if (buf_tag) {
        type       = 1 << buf_tag->common.framebuffer_type;
        height     = buf_tag->common.framebuffer_height;
        width      = buf_tag->common.framebuffer_width;

        if (type == FRAMEBUFFER_TYPE_EGA_TEXT) {
            size = sizeof(uint8_t);
        } else if (type == FRAMEBUFFER_TYPE_RGB) {
            size = sizeof(uint32_t);
        } else if (type == FRAMEBUFFER_TYPE_INDEXED) {
            size = sizeof(uint16_t);
        }

        for (int addr = 0; addr < height * width * size; addr+= PAGE_SIZE) {
            boot::mm::paging::mapping(get_current_pml4(), 
                (uint64_t)(FRAMEBUFFER_BASE + addr), 
                (uint64_t)(buf_tag->common.framebuffer_addr + addr), 
                PTE_PRESENT | PTE_WRITABLE);
        }
    } else {
        // Fallback to default VGA text mode address and size
        type       = FRAMEBUFFER_TYPE_EGA_TEXT;
        size       = sizeof(uint8_t);
        height     = VIDEO_HEIGHT;
        width      = VIDEO_WIDTH;

        for (int addr = 0; addr < height * width * size; addr+= PAGE_SIZE) {
            boot::mm::paging::mapping(get_current_pml4(), 
                (uint64_t)(FRAMEBUFFER_BASE + addr), 
                (uint64_t)(VIDEO_ADDR + addr), 
                PTE_PRESENT | PTE_WRITABLE);
        }
    }
    
    if (type == FRAMEBUFFER_TYPE_RGB) {
        for (int a = 0; a < height * width; a++) {
            *((uint32_t *)FRAMEBUFFER_BASE + a) = 0xff222222;
        }
    }

#if ENABLE_TEXT_OUTPUT == true || OUTPUT_TO_SERIAL == false
    int i;
    for (i = 0; i < height * width * 2; i++) {
        *(video_addr + i) = 0;
    }
    xpos = 0;
    ypos = 0;
#endif
}

/*  Put a character on the screen. */
void putchar(char c, uint8_t color) {
    if (c == '\n' || c == '\r') {
    newline:
        xpos = 0;
        ypos++;
        if (ypos >= height)
            ypos = 0;
        return;
    }

    if (type == FRAMEBUFFER_TYPE_EGA_TEXT) {
        *((uint8_t *)FRAMEBUFFER_BASE + (xpos + ypos * width) * 2)     = c;
        *((uint8_t *)FRAMEBUFFER_BASE + (xpos + ypos * width) * 2 + 1) = color;
        xpos++;
        if (xpos >= width)
            goto newline;
        return;
    }
} // namespace video

}

static void puts(const char *s, uint8_t color) {
    while (*s)
#if OUTPUT_TO_SERIAL == true
        serial::putc(*s++);
#else
        video::putchar(*s++, color);
#endif // OUTPUT_TO_SERIAL
}

/*  Format a string and print it on the screen, just like the libc
function printf. */
int printf(const char *fmt, ...) {
    va_list argp;
    char str[64];
    int a;
    va_start(argp, fmt);          /*开始使用可变参数*/
    a = vsprintf(str, fmt, argp); /*格式化输出*/
    puts(str, ATTRIBUTE);         /*输出格式化后的字符串*/
    va_end(argp);                 /*停止使用可变参数*/
    return a;
}

void panic(const char *fmt, ...) {
    va_list argp;
    char str[128];
    int a;
    va_start(argp, fmt);          /*开始使用可变参数*/
    a = vsprintf(str, fmt, argp); /*格式化输出*/
    puts("KERNEL PANIC: ", 0x04);
    puts(str, 0x04); /*输出格式化后的字符串*/
    va_end(argp);    /*停止使用可变参数*/
    while (true) {
        asm volatile("hlt");
    }
}
} // namespace tty
