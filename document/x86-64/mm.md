# 内存管理

## 1. 内存映射

### 1.1 虚拟地址空间布局

| 虚拟地址范围 | 描述 | 访问权限 | 用途 |
| ------------ | ---- | -------- | ---- |
| 0xFFFFF00000000000 - 0xFFFFF80000000000 | 页表区 | 内核态可读写 | 四级页表存储 |
| 0xFFFF900100000000 - 0xFFFF900200000000 | 内核堆 | 内核态可读写 | 内核动态内存分配 |
| 0xFFFF900000000000 - 0xFFFF900100000000 | 内核栈 | 内核态可读写 | 内核线程栈空间 |
| 0xFFFF800030000000 - 0xFFFF800040000000 | 内核.bss段 | 内核态可读写 |  |
| 0xFFFF800020000000 - 0xFFFF800030000000 | 内核.data段 | 内核态可读写 |  |
| 0xFFFF800010000000 - 0xFFFF800020000000 | 内核.rodata段 | 内核态可读写 |  |
| 0xFFFF800000000000 - 0xFFFF800010000000 | 内核.text段 | 内核态可执行 | 内核代码、只读数据 |
| 0xFFFA800000000000 - 0xFFFF800000000000 | 硬件映射区 | 内核态可访问 | 硬件设备内存映射 |
| 0x0000800000000000 - 0x00007FFFFFFFFFFF | 保留区域 | 不可访问 | 不可访问 |
| 0x00007FFFFFFFF000 - 0x00007FFFFFFFFFFF | 用户栈 | 用户态可读写 | 用户进程栈空间 |
| 0x0000555555554000 - 0x00007FFFFFFFF000 | 用户堆 | 用户态可读写 | 用户进程堆空间 |
| 0x0000555555550000 - 0x0000555555554000 | 用户数据段 | 用户态可读写 | 用户全局变量、静态数据 |
| 0x0000555555550000 - 0x0000555555550000 | 用户代码段 | 用户态可执行 | 用户程序代码 |
| 0x0000000000000000 - 0x0000555555550000 | 共享库/映射区 | 用户态可访问 | 共享库、内存映射文件 |

### 1.2 虚拟地址结构

| 位段 | 位数 | 用途 |
| ---- | ---- | ---- |
| 符号扩展 | 16 | 扩展位63-48到全1或全0 |
| PML4索引 | 9 | 索引PML4表项 |
| PDPT索引 | 9 | 索引PDPT表项 |
| PD索引 | 9 | 索引PD表项 |
| PT索引 | 9 | 索引PT表项 |
| 页内偏移 | 12 | 页内字节偏移 |

## 2. 分页内存管理

### 2.1 页表结构

x86-64架构使用四级页表层次结构：

| 页表级别 | 缩写 | 索引位数 | 条目数 |
| -------- | ---- | -------- | ------ |
| Page Map Level 4 | PML4 | 9 | 512 |
| Page Directory Pointer Table | PDPT | 9 | 512 |
| Page Directory | PD | 9 | 512 |
| Page Table | PT | 9 | 512 |

### 2.2 页表项（PTE）结构

每个页表项为64位，包含以下关键字段：

| 位 | 字段 | 描述 |
| -- | ---- | ---- |
| 0 | Present | 1=页存在于物理内存中，0=页不在物理内存中 |
| 1 | Read/Write | 1=可读写，0=只读 |
| 2 | User/Supervisor | 1=用户态可访问，0=仅内核态可访问 |
| 3 | Page-Level Write-Through | 1=写透缓存，0=回写缓存 |
| 4 | Page-Level Cache Disable | 1=禁用缓存，0=启用缓存 |
| 5 | Accessed | 1=页已被访问，0=页未被访问 |
| 6 | Dirty | 1=页已被修改，0=页未被修改 |
| 7 | PAT | 页属性表索引 |
| 8 | Global | 1=全局页（TLB刷新时不失效），0=普通页 |
| 9-11 | Available | 可用位，由软件定义 |
| 12-51 | Physical Address | 物理页基地址（4KB对齐） |
| 52-62 | Available | 可用位，由软件定义 |
| 63 | No-Execute | 1=不可执行，0=可执行 |

### 2.3 大页支持

