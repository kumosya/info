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
void *alloc();
void mapping(pt_entry *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void mapping_kernel(pt_entry *pml4, multiboot_tag_elf_sections *elf_sections);
void mapping_identity(pt_entry *pml4, uint64_t size);

void *memset(void *dest, int val, size_t len);
void frame_init(uint64_t start_addr, uint64_t end_addr);
void init(uint8_t *addr);
} // namespace mm
} // namespace boot

#endif /* ENTRY_H */
