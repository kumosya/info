#ifndef ENTRY_H
#define ENTRY_H

#include "multiboot2.h"
#include "page.h"
#include <cstdarg>
#include <cstddef>
#include <cstdint>

using namespace std;

namespace boot {
int printf(const char *format, ...);
void video_init();
static uint8_t *video_addr;

static int is_digit(int c);
static int skip_atoi(const char **s);
static char *number(char *str, unsigned long num, int base, int size, int precision, int flags);
static void putchar(char c);
static void puts(const char *s);
static int vsprintf(char *buf, const char *fmt, va_list args);
static size_t strlen(const char *s);

namespace mm {
namespace frame {
    void init(uint64_t start_addr, uint64_t end_addr);
    void *alloc();
    void free(void *addr);
    void *alloc_pages(size_t n);
    void free_pages(void *addr, size_t n);
} // namespace frame
namespace paging {
    void init(multiboot_tag_elf_sections *elf_sections);
    void mapping(pt_entry *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
    void mapping_kernel(pt_entry *pml4, multiboot_tag_elf_sections *elf_sections);
    void mapping_identity(pt_entry *pml4, uint64_t size);
} // namespace paging
    static void *memset(void *dest, int val, size_t len);
    void init(uint8_t *addr);
} // namespace mm
} // namespace boot

#endif /* ENTRY_H */
