#include <entry.h>
#include <multiboot2.h>

#include <stdint.h>

// 物理内存页大小 (4KB)
#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
#define PAGE_MASK (~(PAGE_SIZE - 1))

// 物理页框状态
#define PAGE_FREE 0
#define PAGE_USED 1

// 外部符号声明
extern char __kernel_start[];
extern char __kernel_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];

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

namespace boot::mm {
    // 全局页框管理实例
    frame_mem pm;

    namespace frame {
        // 初始化物理内存管理
        void init(uint64_t start_addr, uint64_t end_addr) {
            uint64_t total_mem = end_addr - start_addr;
            pm.total_pages = total_mem / PAGE_SIZE;
            pm.free_pages = pm.total_pages;
            pm.start_addr = start_addr;
            
            // 计算位图大小
            pm.bitmap_size = (pm.total_pages + 7) / 8;
            pm.bitmap = (uint8_t*)start_addr;
            
            // 标记前 1MB 内存为已使用 (包含位图本身)
            uint64_t used_pages = (1 * 1024 * 1024) / PAGE_SIZE;
            for (uint64_t i = 0; i < used_pages; i++) {
                pm.bitmap[i / 8] |= (1 << (i % 8));
            }
            pm.free_pages -= used_pages;
            
            boot::printf("Bitmap addr: 0x%lx Bitmap size: 0x%lx\n", (uint64_t)pm.bitmap, pm.bitmap_size);
            boot::printf("Used pages: %ld\n", used_pages);
            boot::printf("Physical memory: 0x%lx - 0x%lx\n", start_addr, end_addr);
            boot::printf("Total pages: %ld\n", pm.total_pages);
            boot::printf("Free pages: %ld\n", pm.free_pages);
            boot::printf("Bitmap size: %ld bytes\n", pm.bitmap_size);
        }

        // 分配一个物理页框
        void* alloc() {
            if (pm.free_pages == 0) {
                return NULL;
            }
            
            // 查找空闲页框
            for (uint64_t i = 0; i < pm.total_pages; i++) {
                if (!(pm.bitmap[i / 8] & (1 << (i % 8)))) {
                    // 标记为已使用
                    pm.bitmap[i / 8] |= (1 << (i % 8));
                    pm.free_pages--;
                    
                    // 计算物理地址
                    uint64_t frame_addr = pm.start_addr + (i * PAGE_SIZE);
                    return (void*)frame_addr;
                }
            }
            
            return NULL;
        }

        // 释放一个物理页框
        void free(void* addr) {
            uint64_t frame_addr = (uint64_t)addr;
            uint64_t page_idx = (frame_addr - pm.start_addr) / PAGE_SIZE;
            
            if (page_idx >= pm.total_pages) {
                return;
            }
            
            // 标记为空闲
            pm.bitmap[page_idx / 8] &= ~(1 << (page_idx % 8));
            pm.free_pages++;
        }
    }

    // 页表管理命名空间
    namespace paging {
        // 初始化页表
        void init() {
            boot::printf("Paging init\n");
            // 分配PML4页
            uint64_t* pml4 = (uint64_t*)frame::alloc();
            if (!pml4) {
                boot::printf("Failed to allocate PML4\n");
                while(true);
            }
            
            // 清空页表
            for (int i = 0; i < 512; i++) {
                pml4[i] = 0;
            }

            // 映射内核段
            mapping_kernel(pml4);
            
            // 映射前2MB物理内存（身份映射）
            for (int i = 0; i < 512; i++) {
                mapping(pml4, i * PAGE_SIZE, i * PAGE_SIZE, PTE_PRESENT | PTE_WRITABLE);
            }
            
            boot::printf("PML4: 0x%lx\n", (uint64_t)pml4);
            boot::printf("Mapped first 2MB physical memory\n");
            
            // 加载PML4到CR3寄存器
            asm volatile ("mov %0, %%cr3" : : "r"((uint64_t)pml4));
            boot::printf("Loaded PML4 to CR3\n");
            
            // 启用分页
            uint64_t cr0;
            asm volatile ("mov %%cr0, %0" : "=r"(cr0));
            cr0 |= (1ULL << 31);  // 设置PG位
            asm volatile ("mov %0, %%cr0" : : "r"(cr0));
            boot::printf("Paging enabled\n");
        }
        // 虚拟内存映射函数
        void mapping(uint64_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
            // 计算页表索引
            uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
            uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
            uint64_t pdt_idx = (virt_addr >> 21) & 0x1FF;
            uint64_t pt_idx = (virt_addr >> 12) & 0x1FF;

            // 获取PML4条目
            uint64_t* pdpt = NULL;
            if (pml4[pml4_idx] & PTE_PRESENT) {
                pdpt = (uint64_t*)(pml4[pml4_idx] & PAGE_MASK);
            } else {    
                pdpt = (uint64_t*)mm::frame::alloc();
                if (!pdpt) {
                    boot::printf("Failed to allocate PDPT\n");
                    while(true);
                }
                pml4[pml4_idx] = ((uint64_t)pdpt) | PTE_PRESENT | PTE_WRITABLE;
            }

            // 获取PDPT条目
            uint64_t* pdt = NULL;
            if (pdpt[pdpt_idx] & PTE_PRESENT) {
                pdt = (uint64_t*)(pdpt[pdpt_idx] & PAGE_MASK);
            } else {
                pdt = (uint64_t*)mm::frame::alloc();
                if (!pdt) {
                    boot::printf("Failed to allocate PDT\n");
                    while(true);
                }
                pdpt[pdpt_idx] = ((uint64_t)pdt) | PTE_PRESENT | PTE_WRITABLE;
            }

            // 获取PDT条目
            uint64_t* pt = NULL;
            if (pdt[pdt_idx] & PTE_PRESENT) {
                pt = (uint64_t*)(pdt[pdt_idx] & PAGE_MASK);
            } else {
                pt = (uint64_t*)mm::frame::alloc();
                if (!pt) {
                    boot::printf("Failed to allocate PT\n");
                    while(true);
                }
                pdt[pdt_idx] = ((uint64_t)pt) | PTE_PRESENT | PTE_WRITABLE;
            }

            // 设置PT条目
            pt[pt_idx] = (phys_addr & PAGE_MASK) | flags;
        }

