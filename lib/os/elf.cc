#include <os/elf.h>

#include <os/syscall.h>
#include "service/common/runtime.h"
namespace os {
namespace {
struct SectionHeader {
    uint32_t name, type;
    uint64_t flags, address, offset, size;
    uint32_t link, info;
    uint64_t alignment, entrySize;
};
static_assert(sizeof(SectionHeader) == 64);
bool range(uint64_t offset, uint64_t length, size_t size) {
    return offset <= size && length <= size - offset;
}
bool alignment(uint64_t value) { return !value || !(value & (value - 1)); }
int invalid() { return static_cast<int>(ErrorCode::InvalidArgument); }
}  // namespace
Executable::Executable() = default;
Executable::~Executable() { close(); }
int Executable::open(const void* image, size_t size) {
    if (image_) return static_cast<int>(ErrorCode::Busy);
    if (!image || size < sizeof(ElfHeader)) return invalid();
    Executable candidate;
    candidate.image_ = static_cast<const uint8_t*>(image);
    candidate.size_  = size;
    const int status = candidate.validate();
    if (status) return status;
    *this = candidate;
    return 0;
}
void Executable::close() {
    image_        = nullptr;
    size_         = 0;
    valid_        = false;
    architecture_ = type_ = programCount_ = 0;
    entry_ = programOffset_ = interpreterOffset_ = interpreterSize_ = 0;
}
int Executable::validate() {
    valid_        = false;
    architecture_ = type_ = programCount_ = 0;
    entry_ = programOffset_ = interpreterOffset_ = interpreterSize_ = 0;
    if (!image_ || size_ < sizeof(ElfHeader)) return invalid();
    ElfHeader header{};
    Runtime::copyBytes(&header, image_, sizeof(header));
    if (header.identity[0] != 127 || header.identity[1] != 'E' ||
        header.identity[2] != 'L' || header.identity[3] != 'F' ||
        header.identity[6] != 1 || header.version != 1 ||
        header.size != sizeof(header))
        return invalid();
    if (header.identity[4] != 2 || header.identity[5] != 1 ||
        header.machine != 62 || (header.type != 2 && header.type != 3))
        return static_cast<int>(ErrorCode::NotSupported);
    if (!header.programCount || header.programCount > 128 ||
        header.programSize != sizeof(ElfProgramHeader) ||
        header.programOffset > size_ ||
        header.programCount >
            (size_ - header.programOffset) / sizeof(ElfProgramHeader))
        return invalid();
    if (header.sectionCount) {
        if (header.sectionSize != sizeof(SectionHeader) ||
            header.sectionOffset > size_ ||
            header.sectionCount >
                (size_ - header.sectionOffset) / sizeof(SectionHeader) ||
            header.sectionNames >= header.sectionCount)
            return invalid();
        for (uint32_t index = 0; index < header.sectionCount; ++index) {
            SectionHeader section{};
            Runtime::copyBytes(
                &section,
                image_ + header.sectionOffset + index * sizeof(section),
                sizeof(section));
            if (section.type == 0) continue;
            if ((section.type != 8 &&
                 !range(section.offset, section.size, size_)) ||
                section.size > UINT64_MAX - section.address ||
                !alignment(section.alignment) ||
                (section.entrySize && section.size % section.entrySize))
                return invalid();
        }
    } else if (header.sectionOffset || header.sectionNames)
        return static_cast<int>(ErrorCode::NotSupported);
    bool load = false, entry = false;
    uint64_t interpreterOffset = 0, interpreterSize = 0;
    for (uint32_t index = 0; index < header.programCount; ++index) {
        ElfProgramHeader program{};
        Runtime::copyBytes(
            &program, image_ + header.programOffset + index * sizeof(program),
            sizeof(program));
        if (!program.type) continue;
        if (!range(program.offset, program.fileSize, size_) ||
            program.memorySize > UINT64_MAX - program.address)
            return invalid();
        if (program.type == 1) {
            if (program.fileSize > program.memorySize ||
                !alignment(program.alignment) ||
                (program.alignment > 1 &&
                 (program.address % program.alignment !=
                  program.offset % program.alignment)))
                return invalid();
            load = true;
            if ((program.flags & 1) && header.entry >= program.address &&
                header.entry - program.address < program.memorySize)
                entry = true;
        } else if (program.type == 2) {
            if (program.fileSize % sizeof(ElfDynamic)) return invalid();
        } else if (program.type == 3) {
            if (interpreterSize || program.fileSize < 2) return invalid();
            const auto* name = image_ + program.offset;
            if (name[0] != '/' || name[program.fileSize - 1]) return invalid();
            for (uint64_t i = 0; i + 1 < program.fileSize; ++i)
                if (!name[i]) return invalid();
            interpreterOffset = program.offset;
            interpreterSize   = program.fileSize;
        }
    }
    if (!load || (!entry && (header.type == 2 || header.entry)))
        return invalid();
    architecture_      = header.machine;
    type_              = header.type;
    entry_             = header.entry;
    programCount_      = header.programCount;
    programOffset_     = header.programOffset;
    interpreterOffset_ = interpreterOffset;
    interpreterSize_   = interpreterSize;
    valid_             = true;
    return 0;
}
bool Executable::getValid() const { return valid_; }
uint16_t Executable::getArchitecture() const { return architecture_; }
uint16_t Executable::getType() const { return type_; }
uint64_t Executable::getEntry() const { return entry_; }
uint32_t Executable::getProgramCount() const { return programCount_; }
int Executable::getProgram(uint32_t index, ElfProgramHeader& program) const {
    if (!valid_) return static_cast<int>(ErrorCode::InvalidHandle);
    if (index >= programCount_) return invalid();
    Runtime::copyBytes(&program,
                       image_ + programOffset_ + index * sizeof(program),
                       sizeof(program));
    return 0;
}
int Executable::getInterpreter(char* buffer, size_t capacity) const {
    if (!valid_) return static_cast<int>(ErrorCode::InvalidHandle);
    if (!buffer || !capacity || interpreterSize_ > capacity) return invalid();
    if (!interpreterSize_) {
        buffer[0] = 0;
        return 0;
    }
    Runtime::copyBytes(buffer, image_ + interpreterOffset_, interpreterSize_);
    return 1;
}
}  // namespace os
