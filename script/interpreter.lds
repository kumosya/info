OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x0000070000000000;
    .text : { *(.text.start) *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text
    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
        . = ALIGN(8);
        __init_array_start = .; KEEP(*(.init_array .init_array.*)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array .fini_array.*)) __fini_array_end = .;
    } :data
    .got : { *(.got .got.* .igot .igot.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) } :data
    /DISCARD/ : { *(.comment .eh_frame .note.GNU-stack) }
}
