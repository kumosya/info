#ifndef _KERNEL_ELF_H
#define _KERNEL_ELF_H

#include <cstdint>

using Elf64_Addr = std::uint64_t;
using Elf64_Off  = std::uint64_t;

#define EI_NIDENT 16

#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define ET_CORE 4
#define EM_X86_64 0x3e

#define PT_LOAD 1

struct Elf64_Phdr {
    std::uint32_t p_type;    // 段类型
    std::uint32_t p_flags;   // 段标志
    Elf64_Off p_offset;      // 段在文件中的偏移
    Elf64_Addr p_vaddr;      // 段的虚拟地址
    Elf64_Addr p_paddr;      // 段的物理地址
    std::uint64_t p_filesz;  // 段在文件中的大小
    std::uint64_t p_memsz;   // 段在内存中的大小
    std::uint64_t p_align;   // 对齐方式
} __attribute__((packed));

struct Elf64_Ehdr {
    std::uint8_t e_ident
        [EI_NIDENT];  // 最开头是16个字节的e_ident,
                      // 其中包含用以表示ELF文件的字符，以及其他一些与机器无关的信息。开头的4个字节值固定不变，为0x7f和ELF三个字符。
    std::uint16_t e_type;     // 该文件的类型 2字节
    std::uint16_t e_machine;  // 该程序需要的体系架构 2字节
    std::uint32_t e_version;  // 文件的版本 4字节
    Elf64_Addr e_entry;       // 程序的入口地址 8字节
    Elf64_Off e_phoff;        // Program header table 在文件中的偏移量 8字节
    Elf64_Off e_shoff;        // Section header table 在文件中的偏移量 8字节
    std::uint32_t e_flags;    // 对IA32而言，此项为0。 4字节
    std::uint16_t e_ehsize;   // 表示ELF header大小 2字节
    std::uint16_t
        e_phentsize;        // 表示Program header table中每一个条目的大小 2字节
    std::uint16_t e_phnum;  // 表示Program header table中有多少个条目 2字节
    std::uint16_t
        e_shentsize;  // 表示Section header table中的每一个条目的大小 2字节
    std::uint16_t e_shnum;     // 表示Section header table中有多少个条目 2字节
    std::uint16_t e_shstrndx;  // 包含节名称的字符串是第几个节 2字节
} __attribute__((packed));

#endif
