#ifndef ENTRY_H
#define ENTRY_H
#include <multiboot2.h>

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <page.h>

/*  The attribute of an character. */
#define ATTRIBUTE               7
#define VIDEO_ADDR              (uint8_t *)0xb8000
#define VIDEO_WIDTH             80
#define VIDEO_HEIGHT            25

#define LEFT        1  /* 左对齐标志 */
#define PLUS        2  /* 显示加号标志 */
#define SPACE       4  /* 空格填充标志 */
#define SPECIAL     8  /* 特殊格式标志（如0x前缀） */
#define ZEROPAD    16  /* 零填充标志 */
#define SIGN       32  /* 有符号数标志 */
#define SMALL      64  /* 小写字母标志 */

namespace boot {
    int printf (const char *format, ...);
    void video_init();
    static uint8_t *video_addr;
    
    static int is_digit(int c);
    static int skip_atoi(const char **s);
    static char *number(char *str, unsigned long num, int base, int size, int precision, int flags);
    static void putchar (char c);
    static void puts (const char *s);
    static int vsprintf (char *buf, const char *fmt, va_list args);
    static size_t strlen (const char *s);

    namespace mm {
        namespace frame {
            // 页框结构
            struct mem {
                uint64_t total_pages;      // 总页框数
                uint64_t free_pages;       // 空闲页框数
                uint64_t bitmap_size;      // 位图大小 (字节)
                uint8_t* bitmap;           // 页框位图
                page* pages;        // 页管理数组
                uint64_t start_addr;       // 起始地址 (原始区域起始)
                uint64_t start_usable;     // 可用物理起始地址（跳过位图和管理数组）
            };
            void init(uint64_t start_addr, uint64_t end_addr);
            void* alloc();
            void free(void* addr);
        }
        namespace paging {
            void init(multiboot_tag_elf_sections *elf_sections);
            void mapping(pt_entry* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
            void mapping_kernel(pt_entry* pml4, multiboot_tag_elf_sections *elf_sections);
            void mapping_identity(pt_entry* pml4, uint64_t size);
        }
        static void *memset (void *dest, int val, size_t len);
        void init(uint8_t *addr);
    }
}

#endif /* ENTRY_H */
