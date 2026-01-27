#ifndef INFO_KERNEL_PAGE_H_
#define INFO_KERNEL_PAGE_H_

// 页表条目标志位
#define PTE_PRESENT (1 << 0)         // 页存在标志
#define PTE_WRITABLE (1 << 1)        // 可写标志
#define PTE_USER (1 << 2)            // 用户访问标志
#define PTE_WRITE_THROUGH (1 << 3)   // 写直达标志
#define PTE_CACHE_DISABLE (1 << 4)   // 缓存禁用标志
#define PTE_ACCESSED (1 << 5)        // 访问标志
#define PTE_DIRTY (1 << 6)           // 脏页标志
#define PTE_PAGE_SIZE (1 << 7)       // 页大小标志 (0: 4KB, 1: 2MB/1GB)
#define PTE_GLOBAL (1 << 8)          // 全局页标志
#define PTE_NO_EXECUTE (1ULL << 63)  // 不可执行标志

// 物理内存页大小 (4KB)
#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
#define PAGE_MASK (~(PAGE_SIZE - 1))

// 物理页框状态
#define PAGE_FREE 0
#define PAGE_USED 1

/* page table entry */

#define PT_OFFSET 12
#define PD_OFFSET 21
#define PDPT_OFFSET 30
#define PML4_OFFSET 39

#define PT_ENTRY_MASK 0x1ff
#define PT_MASK (PT_ENTRY_MASK << PT_OFFSET)
#define PD_MASK (PT_ENTRY_MASK << PD_OFFSET)
#define PDPT_MASK (PT_ENTRY_MASK << PDPT_OFFSET)
#define PML4_MASK (PT_ENTRY_MASK << PML4_OFFSET)

#define PT_ENTRY(addr) ((addr >> PT_OFFSET) & PT_ENTRY_MASK)
#define PD_ENTRY(addr) ((addr >> PD_OFFSET) & PT_ENTRY_MASK)
#define PDPT_ENTRY(addr) ((addr >> PDPT_OFFSET) & PT_ENTRY_MASK)
#define PML4_ENTRY(addr) ((addr >> PML4_OFFSET) & PT_ENTRY_MASK)

#define IDENTITY_BASE 0xfffff00000000000ULL

/* for C++ code */

#ifndef ASM_FILE

#include <cstddef>
#include <cstdint>

#define PAGE_ALIGN(addr) ((((std::size_t)addr) + (PAGE_SIZE - 1)) & PAGE_MASK)

extern char __boot_end[];

extern char __kernel_start[];
extern char __kernel_end[];

extern char __text_start[];
extern char __text_end[];

extern char __data_start[];
extern char __data_end[];

extern char __rodata_start[];
extern char __rodata_end[];

extern char __bss_start[];
extern char __bss_end[];

// 页表条目结构
union PTE {
    std::uint64_t value;
    struct {
        std::uint64_t present : 1;        // 页存在标志
        std::uint64_t writable : 1;       // 可写标志
        std::uint64_t user_access : 1;    // 用户访问标志
        std::uint64_t write_through : 1;  // 写直达标志
        std::uint64_t cache_disable : 1;  // 缓存禁用标志
        std::uint64_t accessed : 1;       // 访问标志
        std::uint64_t dirty : 1;          // 脏页标志
        std::uint64_t page_size : 1;      // 页大小标志 (0: 4KB, 1: 2MB/1GB)
        std::uint64_t global : 1;         // 全局页标志
        std::uint64_t available : 3;      // 可用位
        std::uint64_t page_addr : 40;     // 页地址 (4KB对齐)
        std::uint64_t available2 : 11;    // 可用位2
        std::uint64_t no_execute : 1;     // 不可执行标志
    } __attribute__((packed));
};

struct Page {
    std::uint64_t flag;
    std::uint64_t vaddr;
    std::uint32_t count;
};

struct FrameMem {
    std::uint64_t total_pages;   // 总页框数
    std::uint64_t free_pages;    // 空闲页框数
    std::uint64_t bitmap_size;   // 位图大小 (字节)
    std::uint8_t *bitmap;        // 页框位图
    Page *pages;                 // 页管理数组
    std::uint64_t start_addr;    // 起始地址 (原始区域起始)
    std::uint64_t start_usable;  // 可用物理起始地址（跳过位图和管理数组）
};

/* in kernel/boot/mm.cc */
extern FrameMem pm;

#endif

#endif /* INFO_KERNEL_PAGE_H_ */