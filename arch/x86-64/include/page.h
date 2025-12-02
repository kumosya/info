#ifndef _PAGE_H_
#define _PAGE_H_

/* CR0 */
#define CR0_PG (1 << 31)

/* CR4 */
#define CR4_PSE (1 << 4)
#define CR4_PAE (1 << 5)
#define CR4_PGE (1 << 7)

/* Segment selector */
#define SELECTOR_RPL (0)
#define SELECTOR_TI (2)
#define SELECTOR_INDEX (3)

// 页表条目标志位
#define PTE_PRESENT     (1 << 0)    // 页存在标志
#define PTE_WRITABLE    (1 << 1)    // 可写标志
#define PTE_USER        (1 << 2)    // 用户访问标志
#define PTE_WRITE_THROUGH (1 << 3)  // 写直达标志
#define PTE_CACHE_DISABLE (1 << 4)  // 缓存禁用标志
#define PTE_ACCESSED    (1 << 5)    // 访问标志
#define PTE_DIRTY       (1 << 6)    // 脏页标志
#define PTE_PAGE_SIZE   (1 << 7)    // 页大小标志 (0: 4KB, 1: 2MB/1GB)
#define PTE_GLOBAL      (1 << 8)    // 全局页标志
#define PTE_NO_EXECUTE  (1ULL << 63) // 不可执行标志

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

#define PT_ENTRY_MASK   0b111111111UL
#define PT_MASK (PT_ENTRY_MASK << PT_OFFSET)
#define PD_MASK (PT_ENTRY_MASK << PD_OFFSET)
#define PDPT_MASK (PT_ENTRY_MASK << PDPT_OFFSET)
#define PML4_MASK (PT_ENTRY_MASK << PML4_OFFSET)

#define PT_ENTRY(addr) ((addr >> PT_OFFSET) & PT_ENTRY_MASK)
#define PD_ENTRY(addr) ((addr >> PD_OFFSET) & PT_ENTRY_MASK)
#define PDPT_ENTRY(addr) ((addr >> PDPT_OFFSET) & PT_ENTRY_MASK)
#define PML4_ENTRY(addr) ((addr >> PML4_OFFSET) & PT_ENTRY_MASK)

/* for C++ code */

#ifndef ASM_FILE

#include <stdint.h>

#define PAGE_ALIGN(addr)    ((((size_t) addr) + (PAGE_SIZE - 1)) & PAGE_MASK)

#endif


#endif /* _PAGE_H_ */