#ifndef START_H
#define START_H

#include "multiboot2.h"
#include "page.h"
#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace boot {
int printf(const char *format, ...);
void VideoInit();
static std::uint8_t *video_addr;

static int is_digit(int c);
static int skip_atoi(const char **s);
static char *number(char *str, unsigned long num, int base, int size, int precision, int flags);
static void putchar(char c);
static void puts(const char *s);
static int vsprintf(char *buf, const char *fmt, va_list args);
static size_t strlen(const char *s);

namespace mm {
void *Alloc();
void Mapping(PTE *pml4, std::uint64_t virt_addr, std::uint64_t phys_addr, std::uint64_t flags);
void MappingKernel(PTE *pml4, multiboot_tag_elf_sections *elf_sections);
void MappingIdentity(PTE *pml4, std::uint64_t size);

void *memset(void *dest, int val, size_t len);
void FrameInit(std::uint64_t start_addr, std::uint64_t end_addr);
void Init(std::uint8_t *addr);
}  // namespace mm
}  // namespace boot

#endif /* START_H */
