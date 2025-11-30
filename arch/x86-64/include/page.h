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

/* page size */

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))

/* Page Table Entry attributes */

#define PTE_ATTR_P  (1 << 0)
#define PTE_ATTR_RW (1 << 1)
#define PTE_ATTR_US (1 << 2)
#define PTE_ATTR_PWT    (1 << 3)
#define PTE_ATTR_PCD    (1 << 4)
#define PTE_ATTR_A  (1 << 5)
#define PTE_ATTR_D  (1 << 6)

/* Page Directory Entry attributes*/

#define PDE_ATTR_P     (1 << 0)
#define PDE_ATTR_RW    (1 << 1)
#define PDE_ATTR_PS    (1 << 7)
#define PDE_DEFAULT     (PDE_ATTR_P | PDE_ATTR_RW)

/* page table entry */

#define PT_OFFSET 12
#define PD_OFFSET 21
#define PDPT_OFFSET 30
#define PML4_OFFSET 39

#define PT_ENTRY_MASK 0b111111111UL
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

/* basic types for page table */

typedef uint64_t PML4_t;
typedef uint64_t PDPT_t;
typedef uint64_t PD_t;
typedef uint64_t PT_t;
typedef uint64_t page_attr_t;

#define PAGE_ALIGN(addr)    ((((size_t) addr) + (PAGE_SIZE - 1)) & PAGE_MASK)

#endif


#endif /* _PAGE_H_ */