#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kernel {

class Task;

enum class ElfLoadError : uint8_t {
    None,
    InvalidArgument,
    InvalidMagic,
    UnsupportedClass,
    UnsupportedEndian,
    UnsupportedVersion,
    UnsupportedMachine,
    UnsupportedType,
    InvalidHeader,
    InvalidProgramHeader,
    UnsupportedProgramHeader,
    InvalidSegment,
    InvalidAddress,
    OverlappingSegments,
    InvalidPermissions,
    OutOfMemory,
    MappingFailed,
    InvalidEntryPoint,
    UndefinedSymbol
};

struct ElfLoadResult {
    uint64_t entryPoint;
    uint64_t imageBase;
    uint64_t imageEnd;
    uint64_t programHeaders = 0;
    uint64_t programHeaderCount = 0;
    uint64_t applicationEntry = 0;
    uint64_t applicationBias = 0;
    uint64_t interpreterBase = 0;
};

struct ElfSharedImage {
    uint64_t sourceAddress;
    uint64_t size;
    const char* name;
};

class ElfLoader {
   public:
    static const char* errorName(ElfLoadError error);
    static bool runValidationSelfTest(Task& task);

    static ElfLoadError load(Task& task, const void* image, size_t size,
                             ElfLoadResult& result);
    static ElfLoadError loadFromAddressSpace(Task& task, uint64_t sourceRoot,
                                             uint64_t sourceAddress,
                                             size_t size,
                                             ElfLoadResult& result);
    static ElfLoadError loadFromAddressSpace(uint64_t destinationRoot,
                                             uint64_t sourceRoot,
                                             uint64_t sourceAddress,
                                             size_t size,
                                             ElfLoadResult& result);
    static ElfLoadError loadDynamicSetFromAddressSpace(
        uint64_t destinationRoot, uint64_t sourceRoot,
        uint64_t executableAddress, size_t executableSize,
        const ElfSharedImage* sharedImages, uint32_t sharedImageCount,
        ElfLoadResult& result, bool trustedImports = false);
};

}  // namespace kernel
