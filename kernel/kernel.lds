OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* 内核虚拟地址定义 */
KERM_VADDR = 0xffff800000000000;
KERVM_TEXT_BASE = KERM_VADDR;
KERVM_RODATA_BASE = 0xffff800010000000;
KERVM_DATA_BASE = 0xffff800020000000;
KERVM_BSS_BASE = 0xffff800030000000;

SECTIONS
{
    /* 引导加载器将内核加载到1MB地址 */
    . = 1M;
    __boot_start = .;

    /* ASM引导状态内核 */
    .boot :
    {
        KEEP(*(.boot.header))      /* 保留引导头 */
        *(.boot.*)                /* 所有引导相关节 */
        kernel/boot.o    /* 引导汇编文件的引导相关节 */
        kernel/entry.o      /* 入口代码 */
        kernel/boot_video.o /* 视频输出代码 */
        kernel/mm.o         /* 内存管理代码 */
    }
    . = ALIGN(4096);
    .boot.text :
    {
        kernel/boot.o(.text)    /* 引导汇编文件的引导相关节 */
        kernel/entry.o(.text)      /* 入口代码 */
        kernel/boot_video.o(.text) /* 视频输出代码 */
        kernel/mm.o(.text)         /* 内存管理代码 */
    }
    . = ALIGN(4096);
    .boot.rodata :
    {
        kernel/boot.o(.rodata)    /* 引导汇编文件的引导相关节 */
        kernel/entry.o(.rodata)      /* 入口代码 */
        kernel/boot_video.o(.rodata) /* 视频输出代码 */
        kernel/mm.o(.rodata)         /* 内存管理代码 */
    }
    . = ALIGN(4096);
    .boot.data :
    {
        kernel/boot.o(.data)    /* 引导汇编文件的引导相关节 */
        kernel/entry.o(.data)      /* 入口代码 */
        kernel/boot_video.o(.data) /* 视频输出代码 */
        kernel/mm.o(.data)         /* 内存管理代码 */
    }
    . = ALIGN(4096);

    .boot.bss :
    {
        kernel/boot.o(.bss)    /* 引导汇编文件的引导相关节 */
        kernel/entry.o(.bss)      /* 入口代码 */
        kernel/boot_video.o(.bss) /* 视频输出代码 */
        kernel/mm.o(.bss)         /* 内存管理代码 */
    }
    . = ALIGN(4096);

    __boot_end = .;

    /* 64位C内核 */
    . = KERM_VADDR;
    __kernel_start = .;
    __text_start = .;

    /* 代码段 */
    .text ALIGN(4096) : AT (__boot_end)
    {
        *(.text)
        *(.text.*)                 /* 所有.text.*节 */
        *(.ltext)                  /* 链接时代码段 */
        *(.ltext.*)                /* 所有.ltext.*节 */
    }

    . = ALIGN(4096);

    __text_end = .;
    __text_size = __text_end - __text_start;
    . = KERVM_RODATA_BASE;
    __rodata_start = .;

    /* 64位只读数据段 */
    .rodata ALIGN(4096) : AT (__boot_end + __text_size)
    {
        *(.rodata)        
        *(.rodata.*)                 /* 所有.rodata.*节 */
        *(.lrodata)                  /* 链接时只读数据段 */
        *(.lrodata.*)                /* 所有.lrodata.*节 */
    }

    . = ALIGN(4096);

    __rodata_end = .;
    __rodata_size = __rodata_end - __rodata_start;
    . = KERVM_DATA_BASE;
    __data_start = .;

    /* 64位数据段 */
    .kernel.data ALIGN(4096) : AT (__boot_end + __text_size + __rodata_size)
    {
        *(.data)        
        *(.data.*)                 /* 所有.data.*节 */
        *(.ldata)                  /* 链接时数据段 */
        *(.ldata.*)                /* 所有.ldata.*节 */
        
        . = ALIGN(8);

		__init_array = .;
		*(.init_array)
		__init_array_end = .;
		__fini_array = .;
		*(.fini_array)

    }

    . = ALIGN(4096);

    __data_end = .;
    __data_size = __data_end - __data_start;
    . = KERVM_BSS_BASE;
    __bss_start = .;

    /* 64位BSS段 */
    .kernel.bss ALIGN(4096) : AT (__boot_end + __text_size + __rodata_size + __data_size)
    {
        *(COMMON)                  /* 公共符号 */
        *(.bss.*)                  /* 所有.bss.*节 */
        *(.lbss)                   /* 链接时BSS段 */
        *(.lbss.*)                 /* 所有.lbss.*节 */
    }
    __bss_end = .;

    . = ALIGN(4096);

    __kernel_end = .;
}