        // 映射内核段
        void mapping_kernel(uint64_t* pml4) {
            // 内核虚拟地址定义（来自kernel.lds）
            const uint64_t KERM_VADDR = 0xffff800000000000;
            
            // 内核代码段的实际物理地址是从1MB开始的（来自kernel.lds）
            const uint64_t KERNEL_PHYS_BASE = 0x100000;
            
            // 映射内核虚拟地址空间到正确的物理地址
            // 从1MB开始映射前4MB物理内存到内核虚拟地址空间
            for (uint64_t i = 0; i < 1024; i++) {  // 映射4MB (1024 * 4KB)
                uint64_t phys_addr = KERNEL_PHYS_BASE + i * PAGE_SIZE;
                uint64_t virt_addr = KERM_VADDR + i * PAGE_SIZE;
                mapping(pml4, virt_addr, phys_addr, PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL);
//                boot::printf("Mapped 0x%lx -> 0x%lx\n", virt_addr, phys_addr);
            }
        }
    }

    void init(uint8_t *addr) {
        boot::printf("MM init\n");

        struct multiboot_tag *tag =  (struct multiboot_tag *) (addr + 8);;
        struct multiboot_tag_mmap *tag_mmap = NULL;
        struct multiboot_tag_elf_sections *tag_elf_sections = NULL;

        // Find memory map and ELF sections tags
        while (tag->type != MULTIBOOT_TAG_TYPE_END) {
            if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
                tag_mmap = (struct multiboot_tag_mmap*) tag;
            }
            if (tag->type == MULTIBOOT_TAG_TYPE_ELF_SECTIONS) {
                tag_elf_sections = (struct multiboot_tag_elf_sections *) tag;
            }
            tag = (struct multiboot_tag *)((uint8_t *) tag + ((tag->size + 7) & ~7));
        }

        // Print memory map
        /*if (tag_mmap) {
            boot::printf("Memory Map:\n");
            for (int i = 0; 
                 i < (tag_mmap->size - sizeof(*tag_mmap)) / sizeof(struct multiboot_mmap_entry); i++) {
                struct multiboot_mmap_entry* entry = &tag_mmap->entries[i];
                boot::printf(" %d: addr: 0x%lx, size: 0x%lx, type: %d\n",
                    i, entry->addr, entry->len, entry->type);
            }
        }*/

        // Print ELF sections info
        if (tag_elf_sections) {
            boot::printf("ELF sections num: %d entsize: %d shndx: %d\n", 
                tag_elf_sections->num, tag_elf_sections->entsize, tag_elf_sections->shndx);
        }

        // Initialize frameical memory management
        uint64_t mem_start = 0;
        uint64_t mem_end = 0;
        
        // Find the largest available memory region
        if (tag_mmap) {
            for (int i = 0; 
                 i < (tag_mmap->size - sizeof(*tag_mmap)) / sizeof(struct multiboot_mmap_entry); i++) {
                struct multiboot_mmap_entry* entry = &tag_mmap->entries[i];
                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    if (entry->len > (mem_end - mem_start)) {
                        mem_start = entry->addr;
                        mem_end = entry->addr + entry->len;
                    }
                }
            }
        }
        
        // Initialize frameical memory management
        frame::init(mem_start, mem_end);
        
        // Test page allocation
        void* page1 = frame::alloc();
        void* page2 = frame::alloc();
        void* page3 = frame::alloc();
        
        boot::printf("Allocated pages: 0x%lx, 0x%lx, 0x%lx\n", 
            (uint64_t)page1, (uint64_t)page2, (uint64_t)page3);
        boot::printf("Free pages after alloc: %ld\n", mm::pm.free_pages);
        
        // Test page free
        frame::free(page1);
        boot::printf("Free pages after free: %ld\n", mm::pm.free_pages);
        
        // Initialize paging
        mm::paging::init();
        
        return;
    }
}