| 页大小 | 页表层级 | 地址结构 |
| ------ | -------- | -------- |
| 4KB | 四级 | PML4+PDPT+PD+PT+偏移 |
| 2MB | 三级 | PML4+PDPT+PD+偏移 |
| 1GB | 二级 | PML4+PDPT+偏移 |
| 512GB | 一级 | PML4+偏移 |

### 2.4 地址转换过程

1. 从CR3寄存器获取PML4表的物理基地址
2. 使用虚拟地址的PML4索引查找对应的PML4表项
3. PML4表项指向PDPT表的物理基地址
4. 使用PDPT索引查找对应的PDPT表项
5. PDPT表项指向PD表的物理基地址
6. 使用PD索引查找对应的PD表项
7. PD表项指向PT表的物理基地址
8. 使用PT索引查找对应的PT表项
9. PT表项指向物理页的基地址
10. 物理页基地址 + 页内偏移 = 最终物理地址

### 2.5 常见分页相关寄存器

| 寄存器 | 描述 |
| ------ | ---- |
| CR3 | 存储PML4表的物理基地址 |
| CR4.PAE | 启用PAE模式 |
| EFER.LME | 启用长模式（64位模式） |
| EFER.NXE | 启用执行禁止位 |

## 3. MMU实现

### 3.1 页表定义（C++）

**文件名：** `d:\info\arch\x86-64\include\mm\page.h`

```cpp
namespace mm {
    /**
     * @brief 控制寄存器位定义
     */
    // CR0寄存器：分页启用位
    constexpr uint64_t CR0_PG = 1UL << 31;
    // CR4寄存器：页大小扩展位（支持大页）
    constexpr uint64_t CR4_PSE = 1UL << 4;
    // CR4寄存器：物理地址扩展位（支持超过4GB内存）
    constexpr uint64_t CR4_PAE = 1UL << 5;
    // CR4寄存器：全局页启用位（TLB刷新时不失效全局页）
    constexpr uint64_t CR4_PGE = 1UL << 7;

    /**
     * @brief 页大小定义
     */
    // 标准页大小：4KB
    constexpr uint64_t PAGE_SIZE = 1UL << 12;
    // 页掩码：用于页对齐
    constexpr uint64_t PAGE_MASK = ~(PAGE_SIZE - 1);
    
    /**
     * @brief 将地址页对齐
     * @tparam T 地址类型
     * @param addr 要对齐的地址
     * @return 页对齐后的地址
     */
    template<typename T>
    constexpr T PAGE_ALIGN(T addr) {
        return (T)(((uint64_t)addr + PAGE_SIZE - 1) & PAGE_MASK);
    }

    /**
     * @brief 页表项属性位定义
     */
    // 页存在位：1=页在物理内存中
    constexpr uint64_t PTE_ATTR_P = 1UL << 0;
    // 读写权限位：1=可读写，0=只读
    constexpr uint64_t PTE_ATTR_RW = 1UL << 1;
    // 用户/内核权限位：1=用户态可访问，0=仅内核态可访问
    constexpr uint64_t PTE_ATTR_US = 1UL << 2;
    // 页大小位：1=大页（2MB/1GB），0=小页（4KB）
    constexpr uint64_t PDE_ATTR_PS = 1UL << 7;
    // 不可执行位：1=不可执行，0=可执行
    constexpr uint64_t PTE_ATTR_NX = 1UL << 63;

    /**
     * @brief 页表项类型定义
     */
    using pte_t = uint64_t;   // 页表项类型
    using pde_t = uint64_t;   // 页目录项类型
    using pdpe_t = uint64_t;  // 页目录指针表项类型
    using pml4e_t = uint64_t; // 四级页表项类型

    /**
     * @brief 四级页表结构定义
     */
    // 四级页表（PML4）：包含512个PML4表项
    struct PML4Table {
        pml4e_t entries[512];
    };

    // 页目录指针表（PDPT）：包含512个PDPT表项
    struct PDPTable {
        pdpe_t entries[512];
    };

    // 页目录（PD）：包含512个PD表项
    struct PDTable {
        pde_t entries[512];
    };

    // 页表（PT）：包含512个PT表项
    struct PTTable {
        pte_t entries[512];
    };

    /**
     * @brief 地址空间结构
     */
    struct AddressSpace {
        PML4Table* pml4;  // PML4表物理地址
        uint64_t flags;   // 地址空间标志
    };
}
```

