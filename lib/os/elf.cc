#include "os/elf.h"
#include "os/abi.h"

namespace os {
int Executable::interpreter(const void* image, size_t size, char* path, size_t capacity) {
    if (!image || size < sizeof(ElfHeader) || !path || !capacity) return -1;
    path[0] = 0;
    const auto& header = *static_cast<const ElfHeader*>(image);
    if (header.identity[0] != 127 || header.identity[1] != 'E' ||
        header.identity[2] != 'L' || header.identity[3] != 'F' ||
        header.identity[4] != 2 || header.identity[5] != 1 ||
        header.machine != 62 || header.version != 1 ||
        header.size != sizeof(ElfHeader) || header.programSize != sizeof(ElfProgramHeader) ||
        !header.programCount || header.programCount > 128 || header.programOffset > size ||
        header.programCount > (size - header.programOffset) / sizeof(ElfProgramHeader)) return -1;
    const auto* programs = reinterpret_cast<const ElfProgramHeader*>(
        static_cast<const uint8_t*>(image) + header.programOffset);
    bool found = false;
    for (uint16_t i = 0; i < header.programCount; ++i) {
        const auto& program = programs[i];
        if (program.type != 3) continue;
        if (found || program.fileSize < 2 || program.fileSize > capacity ||
            program.offset > size || program.fileSize > size - program.offset) return -1;
        const char* name = static_cast<const char*>(image) + program.offset;
        if (name[0] != '/' || name[program.fileSize - 1]) return -1;
        for (uint64_t j = 0; j + 1 < program.fileSize; ++j) if (!name[j]) return -1;
        Runtime::copyBytes(path, name, program.fileSize);
        found = true;
    }
    return found ? 1 : 0;
}
}  // namespace os
