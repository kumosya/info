OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_BASE = 0xffff800000000000;

SECTIONS
{
    . = 1M;
    __bootStart = .;

    .boot.header ALIGN(8) :
    {
        KEEP(*(.multiboot))
        KEEP(*(.boot.header))
    }

    .boot.text ALIGN(4096) :
    {
        *(.boot.text*)
    }

    .boot.rodata ALIGN(4096) :
    {
        *(.boot.rodata*)
    }

    .boot.data ALIGN(4096) :
    {
        *(.boot.data*)
    }

    . = ALIGN(4096);
    .boot.bss (NOLOAD) :
    {
        . = ALIGN(4096);
        *(.boot.bss*)
    }
    . = ALIGN(4096);
    __bootEnd = .;

    . = KERNEL_BASE + __bootEnd;

    .text : AT(ADDR(.text) - KERNEL_BASE)
    {
        __textStart = .;
        *(.text .text.*)
        __textEnd = .;
    }

    . = ALIGN(4096);
    .rodata : AT(ADDR(.rodata) - KERNEL_BASE)
    {
        __rodataStart = .;
        *(.rodata .rodata.*)
        KEEP(*(.ap_trampoline))
        __rodataEnd = .;
    }

    . = ALIGN(4096);
    .data : AT(ADDR(.data) - KERNEL_BASE)
    {
        __dataStart = .;
        *(.data .data.*)
        *(.init_array .init_array.*)
        *(.fini_array .fini_array.*)
        __dataEnd = .;
    }

    . = ALIGN(4096);
    .bss (NOLOAD) : AT(ADDR(.bss) - KERNEL_BASE)
    {
        __bssStart = .;
        *(COMMON)
        *(.bss .bss.*)
        *(.lbss .lbss.*)
        __bssEnd = .;
    }
    . = ALIGN(4096);
    __kernelEnd = .;
    __kernelLoadStart = LOADADDR(.text);
    __kernelLoadEnd = LOADADDR(.bss) + SIZEOF(.bss);

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.GNU-stack)
    }
}