### 3.2 临时MMU配置（启动阶段）

**文件名：** `d:\info\arch\x86-64\kernel\boot.S`

```assembly
/* Disable paging (UEFI may turn it on) */
mov     %cr0, %eax        ; 将CR0寄存器值加载到EAX
mov     $mm::CR0_PG, %ebx  ; 将分页启用位掩码加载到EBX
not     %ebx              ; 反转EBX，得到分页禁用掩码
and     %ebx, %eax        ; 清除CR0的PG位，禁用分页
mov     %eax, %cr0        ; 将修改后的值写回CR0寄存器

/* Set up page table for booting stage */
mov     $pt, %eax          ; 将页表地址加载到EAX
or      $(mm::PTE_ATTR_P | mm::PTE_ATTR_RW), %eax  ; 设置页存在位和读写位
mov     %eax, pd          ; 将页表地址写入页目录

xor     %eax, %eax        ; 清空EAX
or      $(mm::PTE_ATTR_P | mm::PTE_ATTR_RW | mm::PDE_ATTR_PS), %eax  ; 设置页存在位、读写位和大页位
movl    %eax, pt          ; 将页表项写入页表，启用2MB大页

/* Enable PAE and PGE */
mov     %cr4, %eax        ; 将CR4寄存器值加载到EAX
or      $(mm::CR4_PAE | mm::CR4_PGE), %eax  ; 设置PAE位和PGE位
mov     %eax, %cr4        ; 将修改后的值写回CR4寄存器

/* Enabling EFER.LME */
mov     $0xC0000080, %ecx  ; 将EFER寄存器MSR地址加载到ECX
rdmsr                     ; 读取EFER寄存器值到EDX:EAX
or      $(1 << 8), %eax   ; 设置EFER.LME位（长模式启用位）
wrmsr                     ; 将修改后的值写回EFER寄存器

/* Enable paging */
mov     %cr0, %eax        ; 将CR0寄存器值加载到EAX
or      $mm::CR0_PG, %eax  ; 设置CR0.PG位，启用分页
mov     %eax, %cr0        ; 将修改后的值写回CR0寄存器，进入长模式

/* Set up GDT */
mov     $gdt64_ptr, %eax  ; 将64位GDT指针加载到EAX
lgdt    0(%eax)           ; 加载64位GDT

/* Reload all the segment registers */
mov $(2 << SELECTOR_INDEX), %ax  ; 将数据段选择子加载到AX
mov     %ax, %ds          ; 更新数据段寄存器
mov     %ax, %ss          ; 更新栈段寄存器
mov     %ax, %es          ; 更新附加段寄存器
mov     %ax, %fs          ; 更新FS段寄存器
mov     %ax, %gs          ; 更新GS段寄存器

/* Enter 64-bit world */
jmp $(1 << SELECTOR_INDEX), $mm::cppstart  ; 跳转到64位C++入口函数
```

### 3.3 64位C++入口

**文件名：** `d:\info\arch\x86-64\kernel\entry.cc`

```cpp
namespace mm {
    /**
     * @brief 64位C++入口函数
     * @param mbi Multiboot信息指针
     * @param magic Multiboot魔数
     */
    extern "C" void cppstart(multiboot_info_t* mbi, uint32_t magic) {
        // 初始化视频输出，用于调试信息输出
        video::init();
        
        // 检查Multiboot魔数，验证引导加载程序的正确性
        if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
            video::printf("Invalid magic number: 0x%x\n", magic);
            // 魔数无效，进入无限循环
            while (true) asm volatile ("hlt");
        }
        
        // 初始化内存管理系统，完成后会跳转到内核init()函数
        mm_init(mbi);
        
        // 无限循环（防止返回，理论上不会执行到这里）
        while (true) asm volatile ("hlt");
    }
}
```

### 3.4 MMU初始化函数（mm_init）

**文件名：** `d:\info\arch\x86-64\kernel\mm.cc`

