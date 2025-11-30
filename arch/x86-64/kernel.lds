OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* 内核虚拟地址定义 */
KERM_VADDR = 0xffff800000000000;
KERVM_TEXT_BASE = 0xffff800000000000;
KERVM_DATA_BASE = 0xffffffff80000000;
KERVM_RODATA_BASE = 0xffffffff90000000;
KERVM_BSS_BASE = 0xffffffffA0000000;
KERVM_HEAP_BASE = 0xffffffffC0000000;

SECTIONS
{
    /* 引导加载器将内核加载到1MB地址 */
    . = 1M;
    __boot_start = .;

    /* ASM引导状态内核 */
    .boot.loader.text :
    {
        KEEP(*(.boot.header))      /* 保留引导头 */
        *(.boot.*)                /* 所有引导相关节 */
        kernel/boot.o(.text)    /* 引导汇编文件的引导相关节 */
        kernel/entry.o(.text)      /* 入口代码 */
        kernel/boot_video.o(.text) /* 视频输出代码 */
        kernel/mm.o(.text)         /* 内存管理代码 */
    }
    .boot.loader.rodata :
    {
        kernel/boot.o(.rodata)    /* 引导汇编文件的引导相关节 */
        kernel/entry.o(.rodata)      /* 入口代码 */
        kernel/boot_video.o(.rodata) /* 视频输出代码 */
        kernel/mm.o(.rodata)         /* 内存管理代码 */
    }
    .boot.loader.data :
    {
        kernel/boot.o(.data)    /* 引导汇编文件的引导相关节 */
        kernel/entry.o(.data)      /* 入口代码 */
        kernel/boot_video.o(.data) /* 视频输出代码 */
        kernel/mm.o(.data)         /* 内存管理代码 */
    }

    .boot.loader.bss :
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

    /* 代码段 */
    .kernel.text ALIGN(4096) : AT (__boot_end + SIZEOF(.text))
    {
        *(.text.*)                 /* 所有.text.*节 */
        *(.ltext)                  /* 链接时代码段 */
        *(.ltext.*)                /* 所有.ltext.*节 */
        *(.init.text)              /* 初始化代码 */
        *(.fini.text)              /* 终止代码 */
    }
    /* 入口代码段的起始和结束地址 */
    __entry_text_start = ADDR(.text);
    __entry_text_end = .;

    . = ALIGN(4096);

    /* 64位代码段的起始和结束地址 */
    __kernel_text_start = ADDR(.kernel.text);
    __kernel_text_end = .;

    . = ALIGN(4096);

    /* 64位只读数据段 */
    .kernel.rodata ALIGN(4096) : AT (__boot_end + SIZEOF(.text) + SIZEOF(.kernel.text))
    {
        kernel/boot_video.o(.rodata) /* 视频输出只读数据 */
        kernel/mm.o(.rodata)         /* 内存管理只读数据 */
        kernel/init.o(.rodata)       /* 初始化只读数据 */
        *(.rodata.*)                 /* 所有.rodata.*节 */
        *(.lrodata)                  /* 链接时只读数据段 */
        *(.lrodata.*)                /* 所有.lrodata.*节 */
        *(.init.rodata)              /* 初始化只读数据 */
        *(.fini.rodata)              /* 终止只读数据 */
        *(.srodata)                  /* 小只读数据 */
        *(.srodata.*)                /* 所有.srodata.*节 */
    }
    __rodata_start = ADDR(.kernel.rodata);
    __rodata_end = .;

    . = ALIGN(4096);

    /* 64位数据段 */
    .kernel.data ALIGN(4096) : AT (__boot_end + SIZEOF(.text) + SIZEOF(.kernel.text) + SIZEOF(.kernel.rodata))
    {
        kernel/boot_video.o(.data) /* 视频输出数据 */
        kernel/mm.o(.data)         /* 内存管理数据 */
        kernel/init.o(.data)       /* 初始化数据 */
        *(.data.*)                 /* 所有.data.*节 */
        *(.ldata)                  /* 链接时数据段 */
        *(.ldata.*)                /* 所有.ldata.*节 */
        *(.init.data)              /* 初始化数据 */
        *(.fini.data)              /* 终止数据 */
        *(.sdata)                  /* 小数据 */
        *(.sdata.*)                /* 所有.sdata.*节 */
    }
    __data_start = ADDR(.kernel.data);
    __data_end = .;

    . = ALIGN(4096);

    /* 64位BSS段 */
    .kernel.bss ALIGN(4096) : AT (__boot_end + SIZEOF(.text) + SIZEOF(.kernel.text) + SIZEOF(.kernel.rodata) + SIZEOF(.kernel.data))
    {
        kernel/boot_video.o(.bss)  /* 视频输出BSS */
        kernel/mm.o(.bss)          /* 内存管理BSS */
        kernel/init.o(.bss)        /* 初始化BSS */
        *(COMMON)                  /* 公共符号 */
        *(.bss.*)                  /* 所有.bss.*节 */
        *(.lbss)                   /* 链接时BSS段 */
        *(.lbss.*)                 /* 所有.lbss.*节 */
        *(.sbss)                   /* 小BSS */
        *(.sbss.*)                 /* 所有.sbss.*节 */
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
    }
    __bss_start = ADDR(.kernel.bss);
    __bss_end = .;

    . = ALIGN(4096);

    /* 内核结束标记 */
    __kernel_end = .;
    __heap_start = .;              /* 内核堆起始地址 */
    __heap_end = KERVM_HEAP_BASE + 0x1000000; /* 堆大小：16MB */
}
