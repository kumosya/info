OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    interp PT_INTERP FLAGS(4);
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
    . = 0x400000;
    .interp : { *(.interp) } :text :interp
    .text :
    {
        *(.text.start)
        *(.text .text.*)
        *(.plt .plt.*)
    } :text

    .rodata :
    {
        *(.rodata .rodata.*)
    } :text

    .hash : { *(.hash) } :text
    .gnu.hash : { *(.gnu.hash) } :text
    .dynsym : { *(.dynsym) } :text
    .dynstr : { *(.dynstr) } :text
    .gnu.version : { *(.gnu.version .gnu.version.*) } :text
    .gnu.version_d : { *(.gnu.version_d .gnu.version_d.*) } :text
    .gnu.version_r : { *(.gnu.version_r .gnu.version_r.*) } :text
    .rela.dyn : { *(.rela.dyn .rela.dyn.*) } :text
    .rela.plt : { *(.rela.plt .rela.plt.*) } :text

    . = ALIGN(4096);
    .data :
    {
        *(.data .data.*)
        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
    } :data

    .got : { *(.got) *(.igot) } :data
    .got.plt : { *(.got.plt) *(.igot.plt) } :data

    .dynamic :
    {
        *(.dynamic)
    } :data :dynamic

    . = ALIGN(4096);
    .bss (NOLOAD) :
    {
        *(.bss .bss.* COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.GNU-stack)
    }
}