```cpp
namespace mm {
    // 外部声明内核init函数，位于kernel段
    extern "C" void init();

    /**
     * @brief MMU初始化函数
     * @param mbi Multiboot信息指针，包含内存映射等信息
     */
    void mm_init(multiboot_info_t* mbi) {
        // 1. 解析Multiboot内存映射信息，获取可用物理内存范围
        auto mmap = parse_memory_map(mbi);
        
        // 2. 初始化物理内存管理器，管理可用物理内存
        phys_init(mmap);
        
        // 3. 创建内核页表，建立完整的四级页表结构
        auto kernel_pml4 = pml4_create();
        
        // 4. 映射内核代码段，设置为可执行
        map_kernel_code(kernel_pml4);
        
        // 5. 映射内核数据段，设置为可读写
        map_kernel_data(kernel_pml4);
        
        // 6. 映射内核堆，用于内核动态内存分配
        map_kernel_heap(kernel_pml4);
        
        // 7. 映射页表区，用于存储四级页表
        map_page_tables(kernel_pml4);
        
        // 8. 映射硬件设备区，用于设备内存访问
        map_hardware(kernel_pml4);
        
        // 9. 设置CR3寄存器，加载PML4表物理地址
        set_cr3(reinterpret_cast<uint64_t>(kernel_pml4));
        
        // 10. 刷新TLB，确保新的页表生效
        flush_tlb();
        
        // 输出初始化成功信息
        video::printf("MMU initialized successfully\n");
        
        // 11. 跳转到内核init函数，执行内核主要初始化逻辑
        init();
    }
}
```

### 3.5 页框管理

**文件名：** `d:\info\arch\x86-64\kernel\mm\phys_mem.cc`

```cpp
namespace mm {
    /**
     * @brief 物理内存页框结构
     */
    struct PhysMem {
        uint64_t total_pages;        // 总物理页数
        uint64_t free_pages;         // 空闲物理页数
        uint64_t kernel_reserved;    // 内核保留页数
        uint64_t* bitmap;            // 页框位图（1位表示1页）
        uint64_t bitmap_size;        // 位图大小（字节）
        uint64_t start_addr;         // 物理内存起始地址
    };

    // 物理内存全局实例
    PhysMem g_phys_mem;

    /**
     * @brief 初始化物理内存管理器
     * @param mmap 内存映射信息
     */
    void phys_init(const MemoryMap& mmap) {
        // 初始化物理内存管理器
        // ...
    }

    /**
     * @brief 分配连续物理页
     * @param count 要分配的页数
     * @return 分配的物理页起始地址
     */
    uint64_t phys_alloc(uint64_t count) {
        // 分配连续物理页
        // ...
        return addr;
    }

    /**
     * @brief 释放连续物理页
     * @param addr 要释放的物理页起始地址
     * @param count 要释放的页数
     */
    void phys_free(uint64_t addr, uint64_t count) {
        // 释放连续物理页
        // ...
    }
}
```

### 3.6 页表管理

**文件名：** `d:\info\arch\x86-64\kernel\mm\page_table.cc`

```cpp
namespace mm {
    /**
     * @brief 创建PML4表
     * @return 指向PML4表的指针
     */
    PML4Table* pml4_create() {
        // 分配一个物理页作为PML4表
        auto pml4 = reinterpret_cast<PML4Table*>(phys_alloc(1));
        // 清空PML4表
        memset(pml4, 0, PAGE_SIZE);
        return pml4;
    }

    /**
     * @brief 销毁PML4表
     * @param pml4 要销毁的PML4表指针
     */
    void pml4_destroy(PML4Table* pml4) {
        // 释放PML4表占用的物理页
        phys_free(reinterpret_cast<uint64_t>(pml4), 1);
    }

    /**
     * @brief 映射虚拟页到物理页
     * @param virt_addr 虚拟地址
     * @param phys_addr 物理地址
     * @param flags 页表项属性
     * @param pml4 PML4表指针
     * @return 是否映射成功
     */
    bool page_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags, PML4Table* pml4) {
        // 实现虚拟地址到物理地址的映射
        // ...
        return true;
    }

    /**
     * @brief 解除虚拟页映射
     * @param virt_addr 要解除映射的虚拟地址
     * @param pml4 PML4表指针
     * @return 是否解除成功
     */
    bool page_unmap(uint64_t virt_addr, PML4Table* pml4) {
        // 实现虚拟地址映射的解除
        // ...
        return true;
    }

    /**
     * @brief 虚拟地址到物理地址转换
     * @param virt_addr 虚拟地址
     * @param pml4 PML4表指针
     * @return 对应的物理地址
     */
    uint64_t virt_to_phys(uint64_t virt_addr, PML4Table* pml4) {
        // 实现虚拟地址到物理地址的转换
        // ...
        return phys_addr;
    }
}
```

