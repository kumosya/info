#pragma once

#include <stdint.h>

namespace kernel {

constexpr uint32_t multiboot2Magic             = 0x36d76289;
constexpr uint32_t multibootTagEnd             = 0;
constexpr uint32_t multibootTagBootCommandLine = 1;
constexpr uint32_t multibootTagBootLoaderName  = 2;
constexpr uint32_t multibootTagModule          = 3;
constexpr uint32_t multibootTagMemoryMap       = 6;
constexpr uint32_t multibootTagFramebuffer     = 8;
constexpr uint32_t multibootTagElfSections     = 9;
constexpr uint32_t multibootTagAcpiOld         = 14;
constexpr uint32_t multibootTagAcpiNew         = 15;

constexpr uint32_t memoryAvailable = 1;

struct MemoryRegion {
    uint64_t base;
    uint64_t length;
    uint32_t type;
};

struct BootModule {
    uint32_t start;
    uint32_t end;
    const char* commandLine;
};

struct FramebufferInfo {
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bitsPerPixel;
    uint8_t type;
    uint8_t redPosition;
    uint8_t redMaskSize;
    uint8_t greenPosition;
    uint8_t greenMaskSize;
    uint8_t bluePosition;
    uint8_t blueMaskSize;
};

struct BootInfo {
    const MemoryRegion* memoryMap;
    uint32_t memoryMapCount;
    const BootModule* modules;
    uint32_t moduleCount;
    const char* commandLine;
    const char* bootLoaderName;
    const void* acpiRoot;
    const void* elfSections;
    FramebufferInfo framebuffer;
    uint64_t multibootAddress;
    uint32_t multibootSize;
    uint32_t reserved;
    uint64_t pageTableRoot;
    uint64_t bootstrapStart;
    uint64_t bootstrapEnd;
};

bool parseMultibootInfo(uint64_t address, BootInfo* bootInfo);

}  // namespace kernel
