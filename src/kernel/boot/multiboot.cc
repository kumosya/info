#include "kernel/boot.h"
#include "kernel/device.h"

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
::MemoryRegion memoryRegions[maxMemoryRegions];
::BootModule bootModules[maxModules];

const char* stringAfterHeader(const MultibootTag* tag) {
    return reinterpret_cast<const char*>(reinterpret_cast<uint64_t>(tag) + 8 +
                                         directMapBase);
}

}  // namespace

bool parseMultibootInfo(uint64_t address, BootInfo* bootInfo) {
    // device::serialWrite("[BOOT] Multiboot2 address=");
    // device::serialWriteHex(address);
    kernel::device::serialWrite("\n");
    if (bootInfo == nullptr || address == 0) {
        return false;
    }

    bootInfo->setMemoryMap(memoryRegions);
    bootInfo->setMemoryMapCount(0);
    bootInfo->setModules(bootModules);
    bootInfo->setModuleCount(0);
    bootInfo->setCommandLine(nullptr);
    bootInfo->setBootLoaderName(nullptr);
    bootInfo->setAcpiRoot(nullptr);
    bootInfo->setElfSections(nullptr);
    bootInfo->setFramebuffer({});
    bootInfo->setMultibootAddress(address);
    bootInfo->setMultibootSize(0);

    const uint32_t* header   = reinterpret_cast<const uint32_t*>(address);
    const uint32_t totalSize = header[0];
    /*device::serialWrite("[BOOT] Multiboot2 size=");
    device::serialWriteDecimal(totalSize);
    device::serialWrite("\n");*/
    if (totalSize < 16 || totalSize > (16 * 1024 * 1024)) {
        return false;
    }
    bootInfo->setMultibootSize(totalSize);

    const uint8_t* cursor = reinterpret_cast<const uint8_t*>(address) + 8;
    const uint8_t* end = reinterpret_cast<const uint8_t*>(address) + totalSize;
    while (cursor + sizeof(MultibootTag) <= end) {
        const MultibootTag* tag = reinterpret_cast<const MultibootTag*>(cursor);
        /*device::serialWrite("[BOOT] tag type=");
        device::serialWriteDecimal(tag->type);
        device::serialWrite(" size=");
        device::serialWriteDecimal(tag->size);
        device::serialWrite("\n");*/
        if (tag->size < 8 || cursor + tag->size > end) {
            return false;
        }
        if (tag->type == multibootTagEnd) {
            break;
        }
        if (tag->type == multibootTagBootCommandLine) {
            bootInfo->setCommandLine(stringAfterHeader(tag));
        } else if (tag->type == multibootTagBootLoaderName) {
            bootInfo->setBootLoaderName(stringAfterHeader(tag));
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
                   bootInfo->getMemoryMapCount() < maxMemoryRegions) {
                const MultibootMemoryMapEntry* entry =
                    reinterpret_cast<const MultibootMemoryMapEntry*>(
                        entryCursor);
                MemoryRegion& region =
                    memoryRegions[bootInfo->getMemoryMapCount()];
                bootInfo->setMemoryMapCount(bootInfo->getMemoryMapCount() + 1);
                region.setBase(entry->base);
                region.setLength(entry->length);
                region.setType(entry->type);
                entryCursor += mapTag->entrySize;
            }
        } else if (tag->type == multibootTagModule && tag->size >= 16 &&
                   bootInfo->getModuleCount() < maxModules) {
            const uint32_t* module = reinterpret_cast<const uint32_t*>(tag);
            BootModule& bootModule = bootModules[bootInfo->getModuleCount()];
            bootInfo->setModuleCount(bootInfo->getModuleCount() + 1);
            bootModule.setStart(module[2]);
            bootModule.setEnd(module[3]);
            bootModule.setCommandLine(reinterpret_cast<const char*>(
                reinterpret_cast<uint64_t>(module + 4) + directMapBase));
        } else if (tag->type == multibootTagFramebuffer &&
                   tag->size >= sizeof(MultibootFramebufferTag)) {
            const MultibootFramebufferTag* framebuffer =
                reinterpret_cast<const MultibootFramebufferTag*>(tag);
            bootInfo->getFramebuffer().setAddress(framebuffer->address);
            bootInfo->getFramebuffer().setPitch(framebuffer->pitch);
            bootInfo->getFramebuffer().setWidth(framebuffer->width);
            bootInfo->getFramebuffer().setHeight(framebuffer->height);
            bootInfo->getFramebuffer().setBitsPerPixel(
                framebuffer->bitsPerPixel);
            bootInfo->getFramebuffer().setType(framebuffer->framebufferType);
            if (framebuffer->framebufferType == 1 &&
                tag->size >= sizeof(MultibootFramebufferTag)) {
                bootInfo->getFramebuffer().setRedPosition(
                    framebuffer->redPosition);
                bootInfo->getFramebuffer().setRedMaskSize(
                    framebuffer->redMaskSize);
                bootInfo->getFramebuffer().setGreenPosition(
                    framebuffer->greenPosition);
                bootInfo->getFramebuffer().setGreenMaskSize(
                    framebuffer->greenMaskSize);
                bootInfo->getFramebuffer().setBluePosition(
                    framebuffer->bluePosition);
                bootInfo->getFramebuffer().setBlueMaskSize(
                    framebuffer->blueMaskSize);
            }
        } else if (tag->type == multibootTagElfSections) {
            bootInfo->setElfSections(reinterpret_cast<const void*>(
                reinterpret_cast<uint64_t>(tag) + directMapBase));
        } else if (tag->type == multibootTagAcpiOld ||
                   tag->type == multibootTagAcpiNew) {
            bootInfo->setAcpiRoot(reinterpret_cast<const void*>(
                reinterpret_cast<uint64_t>(tag) + 8 + directMapBase));
        }
        cursor += (tag->size + 7) & ~7U;
    }

    if (bootInfo->getMemoryMapCount() == 0) {
        kernel::device::serialWrite("[BOOT] memory map tag missing\n");
        return false;
    }
    return true;
}
