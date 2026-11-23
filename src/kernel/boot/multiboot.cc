/*
 * 解析引导信息，收集内存、模块、显存与 ACPI 入口。
 */

#include "kernel/boot.h"
#include "kernel/device.h"

/* 引导标签公共头，size 包括头本身，但不包括末尾对齐填充。 */
struct MultibootTag {
        uint32_t type;
        uint32_t size;
};

/* 内存地图标签头，声明后续条目的实际步长。 */
struct MultibootMemoryMapTag {
        uint32_t type;
        uint32_t size;
        uint32_t entrySize;
        uint32_t entryVersion;
};

/* 一段物理内存的起点、长度及可用或保留类型。 */
struct MultibootMemoryMapEntry {
        uint64_t base;
        uint64_t length;
        uint32_t type;
        uint32_t reserved;
};

/* 引导器给出的显存地址、屏幕尺寸、行跨度和颜色布局。 */
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
static ::MemoryRegion memoryRegions[maxMemoryRegions];
static ::BootModule bootModules[maxModules];

/*
 * 取得引导标签头后面的字符串，并转换为内核高地址直接映射指针；这里只换算地址，不复制字符串。
 */
static const char* stringAfterHeader(const MultibootTag* tag) {
    return reinterpret_cast<const char*>(reinterpret_cast<uint64_t>(tag) + 8 +
                                         directMapBase);
}

/*
 * 把 Multiboot2 标签解析成内核统一的 BootInfo。
 * 逐项检查长度和容量，收集内存、模块、显示和 ACPI 信息，最终由主启动流程使用。
 */
bool BootInfo::parseMultibootInfo(uint64_t address) {
    // device::serialWrite("[BOOT] Multiboot2 address=");
    // device::serialWriteHex(address);
    device::serialWrite("\n");

    if (address == 0) {
        return false;
    }

    // 先把输出恢复为明确的空状态；下面只发布实际找到的条目，数组容量固定，不能无限追加。
    setMemoryMap(memoryRegions);
    setMemoryMapCount(0);
    setModules(bootModules);
    setModuleCount(0);
    setCommandLine(nullptr);
    setBootLoaderName(nullptr);
    setAcpiRoot(nullptr);
    setElfSections(nullptr);
    setFramebuffer({});
    setMultibootAddress(address);
    setMultibootSize(0);

    // 总长度来自引导器，先限制到合理范围，再用它约束每个标签的读取。
    const uint32_t* header   = reinterpret_cast<const uint32_t*>(address);
    const uint32_t totalSize = header[0];

    /*device::serialWrite("[BOOT] Multiboot2 size=");
    device::serialWriteDecimal(totalSize);
    device::serialWrite("\n");*/

    if (totalSize < 16 || totalSize > (16 * 1024 * 1024)) {
        return false;
    }

    setMultibootSize(totalSize);

    const uint8_t* cursor = reinterpret_cast<const uint8_t*>(address) + 8;
    const uint8_t* end = reinterpret_cast<const uint8_t*>(address) + totalSize;

    // 每个标签都带自己的长度；长度不足或越过整块信息末尾时立即失败，避免按坏数据继续前进。
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
            setCommandLine(stringAfterHeader(tag));
        } else if (tag->type == multibootTagBootLoaderName) {
            setBootLoaderName(stringAfterHeader(tag));
        } else if (tag->type == multibootTagMemoryMap &&
                   tag->size >= sizeof(MultibootMemoryMapTag)) {
            // 内存条目步长由引导器指定，可能大于当前结构；按该步长遍历，同时限制内核数组容量。
            const MultibootMemoryMapTag* mapTag =
                reinterpret_cast<const MultibootMemoryMapTag*>(tag);

            if (mapTag->entrySize < sizeof(MultibootMemoryMapEntry)) {
                return false;
            }

            const uint8_t* entryCursor = cursor + sizeof(MultibootMemoryMapTag);
            const uint8_t* entryEnd    = cursor + tag->size;

            while (entryCursor + mapTag->entrySize <= entryEnd &&
                   getMemoryMapCount() < maxMemoryRegions) {
                const MultibootMemoryMapEntry* entry =
                    reinterpret_cast<const MultibootMemoryMapEntry*>(
                        entryCursor);
                MemoryRegion& region = memoryRegions[getMemoryMapCount()];
                setMemoryMapCount(getMemoryMapCount() + 1);
                region.setBase(entry->base);
                region.setLength(entry->length);
                region.setType(entry->type);
                entryCursor += mapTag->entrySize;
            }
        } else if (tag->type == multibootTagModule && tag->size >= 16 &&
                   getModuleCount() < maxModules) {
            // 模块正文给出物理起止地址；命令行保留在引导信息中，通过高地址直接映射访问。
            const uint32_t* module = reinterpret_cast<const uint32_t*>(tag);
            BootModule& bootModule = bootModules[getModuleCount()];
            setModuleCount(getModuleCount() + 1);
            bootModule.setStart(module[2]);
            bootModule.setEnd(module[3]);
            bootModule.setCommandLine(reinterpret_cast<const char*>(
                reinterpret_cast<uint64_t>(module + 4) + directMapBase));
        } else if (tag->type == multibootTagFramebuffer &&
                   tag->size >= sizeof(MultibootFramebufferTag)) {
            // 保存显存布局供显示服务使用；RGB
            // 类型还需要各颜色分量的位置和位数。
            const MultibootFramebufferTag* framebuffer =
                reinterpret_cast<const MultibootFramebufferTag*>(tag);
            getFramebuffer().setAddress(framebuffer->address);
            getFramebuffer().setPitch(framebuffer->pitch);
            getFramebuffer().setWidth(framebuffer->width);
            getFramebuffer().setHeight(framebuffer->height);
            getFramebuffer().setBitsPerPixel(framebuffer->bitsPerPixel);
            getFramebuffer().setType(framebuffer->framebufferType);

            if (framebuffer->framebufferType == 1 &&
                tag->size >= sizeof(MultibootFramebufferTag)) {
                getFramebuffer().setRedPosition(framebuffer->redPosition);
                getFramebuffer().setRedMaskSize(framebuffer->redMaskSize);
                getFramebuffer().setGreenPosition(framebuffer->greenPosition);
                getFramebuffer().setGreenMaskSize(framebuffer->greenMaskSize);
                getFramebuffer().setBluePosition(framebuffer->bluePosition);
                getFramebuffer().setBlueMaskSize(framebuffer->blueMaskSize);
            }
        } else if (tag->type == multibootTagElfSections) {
            setElfSections(reinterpret_cast<const void*>(
                reinterpret_cast<uint64_t>(tag) + directMapBase));
        } else if (tag->type == multibootTagAcpiOld ||
                   tag->type == multibootTagAcpiNew) {
            setAcpiRoot(reinterpret_cast<const void*>(
                reinterpret_cast<uint64_t>(tag) + 8 + directMapBase));
        }

        // 标签实际内容未必占满八字节边界，跳过末尾填充后才是下一个标签。
        cursor += (tag->size + 7) & ~7U;
    }

    // 没有内存地图就无法可靠区分可分配内存与保留区域，因此不能继续启动。
    if (getMemoryMapCount() == 0) {
        device::serialWrite("[BOOT] memory map tag missing\n");

        return false;
    }

    return true;
}
