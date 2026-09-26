#include "kernel/heap.h"

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"

namespace {

constexpr uint64_t pageSize = 4096;
constexpr uint64_t heapBase = 0xffff804000000000ULL;
constexpr uint64_t heapSize = 16ULL * 1024 * 1024;

uint64_t heapCurrent;
uint64_t heapMappedEnd;
kernel::SpinLock heapLock;

uint64_t alignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

namespace kernel {

void initKernelHeap() {
    heapCurrent   = heapBase;
    heapMappedEnd = heapBase;
    serialWrite("[MEMORY] kernel bump heap initialized\n");
}

void* kernelAllocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }
    InterruptSpinLockGuard guard(heapLock);
    const uint64_t allocationStart = alignUp(heapCurrent, 16);
    const uint64_t allocationEnd   = allocationStart + size;
    if (allocationEnd < allocationStart ||
        allocationEnd > heapBase + heapSize) {
        return nullptr;
    }

    while (heapMappedEnd < allocationEnd) {
        const uint64_t physicalAddress = allocatePhysicalPage();
        if (physicalAddress == 0 || !mapPage(heapMappedEnd, physicalAddress,
                                             pageWritable | pageNoExecute)) {
            if (physicalAddress != 0) {
                freePhysicalPage(physicalAddress);
            }
            return nullptr;
        }
        heapMappedEnd += pageSize;
    }
    heapCurrent = allocationEnd;
    return reinterpret_cast<void*>(allocationStart);
}

void kernelFree(void*) {
    // The first-stage bump allocator releases all memory only when the heap is
    // reinitialized.
}

}  // namespace kernel
