OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

MEMORY {
    ram (rwx) : ORIGIN = 0x80000000, LENGTH = 128M
}

SECTIONS {
    /* Kernel code and data at 0x80000000 */
    . = 0x80000000;

    .text : {
        _ftext = . ;
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.reginfo)
        *(.init)
        *(.stub)
        *(.gnu.warning)
    } > ram AT>ram
    _etext = .;
    PROVIDE (etext = .);

    .data : {
        _fdata = . ;
        *(.data)
        *(.data.*)
        CONSTRUCTORS
    } > ram AT>ram

    .data1 : { *(.data1) }

    .ctors : {
        __CTOR_LIST__ = .;
        LONG((__CTOR_END__ - __CTOR_LIST__) / 4 - 2)
        *(.ctors)
        LONG(0)
        __CTOR_END__ = .;
    }

    .dtors : {
        __DTOR_LIST__ = .;
        LONG((__DTOR_END__ - __DTOR_LIST__) / 4 - 2)
        *(.dtors)
        LONG(0)
        __DTOR_END__ = .;
    }

    .got : {
        *(.got.plt) *(.got)
    }

    .sdata : { *(.sdata) }
    .lit8 : { *(.lit8) }
    .lit4 : { *(.lit4) }
    _edata = .;
    PROVIDE (edata = .);

    __bss_start = .;
    _fbss = .;
    .sbss : { *(.sbss) *(.scommon) }
    .bss : {
        *(.dynbss)
        *(.bss)
        *(COMMON)
    } > ram AT>ram
    __bbs_end = .;
    _end = . ; __end = .; end = .;
    PROVIDE (end = .);
}
