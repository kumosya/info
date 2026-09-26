#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kernel {

class Process;

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
};

struct ElfSharedImage {
    uint64_t sourceAddress;
    uint64_t size;
    const char* name;
};

class ElfLoader {
   public:
    static ElfLoadError load(Process& process, const void* image, size_t size,
                             ElfLoadResult& result);
    static ElfLoadError loadFromAddressSpace(
        Process& process, uint64_t sourceRoot, uint64_t sourceAddress,
        size_t size, ElfLoadResult& result);
    static ElfLoadError loadFromAddressSpace(
        uint64_t destinationRoot, uint64_t sourceRoot,
        uint64_t sourceAddress, size_t size, ElfLoadResult& result);
    static ElfLoadError loadDynamicSetFromAddressSpace(
        uint64_t destinationRoot, uint64_t sourceRoot,
        uint64_t executableAddress, size_t executableSize,
        const ElfSharedImage* sharedImages, uint32_t sharedImageCount,
        ElfLoadResult& result);
};

const char* getElfLoadErrorName(ElfLoadError error);
bool runElfLoaderValidationSelfTest(Process& process);

}  // namespace kernel
