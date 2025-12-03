#ifndef ENTRY_H
#define ENTRY_H
#include <multiboot2.h>

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

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

extern char KERM_VADDR[];
extern char __boot_end;
extern char __kernel_end;
extern char KERM_VADDR[];
extern void cppinit();

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
        // 页表条目结构
        union page_table_entry {
            uint64_t value;
            struct {
                uint64_t present        : 1;   // 页存在标志
                uint64_t writable       : 1;   // 可写标志
                uint64_t user_access    : 1;   // 用户访问标志
                uint64_t write_through  : 1;   // 写直达标志
                uint64_t cache_disable  : 1;   // 缓存禁用标志
                uint64_t accessed       : 1;   // 访问标志
                uint64_t dirty          : 1;   // 脏页标志
                uint64_t page_size      : 1;   // 页大小标志 (0: 4KB, 1: 2MB/1GB)
                uint64_t global         : 1;   // 全局页标志
                uint64_t available      : 3;   // 可用位
                uint64_t page_addr      : 40;  // 页地址 (4KB对齐)
                uint64_t available2     : 11;  // 可用位2
                uint64_t no_execute     : 1;   // 不可执行标志
            } __attribute__((packed));
        };
        namespace frame {
            // 页框结构
            struct mem {
                uint64_t total_pages;      // 总页框数
                uint64_t free_pages;       // 空闲页框数
                uint64_t bitmap_size;      // 位图大小 (字节)
                uint8_t* bitmap;           // 页框位图
                uint64_t start_addr;       // 起始地址
            };
            void init(uint64_t start_addr, uint64_t end_addr);
            void* alloc();
            void free(void* addr);
        }
        namespace page {
            void init(uint64_t start_addr, uint64_t end_addr);
            void mapping(uint64_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
            void mapping_kernel(uint64_t* pml4);
            void mapping_identity(uint64_t* pml4, uint64_t size);
        }
        static void *memset (void *dest, int val, size_t len);
        void init(uint8_t *addr);
    }
}

#endif /* ENTRY_H */
