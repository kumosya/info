/* ELF 符号表使用的固定宽度类型和记录格式。 */
#ifndef _POSIX_ELF_H
#define _POSIX_ELF_H

#include <stdint.h>

typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t Elf32_Sword;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Section;

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Section;

/* 32 位 ELF 的符号名称、位置、长度、类型和所属节。 */
typedef struct {
        Elf32_Word st_name;
        Elf32_Addr st_value;
        Elf32_Word st_size;
        unsigned char st_info;
        unsigned char st_other;
        Elf32_Section st_shndx;
} Elf32_Sym;

/* 64 位 ELF 把类型和所属节放在地址之前，以满足文件格式的排列要求。 */
typedef struct {
        Elf64_Word st_name;
        unsigned char st_info;
        unsigned char st_other;
        Elf64_Section st_shndx;
        Elf64_Addr st_value;
        Elf64_Xword st_size;
} Elf64_Sym;

#define ELF32_ST_BIND(info) ((info) >> 4)
#define ELF32_ST_TYPE(info) ((info) & 0x0f)
#define ELF32_ST_INFO(binding, type) (((binding) << 4) | ((type) & 0x0f))
#define ELF64_ST_BIND(info) ELF32_ST_BIND(info)
#define ELF64_ST_TYPE(info) ELF32_ST_TYPE(info)
#define ELF64_ST_INFO(binding, type) ELF32_ST_INFO(binding, type)

#define SHN_UNDEF 0
#define SHN_LORESERVE 0xff00
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define SHN_XINDEX 0xffff

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2

#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4
#define STT_COMMON 5
#define STT_TLS 6

#endif
