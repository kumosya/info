#include "kernel/mm.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"

namespace {

constexpr uint64_t pageSize           = 4096;
constexpr uint64_t maxPhysicalAddress = 64ULL * 1024 * 1024 * 1024;
constexpr uint64_t maxPageCount       = maxPhysicalAddress / pageSize;
constexpr uint64_t bitmapByteCount    = maxPageCount / 8;

uint8_t pageBitmap[bitmapByteCount];
uint8_t reservedBitmap[bitmapByteCount];
uint64_t highestAddress;
uint64_t freePageCount;
kernel::SpinLock physicalMemoryLock;

inline bool isMarked(uint64_t page) {
    return (pageBitmap[page >> 3] & (1U << (page & 7))) != 0;
}

inline void setMarked(uint64_t page) {
    pageBitmap[page >> 3] |= static_cast<uint8_t>(1U << (page & 7));
}

inline void clearMarked(uint64_t page) {
    pageBitmap[page >> 3] &= static_cast<uint8_t>(~(1U << (page & 7)));
}

inline bool isReserved(uint64_t page) {
    return (reservedBitmap[page >> 3] & (1U << (page & 7))) != 0;
}

inline void setReserved(uint64_t page) {
    reservedBitmap[page >> 3] |= static_cast<uint8_t>(1U << (page & 7));
}

inline void clearReserved(uint64_t page) {
    reservedBitmap[page >> 3] &= static_cast<uint8_t>(~(1U << (page & 7)));
}

void markRange(uint64_t base, uint64_t length, bool used) {
    if (length == 0 || base >= maxPhysicalAddress) {
        return;
    }
    uint64_t end = base + length;
    if (end < base || end > maxPhysicalAddress) {
        end = maxPhysicalAddress;
    }
    const uint64_t firstPage = (base + pageSize - 1) / pageSize;
    const uint64_t lastPage  = end / pageSize;
    for (uint64_t page = firstPage; page < lastPage; ++page) {
        if (used) {
            setReserved(page);
            if (!isMarked(page)) {
                setMarked(page);
                if (freePageCount != 0) {
                    --freePageCount;
                }
            }
        } else if (isMarked(page)) {
            clearMarked(page);
            clearReserved(page);
            ++freePageCount;
        } else {
            clearReserved(page);
        }
    }
}

void reserveRange(uint64_t base, uint64_t length) {
    if (length == 0 || base >= maxPhysicalAddress) {
        return;
    }
    const uint64_t end         = base + length;
    const uint64_t alignedBase = base & ~(pageSize - 1);
    const uint64_t alignedEnd  = (end < base || end > maxPhysicalAddress)
                                     ? maxPhysicalAddress
                                     : (end + pageSize - 1) & ~(pageSize - 1);
    markRange(alignedBase, alignedEnd - alignedBase, true);
}

}  // namespace

namespace kernel::memory {

void initPhy(const BootInfo* bootInfo) {
    for (uint64_t index = 0; index < bitmapByteCount; ++index) {
        pageBitmap[index]     = 0xff;
        reservedBitmap[index] = 0xff;
    }
    freePageCount  = 0;
    highestAddress = 0;

    for (uint32_t index = 0; index < bootInfo->memoryMapCount; ++index) {
        const MemoryRegion& region = bootInfo->memoryMap[index];
        if (region.type == memoryAvailable) {
            markRange(region.base, region.length, false);
            const uint64_t end = region.base + region.length;
            if (end > highestAddress && end < maxPhysicalAddress) {
                highestAddress = end;
            }
        }
    }

    if (bootInfo->bootstrapEnd > bootInfo->bootstrapStart) {
        reserveRange(bootInfo->bootstrapStart,
                     bootInfo->bootstrapEnd - bootInfo->bootstrapStart);
    }
    reserveRange(bootInfo->multibootAddress, bootInfo->multibootSize);
    for (uint32_t index = 0; index < bootInfo->moduleCount; ++index) {
        const BootModule& module = bootInfo->modules[index];
        if (module.end > module.start) {
            reserveRange(module.start, module.end - module.start);
        }
    }
    reserveRange(bootInfo->framebuffer.address,
                 static_cast<uint64_t>(bootInfo->framebuffer.pitch) *
                     bootInfo->framebuffer.height);

    serial::serialWrite("[MEMORY] physical memory initialized, free pages=");
    serial::serialWriteDecimal(freePageCount);
    serial::serialWrite(" highest=");
    serial::serialWriteHex(highestAddress);
    serial::serialWrite("\n");
}

uint64_t allocatePhysicalPage() {
    InterruptSpinLockGuard guard(physicalMemoryLock);
    for (uint64_t page = 256; page < maxPageCount; ++page) {
        if (!isMarked(page)) {
            setMarked(page);
            if (freePageCount != 0) {
                --freePageCount;
            }
            return page * pageSize;
        }
    }
    return 0;
}

void freePhysicalPage(uint64_t physicalAddress) {
    if ((physicalAddress & (pageSize - 1)) != 0 ||
        physicalAddress >= maxPhysicalAddress) {
        return;
    }
    InterruptSpinLockGuard guard(physicalMemoryLock);
    const uint64_t page = physicalAddress / pageSize;
    if (isMarked(page) && !isReserved(page)) {
        clearMarked(page);
        ++freePageCount;
    }
}

uint64_t getTotalMemoryBytes() { return highestAddress; }

uint64_t getFreeMemoryBytes() {
    InterruptSpinLockGuard guard(physicalMemoryLock);
    return freePageCount * pageSize;
}

}  // namespace kernel::memory