### 3.7 地址映射函数

**文件名：** `d:\info\arch\x86-64\kernel\mm\map.cc`

```cpp
namespace mm {
    /**
     * @brief 映射内核代码段
     * @param pml4 PML4表指针
     */
    void map_kernel_code(PML4Table* pml4) {
        // 映射内核代码段，设置为可执行
        // ...
    }

    /**
     * @brief 映射内核数据段
     * @param pml4 PML4表指针
     */
    void map_kernel_data(PML4Table* pml4) {
        // 映射内核数据段，设置为可读写
        // ...
    }

    /**
     * @brief 映射内核堆
     * @param pml4 PML4表指针
     */
    void map_kernel_heap(PML4Table* pml4) {
        // 映射内核堆，用于内核动态内存分配
        // ...
    }

    /**
     * @brief 映射页表区
     * @param pml4 PML4表指针
     */
    void map_page_tables(PML4Table* pml4) {
        // 映射页表区，用于存储四级页表
        // ...
    }

    /**
     * @brief 映射硬件设备区
     * @param pml4 PML4表指针
     */
    void map_hardware(PML4Table* pml4) {
        // 映射硬件设备区，用于设备内存访问
        // ...
    }
}
```

### 3.8 控制寄存器操作

**文件名：** `d:\info\arch\x86-64\include\mm\cr.h`

```cpp
namespace mm {
    /**
     * @brief 设置CR3寄存器
     * @param addr PML4表物理地址
     */
    inline void set_cr3(uint64_t addr) {
        // 将PML4表物理地址写入CR3寄存器
        asm volatile ("mov %0, %%cr3" :: "r"(addr) : "memory");
    }

    /**
     * @brief 刷新TLB
     */
    inline void flush_tlb() {
        uint64_t cr3;
        // 读取CR3寄存器值
        asm volatile ("mov %%cr3, %0" : "=r"(cr3));
        // 将相同值写回CR3，刷新所有TLB条目
        asm volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
    }

    /**
     * @brief 刷新指定虚拟地址的TLB条目
     * @param addr 要刷新的虚拟地址
     */
    inline void flush_tlb_page(uint64_t addr) {
        // 刷新指定虚拟地址的TLB条目
        asm volatile ("invlpg (%0)" :: "r"(addr) : "memory");
    }
}
```

### 3.9 内存保护机制

**文件名：** `d:\info\arch\x86-64\kernel\mm\protection.cc`

```cpp
namespace mm {
    /**
     * @brief 页故障处理函数
     * @param frame 中断帧指针，包含故障时的寄存器状态
     */
    void page_fault_handler(InterruptFrame* frame) {
        uint64_t fault_addr;
        // 读取CR2寄存器，获取故障虚拟地址
        asm volatile ("mov %%cr2, %0" : "=r"(fault_addr));
        
        // 获取错误码，包含故障原因
        uint64_t error_code = frame->error_code;
        
        // 根据错误码分析故障原因
        if (error_code & 0x1) {
            // 页不在内存中
            handle_page_not_present(fault_addr, error_code);
        } else if (error_code & 0x2) {
            // 写保护错误：尝试写入只读页
            handle_write_protection(fault_addr, error_code);
        } else if (error_code & 0x4) {
            // 用户态访问内核页：权限不足
            handle_user_access(fault_addr, error_code);
        } else if (error_code & 0x8) {
            // 保留位被设置：页表项中保留位被错误设置
            handle_reserved_bit(fault_addr, error_code);
        } else if (error_code & 0x10) {
            // 执行保护错误：尝试执行不可执行页
            handle_execution_protection(fault_addr, error_code);
        }
    }
}
```

### 3.10 大页映射

**文件名：** `d:\info\arch\x86-64\kernel\mm\large_page.cc`

