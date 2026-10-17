

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

class alignas(16) Block {
    friend void* kernel::heap::kernelAllocate(size_t);
    friend void kernel::heap::kernelFree(void*);
   private:
    uint64_t size;
    Block* previous;
    Block* next;
    bool free;
};
Block* firstBlock;
Block* lastBlock;

uint64_t alignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

namespace kernel::heap {

void init() {
    heapCurrent   = heapBase;
    heapMappedEnd = heapBase;
    firstBlock = lastBlock = nullptr;
    serial::serialWrite("[MEMORY] kernel heap initialized\n");
}

void* kernelAllocate(size_t size) {
    if (size == 0 || size > heapSize - sizeof(Block)) {
        return nullptr;
    }
    InterruptSpinLockGuard guard(heapLock);
    size = alignUp(size, 16);
    for (Block* block = firstBlock; block != nullptr; block = block->next) {
        if (!block->free || block->size < size) continue;
        if (block->size >= size + sizeof(Block) + 16) {
            Block* remainder = reinterpret_cast<Block*>(
                reinterpret_cast<uint8_t*>(block + 1) + size);
            remainder->size = block->size - size - sizeof(Block);
            remainder->free = true;
            remainder->previous = block;
            remainder->next = block->next;
            if (remainder->next) remainder->next->previous = remainder;
            else lastBlock = remainder;
            block->next = remainder;
            block->size = size;
        }
        block->free = false;
        return block + 1;
    }
    const uint64_t allocationStart = heapCurrent;
    const uint64_t allocationEnd   = allocationStart + sizeof(Block) + size;
    if (allocationEnd < allocationStart ||
        allocationEnd > heapBase + heapSize) {
        return nullptr;
    }

    while (heapMappedEnd < allocationEnd) {
        const uint64_t physicalAddress = memory::allocatePhysicalPage();
        if (physicalAddress == 0 ||
            !memory::mapPage(heapMappedEnd, physicalAddress,
                             pageWritable | pageNoExecute)) {
            if (physicalAddress != 0) {
                memory::freePhysicalPage(physicalAddress);
            }
            return nullptr;
        }
        heapMappedEnd += pageSize;
    }
    auto* block = reinterpret_cast<Block*>(allocationStart);
    block->size = size;
    block->free = false;
    block->previous = lastBlock;
    block->next = nullptr;
    if (lastBlock) lastBlock->next = block;
    else firstBlock = block;
    lastBlock = block;
    heapCurrent = allocationEnd;
    return block + 1;
}

void kernelFree(void* address) {
    if (!address) return;
    InterruptSpinLockGuard guard(heapLock);
    auto* block = static_cast<Block*>(address) - 1;
    if (reinterpret_cast<uint64_t>(block) < heapBase ||
        reinterpret_cast<uint64_t>(block) >= heapCurrent || block->free) return;
    block->free = true;
    if (block->previous && block->previous->free) {
        Block* previous = block->previous;
        previous->size += sizeof(Block) + block->size;
        previous->next = block->next;
        if (block->next) block->next->previous = previous;
        else lastBlock = previous;
        block = previous;
    }
    if (block->next && block->next->free) {
        Block* next = block->next;
        block->size += sizeof(Block) + next->size;
        block->next = next->next;
        if (block->next) block->next->previous = block;
        else lastBlock = block;
    }
}

}  // namespace kernel::heap
