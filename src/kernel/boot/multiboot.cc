#include "kernel/boot.h"
#include "kernel/serial.h"

namespace {

struct MultibootTag {
    uint32_t type;
    uint32_t size;
};

struct MultibootMemoryMapTag {
    uint32_t type;
    uint32_t size;
    uint32_t entrySize;
    uint32_t entryVersion;
};

struct MultibootMemoryMapEntry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

struct [[gnu::packed]] MultibootFramebufferTag {
    uint32_t type;
    uint32_t size;
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bitsPerPixel;
    uint8_t framebufferType;
    uint16_t reserved;
    uint8_t redPosition;
    uint8_t redMaskSize;
    uint8_t greenPosition;
    uint8_t greenMaskSize;
    uint8_t bluePosition;
    uint8_t blueMaskSize;
};

constexpr uint32_t maxMemoryRegions = 128;
constexpr uint32_t maxModules       = 32;
constexpr uint64_t directMapBase    = 0xffff800000000000ULL;
kernel::MemoryRegion memoryRegions[maxMemoryRegions];
kernel::BootModule bootModules[maxModules];

const char* stringAfterHeader(const MultibootTag* tag) {
    return reinterpret_cast<const char*>(reinterpret_cast<uint64_t>(tag) + 8 +
                                         directMapBase);
}

}  // namespace

namespace kernel {

bool parseMultibootInfo(uint64_t address, BootInfo* bootInfo) {
    // serial::serialWrite("[BOOT] Multiboot2 address=");
    // serial::serialWriteHex(address);
    serial::serialWrite("\n");
    if (bootInfo == nullptr || address == 0) {
        return false;
    }

    bootInfo->memoryMap        = memoryRegions;
    bootInfo->memoryMapCount   = 0;
    bootInfo->modules          = bootModules;
    bootInfo->moduleCount      = 0;
    bootInfo->commandLine      = nullptr;
    bootInfo->bootLoaderName   = nullptr;
    bootInfo->acpiRoot         = nullptr;
    bootInfo->elfSections      = nullptr;
    bootInfo->framebuffer      = {};
    bootInfo->multibootAddress = address;
    bootInfo->multibootSize    = 0;

    const uint32_t* header   = reinterpret_cast<const uint32_t*>(address);
    const uint32_t totalSize = header[0];
    /*serial::serialWrite("[BOOT] Multiboot2 size=");
    serial::serialWriteDecimal(totalSize);
    serial::serialWrite("\n");*/
    if (totalSize < 16 || totalSize > (16 * 1024 * 1024)) {
        return false;
    }
    bootInfo->multibootSize = totalSize;

    const uint8_t* cursor = reinterpret_cast<const uint8_t*>(address) + 8;
    const uint8_t* end = reinterpret_cast<const uint8_t*>(address) + totalSize;
    while (cursor + sizeof(MultibootTag) <= end) {
        const MultibootTag* tag = reinterpret_cast<const MultibootTag*>(cursor);
        /*serial::serialWrite("[BOOT] tag type=");
        serial::serialWriteDecimal(tag->type);
        serial::serialWrite(" size=");
        serial::serialWriteDecimal(tag->size);
        serial::serialWrite("\n");*/
        if (tag->size < 8 || cursor + tag->size > end) {
            return false;
        }
        if (tag->type == multibootTagEnd) {
            break;
        }
        if (tag->type == multibootTagBootCommandLine) {
            bootInfo->commandLine = stringAfterHeader(tag);
        } else if (tag->type == multibootTagBootLoaderName) {
            bootInfo->bootLoaderName = stringAfterHeader(tag);
        } else if (tag->type == multibootTagMemoryMap &&
                   tag->size >= sizeof(MultibootMemoryMapTag)) {
            const MultibootMemoryMapTag* mapTag =
                reinterpret_cast<const MultibootMemoryMapTag*>(tag);
            if (mapTag->entrySize < sizeof(MultibootMemoryMapEntry)) {
                return false;
            }
            const uint8_t* entryCursor = cursor + sizeof(MultibootMemoryMapTag);
            const uint8_t* entryEnd    = cursor + tag->size;
            while (entryCursor + mapTag->entrySize <= entryEnd &&
                   bootInfo->memoryMapCount < maxMemoryRegions) {
                const MultibootMemoryMapEntry* entry =
                    reinterpret_cast<const MultibootMemoryMapEntry*>(
                        entryCursor);
                MemoryRegion& region =
                    memoryRegions[bootInfo->memoryMapCount++];
                region.base   = entry->base;
                region.length = entry->length;
                region.type   = entry->type;
                entryCursor += mapTag->entrySize;
            }
        } else if (tag->type == multibootTagModule && tag->size >= 16 &&
                   bootInfo->moduleCount < maxModules) {
            const uint32_t* module = reinterpret_cast<const uint32_t*>(tag);
            BootModule& bootModule = bootModules[bootInfo->moduleCount++];
            bootModule.start       = module[2];
            bootModule.end         = module[3];
            bootModule.commandLine = reinterpret_cast<const char*>(
                reinterpret_cast<uint64_t>(module + 4) + directMapBase);
        } else if (tag->type == multibootTagFramebuffer &&
                   tag->size >= sizeof(MultibootFramebufferTag)) {
            const MultibootFramebufferTag* framebuffer =
                reinterpret_cast<const MultibootFramebufferTag*>(tag);
            bootInfo->framebuffer.address      = framebuffer->address;
            bootInfo->framebuffer.pitch        = framebuffer->pitch;
            bootInfo->framebuffer.width        = framebuffer->width;
            bootInfo->framebuffer.height       = framebuffer->height;
            bootInfo->framebuffer.bitsPerPixel = framebuffer->bitsPerPixel;
            bootInfo->framebuffer.type         = framebuffer->framebufferType;
            if (framebuffer->framebufferType == 1 &&
                tag->size >= sizeof(MultibootFramebufferTag)) {
                bootInfo->framebuffer.redPosition = framebuffer->redPosition;
                bootInfo->framebuffer.redMaskSize = framebuffer->redMaskSize;
                bootInfo->framebuffer.greenPosition =
                    framebuffer->greenPosition;
                bootInfo->framebuffer.greenMaskSize =
                    framebuffer->greenMaskSize;
                bootInfo->framebuffer.bluePosition = framebuffer->bluePosition;
                bootInfo->framebuffer.blueMaskSize = framebuffer->blueMaskSize;
            }
        } else if (tag->type == multibootTagElfSections) {
            bootInfo->elfSections = reinterpret_cast<const void*>(
                reinterpret_cast<uint64_t>(tag) + directMapBase);
        } else if (tag->type == multibootTagAcpiOld ||
                   tag->type == multibootTagAcpiNew) {
            bootInfo->acpiRoot = reinterpret_cast<const void*>(
                reinterpret_cast<uint64_t>(tag) + 8 + directMapBase);
        }
        cursor += (tag->size + 7) & ~7U;
    }

    if (bootInfo->memoryMapCount == 0) {
        serial::serialWrite("[BOOT] memory map tag missing\n");
        return false;
    }
    return true;
}

}  // namespace kernel