```cpp
namespace mm {
    /**
     * @brief 映射2MB大页
     * @param virt_addr 虚拟地址
     * @param phys_addr 物理地址
     * @param flags 页表项属性
     * @param pml4 PML4表指针
     * @return 是否映射成功
     */
    bool map_2mb_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags, PML4Table* pml4) {
        // 映射2MB大页，减少页表层级
        // ...
        return true;
    }

    /**
     * @brief 映射1GB大页
     * @param virt_addr 虚拟地址
     * @param phys_addr 物理地址
     * @param flags 页表项属性
     * @param pml4 PML4表指针
     * @return 是否映射成功
     */
    bool map_1gb_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags, PML4Table* pml4) {
        // 映射1GB大页，进一步减少页表层级
        // ...
        return true;
    }
}
```

## 4. 内存池机制

### 4.1 内存池结构

```cpp
namespace mm {
    struct MemPool {
        uint64_t start_addr;
        uint64_t end_addr;
        uint64_t total_size;
        uint64_t free_size;
        uint64_t min_alloc_size;
        uint64_t max_alloc_size;
        void* allocator;
    };

    // 内存池类型
    enum class PoolType {
        KERNEL,
        USER,
        PAGE_TABLE,
        CACHE
    };

    // 内存池实例
    MemPool g_kernel_pool;
    MemPool g_user_pool;
    MemPool g_page_table_pool;
    MemPool g_cache_pool;
}
```

### 4.2 内存池初始化

**文件名：** `d:\info\arch\x86-64\kernel\mm\pool.cc`

```cpp
namespace mm {
    /**
     * @brief 初始化内存池
     * @param pool 内存池指针
     * @param start 内存池起始地址
     * @param size 内存池大小（字节）
     * @param type 内存池类型
     */
    void mem_pool_init(MemPool* pool, uint64_t start, uint64_t size, PoolType type) {
        // 设置内存池基本属性
        pool->start_addr = start;
        pool->end_addr = start + size;
        pool->total_size = size;
        pool->free_size = size;
        
        // 根据内存池类型设置不同的分配器和分配大小
        switch (type) {
            case PoolType::KERNEL:
                // 内核内存池：使用伙伴系统，支持16字节到16MB的分配
                pool->min_alloc_size = 16;
                pool->max_alloc_size = 16 * 1024 * 1024;
                pool->allocator = create_buddy_allocator(start, size);
                break;
            case PoolType::USER:
                // 用户内存池：使用伙伴系统，支持16字节到16MB的分配
                pool->min_alloc_size = 16;
                pool->max_alloc_size = 16 * 1024 * 1024;
                pool->allocator = create_buddy_allocator(start, size);
                break;
            case PoolType::PAGE_TABLE:
                // 页表内存池：使用页分配器，只支持页大小的分配
                pool->min_alloc_size = PAGE_SIZE;
                pool->max_alloc_size = PAGE_SIZE;
                pool->allocator = create_page_allocator(start, size);
                break;
            case PoolType::CACHE:
                // 缓存内存池：使用Slab分配器，支持8字节到4KB的分配
                pool->min_alloc_size = 8;
                pool->max_alloc_size = 4096;
                pool->allocator = create_slab_allocator(start, size);
                break;
        }
    }

    /**
     * @brief 从内存池分配内存
     * @param pool 内存池指针
     * @param size 要分配的大小（字节）
     * @return 分配的内存指针，失败返回nullptr
     */
    void* mem_alloc(MemPool* pool, uint64_t size) {
        // 内存分配实现
        // ...
        return addr;
    }

    /**
     * @brief 释放内存到内存池
     * @param pool 内存池指针
     * @param addr 要释放的内存指针
     */
    void mem_free(MemPool* pool, void* addr) {
        // 内存释放实现
        // ...
    }
}
```

### 4.3 内存分配器实现

**文件名：** `d:\info\arch\x86-64\kernel\mm\allocator.cc`

