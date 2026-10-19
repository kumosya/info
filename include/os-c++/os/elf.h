#pragma once
#include <stddef.h>
#include <stdint.h>

namespace os {
struct ElfHeader {
    uint8_t identity[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, programOffset, sectionOffset;
    uint32_t flags;
    uint16_t size, programSize, programCount, sectionSize, sectionCount, sectionNames;
};
struct ElfProgramHeader {
    uint32_t type, flags;
    uint64_t offset, address, physical, fileSize, memorySize, alignment;
};
struct ElfDynamic { int64_t tag; uint64_t value; };
struct ElfSymbol {
    uint32_t name;
    uint8_t info, visibility;
    uint16_t section;
    uint64_t value, size;
};
struct ElfRela { uint64_t offset, info; int64_t addend; };

class Executable {
   public:
    // 1: interpreter found, 0: static/boot image, -1: malformed ELF.
    static int interpreter(const void* image, size_t size, char* path, size_t capacity);
};
}  // namespace os
