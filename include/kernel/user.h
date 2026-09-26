#pragma once

#include <stdint.h>

#include "kernel/elf.h"
#include "os/abi.h"

namespace kernel {

struct BootInfo;
class Process;

class UserProgramLoader {
   public:
    static Process* load(os::ProgramId program, Process* parent,
                         uint64_t bootstrapHandle = 0,
                         uint32_t argumentCount = 0,
                         const char* const* arguments = nullptr,
                         ElfLoadError* loadError  = nullptr);
    static Process* loadImage(uint64_t sourceAddress, size_t imageSize,
                              const char* name, Process* source,
                              Process* parent,
                              uint64_t bootstrapHandle, uint64_t capabilities,
                              uint64_t standardInputHandle,
                              uint64_t standardOutputHandle,
                              uint64_t standardErrorHandle,
                              uint32_t argumentCount,
                              const char* const* arguments,
                              const ElfSharedImage* sharedImages,
                              uint32_t sharedImageCount,
                              ElfLoadError* loadError = nullptr);
};

ElfLoadError buildUserStack(uint64_t rootPhysicalAddress,
                            const char* programName,
                            uint32_t argumentCount,
                            const char* const* arguments,
                            uint32_t environmentCount,
                            const char* const* environment,
                            uint64_t runtimeStateAddress,
                            uint64_t& stackPointer);

Process* startInitialUserland(const BootInfo* bootInfo);

}  // namespace kernel