```cpp
namespace mm {
    /**
     * @brief 创建伙伴系统分配器
     * @param start 分配器起始地址
     * @param size 分配器大小（字节）
     * @return 伙伴系统分配器实例指针
     */
    void* create_buddy_allocator(uint64_t start, uint64_t size) {
        // 创建伙伴系统分配器，用于大块内存分配
        // 伙伴系统将内存划分为不同大小的块，通过合并和分割块来管理内存
        // ...
        return allocator;
    }

    /**
     * @brief 创建Slab分配器
     * @param start 分配器起始地址
     * @param size 分配器大小（字节）
     * @return Slab分配器实例指针
     */
    void* create_slab_allocator(uint64_t start, uint64_t size) {
        // 创建Slab分配器，用于频繁分配释放的小对象
        // Slab分配器将相同大小的对象组织成缓存，提高分配效率
        // ...
        return allocator;
    }

    /**
     * @brief 创建页分配器
     * @param start 分配器起始地址
     * @param size 分配器大小（字节）
     * @return 页分配器实例指针
     */
    void* create_page_allocator(uint64_t start, uint64_t size) {
        // 创建页分配器，用于分配整页内存
        // 主要用于页表分配等需要整页内存的场景
        // ...
        return allocator;
    }
}
```

## 5. MMU测试框架

**文件名：** `d:\info\arch\x86-64\kernel\mm\test.cc`

```cpp
namespace mm {
    /**
     * @brief 测试物理内存分配
     */
    void test_phys_alloc() {
        // 测试物理内存分配和释放功能
        // ...
    }

    /**
     * @brief 测试页表创建和销毁
     */
    void test_pml4_create() {
        // 测试PML4表的创建和销毁
        // ...
    }

    /**
     * @brief 测试地址映射
     */
    void test_page_map() {
        // 测试虚拟地址到物理地址的映射功能
        // ...
    }

    /**
     * @brief 测试内存保护机制
     */
    void test_memory_protection() {
        // 测试内存访问权限控制
        // ...
    }

    /**
     * @brief 测试大页映射
     */
    void test_large_page() {
        // 测试2MB和1GB大页映射
        // ...
    }

    /**
     * @brief 测试缺页异常处理
     */
    void test_page_fault() {
        // 测试缺页异常的处理
        // ...
    }

    /**
     * @brief 运行所有MMU测试
     */
    void mm_test() {
        // 输出测试开始信息
        video::printf("Running MMU tests...\n");
        
        // 运行各项测试
        test_phys_alloc();
        test_pml4_create();
        test_page_map();
        test_memory_protection();
        test_large_page();
        test_page_fault();
        
        // 输出测试完成信息
        video::printf("All MMU tests passed!\n");
    }
}
```

## 6. MMU实现计划

### 6.1 第一阶段：基础架构

| 任务 | 描述 | 完成标志 |
| ---- | ---- | -------- |
| 物理内存管理器 | 实现基于位图的物理内存分配器 | `phys_alloc` 和 `phys_free` 可用 |
| 页表结构 | 实现四级页表结构定义 | 页表结构可用 |
| MMU初始化框架 | 实现MMU初始化函数 | `mm_init` 框架完成 |

### 6.2 第二阶段：核心功能

| 任务 | 描述 | 完成标志 |
| ---- | ---- | -------- |
| 页表管理 | 实现页表创建、映射和解除映射 | `page_map` 和 `page_unmap` 可用 |
| 地址转换 | 实现虚拟地址到物理地址转换 | `virt_to_phys` 可用 |
| 内存保护 | 实现基本内存保护机制 | 非法访问触发异常 |

### 6.3 第三阶段：高级功能

| 任务 | 描述 | 完成标志 |
| ---- | ---- | -------- |
| 大页支持 | 实现2MB和1GB大页映射 | `map_2mb_page` 和 `map_1gb_page` 可用 |
| 缺页异常处理 | 实现缺页异常处理机制 | 缺页异常被正确处理 |
| TLB优化 | 实现TLB管理和优化 | TLB刷新机制可用 |

### 6.4 第四阶段：内存池

| 任务 | 描述 | 完成标志 |
| ---- | ---- | -------- |
| 伙伴系统 | 实现基于伙伴系统的内存分配器 | 内核堆分配可用 |
| Slab分配器 | 实现Slab分配器 | 小对象分配可用 |
| 内存池管理 | 实现内存池初始化和管理 | 内存池API可用 |

### 6.5 第五阶段：测试与优化

| 任务 | 描述 | 完成标志 |
| ---- | ---- | -------- |
| 测试框架 | 实现MMU测试框架 | 所有测试用例通过 |
| 性能优化 | 优化内存分配和页表操作 | 内存分配延迟降低 |
| 代码审查 | 代码审查和文档完善 | 代码质量达标 |

```

```