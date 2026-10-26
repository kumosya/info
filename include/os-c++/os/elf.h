#pragma once
#include <cstddef>
#include <cstdint>

namespace os {
struct ElfHeader {
    uint8_t identity[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, programOffset, sectionOffset;
    uint32_t flags;
    uint16_t size, programSize, programCount, sectionSize, sectionCount,
        sectionNames;
};
struct ElfProgramHeader {
    uint32_t type, flags;
    uint64_t offset, address, physical, fileSize, memorySize, alignment;
};
struct ElfDynamic {
    int64_t tag;
    uint64_t value;
};
struct ElfSymbol {
    uint32_t name;
    uint8_t info, visibility;
    uint16_t section;
    uint64_t value, size;
};
struct ElfRela {
    uint64_t offset, info;
    int64_t addend;
};

static_assert(sizeof(ElfHeader) == 64);
static_assert(sizeof(ElfProgramHeader) == 56);
static_assert(sizeof(ElfDynamic) == 16);
static_assert(sizeof(ElfSymbol) == 24);
static_assert(sizeof(ElfRela) == 24);
class Executable {
   public:
    Executable();
    ~Executable();
    int open(const void* image, size_t size);
    void close();
    int validate();
    bool getValid() const;
    uint16_t getArchitecture() const;
    uint16_t getType() const;
    uint64_t getEntry() const;
    uint32_t getProgramCount() const;
    int getProgram(uint32_t index, ElfProgramHeader& program) const;
    int getInterpreter(char* buffer, size_t capacity) const;

   private:
    const uint8_t* image_{};
    size_t size_{};
    bool valid_{};
    uint16_t architecture_{}, type_{}, programCount_{};
    uint64_t entry_{}, programOffset_{}, interpreterOffset_{},
        interpreterSize_{};
};
}  // namespace os
