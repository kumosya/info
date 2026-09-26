#include "kernel/mm.h"
#include "kernel/boot.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/spinlock.h"

namespace {

constexpr uint64_t pageMask      = 0x000ffffffffff000ULL;
constexpr uint64_t directMapBase = 0xffff800000000000ULL;

struct PageTable {
    uint64_t entries[512];
};

uint64_t kernelAddressSpaceRoot;
kernel::SpinLock virtualMemoryLock;

PageTable* getTable(uint64_t address) {
    return reinterpret_cast<PageTable*>(directMapBase + (address & pageMask));
}

uint64_t getTablePhysicalAddress(const PageTable* table) {
    return reinterpret_cast<uint64_t>(table) - directMapBase;
}

PageTable* allocateTable() {
    const uint64_t physicalAddress = kernel::allocatePhysicalPage();
    if (physicalAddress == 0) {
        return nullptr;
    }
    PageTable* table = getTable(physicalAddress);
    for (uint64_t& entry : table->entries) {
        entry = 0;
    }
    return table;
}

PageTable* getOrCreateTable(PageTable* parent, uint64_t index, uint64_t flags) {
    uint64_t& entry = parent->entries[index];
    if ((entry & kernel::pagePresent) == 0) {
        PageTable* table = allocateTable();
        if (table == nullptr) {
            return nullptr;
        }
        entry = getTablePhysicalAddress(table) | kernel::pagePresent |
                kernel::pageWritable;
    }
    if ((flags & kernel::pageUser) != 0) {
        entry |= kernel::pageUser;
    }
    return getTable(entry);
}

PageTable* splitLargePage(PageTable* directory, uint64_t index) {
    const uint64_t largeEntry = directory->entries[index];
    PageTable* table          = allocateTable();
    if (table == nullptr) {
        return nullptr;
    }
    constexpr uint64_t preservedFlags = 0x800000000000011fULL;
    const uint64_t physicalBase       = largeEntry & 0x000fffffffe00000ULL;
    const uint64_t flags = largeEntry & preservedFlags & ~(1ULL << 7);
    for (uint64_t page = 0; page < 512; ++page) {
        table->entries[page] = physicalBase + page * 4096 + flags;
    }
    directory->entries[index] = getTablePhysicalAddress(table) |
                                kernel::pagePresent | kernel::pageWritable |
                                (largeEntry & kernel::pageUser);
    return table;
}

void releaseUserPageTables(uint64_t tablePhysicalAddress, uint32_t level) {
    PageTable* table = getTable(tablePhysicalAddress);
    for (uint32_t index = 0; index < 512; ++index) {
        const uint64_t entry = table->entries[index];
        if ((entry & kernel::pagePresent) == 0) {
            continue;
        }
        if (level > 1 && (entry & (1ULL << 7)) == 0) {
            releaseUserPageTables(entry & pageMask, level - 1);
        } else if ((entry & kernel::pageExternal) == 0) {
            kernel::freePhysicalPage(entry & pageMask);
        }
    }
    kernel::freePhysicalPage(tablePhysicalAddress);
}

bool mapPageLocked(uint64_t rootPhysicalAddress, uint64_t virtualAddress,
                   uint64_t physicalAddress, uint64_t flags) {
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
    const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
    const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;
    PageTable* pml4          = getTable(rootPhysicalAddress);
    PageTable* pdpt          = getOrCreateTable(pml4, pml4Index, flags);
    if (pdpt == nullptr || (pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) {
        return false;
    }
    PageTable* pd = getOrCreateTable(pdpt, pdptIndex, flags);
    if (pd == nullptr) {
        return false;
    }
    PageTable* pt;
    if ((pd->entries[pdIndex] & kernel::pagePresent) != 0 &&
        (pd->entries[pdIndex] & (1ULL << 7)) != 0) {
        pt = splitLargePage(pd, pdIndex);
    } else {
        pt = getOrCreateTable(pd, pdIndex, flags);
    }
    if (pt == nullptr || (pt->entries[ptIndex] & kernel::pagePresent) != 0) {
        return false;
    }
    pt->entries[ptIndex] =
        (physicalAddress & pageMask) | flags | kernel::pagePresent;
    return true;
}

bool unmapPageLocked(uint64_t rootPhysicalAddress, uint64_t virtualAddress) {
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
    const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
    const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;
    PageTable* pml4          = getTable(rootPhysicalAddress);
    if ((pml4->entries[pml4Index] & kernel::pagePresent) == 0) {
        return false;
    }
    PageTable* pdpt = getTable(pml4->entries[pml4Index]);
    if ((pdpt->entries[pdptIndex] & kernel::pagePresent) == 0 ||
        (pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) {
        return false;
    }
    PageTable* pd = getTable(pdpt->entries[pdptIndex]);
    if ((pd->entries[pdIndex] & kernel::pagePresent) == 0 ||
        (pd->entries[pdIndex] & (1ULL << 7)) != 0) {
        return false;
    }
    PageTable* pt = getTable(pd->entries[pdIndex]);
    if ((pt->entries[ptIndex] & kernel::pagePresent) == 0) {
        return false;
    }
    pt->entries[ptIndex] = 0;
    return true;
}

bool protectPageLocked(uint64_t rootPhysicalAddress, uint64_t virtualAddress,
                       uint64_t flags) {
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
    const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
    const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;
    PageTable* pml4 = getTable(rootPhysicalAddress);
    if ((pml4->entries[pml4Index] & kernel::pagePresent) == 0) return false;
    PageTable* pdpt = getTable(pml4->entries[pml4Index]);
    if ((pdpt->entries[pdptIndex] & kernel::pagePresent) == 0 ||
        (pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) return false;
    PageTable* pd = getTable(pdpt->entries[pdptIndex]);
    if ((pd->entries[pdIndex] & kernel::pagePresent) == 0 ||
        (pd->entries[pdIndex] & (1ULL << 7)) != 0) return false;
    PageTable* pt = getTable(pd->entries[pdIndex]);
    uint64_t& entry = pt->entries[ptIndex];
    if ((entry & kernel::pagePresent) == 0) return false;
    const uint64_t preserved = entry & (pageMask | kernel::pageExternal);
    entry = preserved | flags | kernel::pagePresent;
    return true;
}

uint64_t translateLocked(uint64_t rootPhysicalAddress, uint64_t virtualAddress,
                         uint64_t* flags) {
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
    const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
    const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;
    PageTable* pml4          = getTable(rootPhysicalAddress);
    if ((pml4->entries[pml4Index] & kernel::pagePresent) == 0) {
        return 0;
    }
    PageTable* pdpt = getTable(pml4->entries[pml4Index]);
    if ((pdpt->entries[pdptIndex] & kernel::pagePresent) == 0) {
        return 0;
    }
    if ((pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) {
        if (flags != nullptr) {
            *flags = pdpt->entries[pdptIndex];
        }
        return (pdpt->entries[pdptIndex] & 0x000fffffc0000000ULL) |
               (virtualAddress & 0x3fffffff);
    }
    PageTable* pd = getTable(pdpt->entries[pdptIndex]);
    if ((pd->entries[pdIndex] & kernel::pagePresent) == 0) {
        return 0;
    }
    if ((pd->entries[pdIndex] & (1ULL << 7)) != 0) {
        if (flags != nullptr) {
            *flags = pd->entries[pdIndex];
        }
        return (pd->entries[pdIndex] & 0x000fffffffe00000ULL) |
               (virtualAddress & 0x1fffff);
    }
    PageTable* pt = getTable(pd->entries[pdIndex]);
    if ((pt->entries[ptIndex] & kernel::pagePresent) == 0) {
        return 0;
    }
    if (flags != nullptr) {
        *flags = pt->entries[ptIndex];
    }
    return (pt->entries[ptIndex] & pageMask) | (virtualAddress & 0xfff);
}

}  // namespace

namespace kernel {

void initVirtualMemory(const BootInfo* bootInfo) {
    if (bootInfo == nullptr || bootInfo->pageTableRoot == 0) {
        serialWrite("[MEMORY] missing bootstrap page table root\n");
        while (true) {
            asm volatile("cli; hlt");
        }
    }
    kernelAddressSpaceRoot = bootInfo->pageTableRoot;
    PageTable* pml4 = getTable(kernelAddressSpaceRoot);
    pml4->entries[0] = 0;
    asm volatile("mov %0, %%cr3" : : "r"(kernelAddressSpaceRoot) : "memory");
    serialWrite(
        "[MEMORY] higher-half direct map active; identity map removed\n");
}

bool mapPage(uint64_t virtualAddress, uint64_t physicalAddress,
             uint64_t flags) {
    return mapPageInAddressSpace(getKernelAddressSpaceRoot(), virtualAddress,
                                 physicalAddress, flags);
}

bool mapPageInAddressSpace(uint64_t rootPhysicalAddress,
                           uint64_t virtualAddress, uint64_t physicalAddress,
                           uint64_t flags) {
    if ((virtualAddress & 0xfff) != 0 || (physicalAddress & 0xfff) != 0) {
        return false;
    }
    bool mapped;
    {
        InterruptSpinLockGuard guard(virtualMemoryLock);
        mapped = mapPageLocked(rootPhysicalAddress, virtualAddress,
                               physicalAddress, flags);
        if (mapped) {
            asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
        }
    }
    return mapped;
}

bool unmapPage(uint64_t virtualAddress) {
    return unmapPageInAddressSpace(getKernelAddressSpaceRoot(), virtualAddress);
}

bool unmapPageInAddressSpace(uint64_t rootPhysicalAddress,
                             uint64_t virtualAddress) {
    if ((virtualAddress & 0xfff) != 0) {
        return false;
    }
    bool unmapped;
    {
        InterruptSpinLockGuard guard(virtualMemoryLock);
        unmapped = unmapPageLocked(rootPhysicalAddress, virtualAddress);
        if (unmapped) {
            asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
        }
    }
    if (unmapped) {
        requestTlbShootdown(virtualAddress);
    }
    return unmapped;
}

bool protectPageInAddressSpace(uint64_t rootPhysicalAddress,
                               uint64_t virtualAddress, uint64_t flags) {
    if ((virtualAddress & 0xfff) != 0 || (flags & pageUser) == 0) return false;
    bool protectedPage;
    {
        InterruptSpinLockGuard guard(virtualMemoryLock);
        protectedPage = protectPageLocked(rootPhysicalAddress, virtualAddress,
                                          flags);
        if (protectedPage) {
            asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
        }
    }
    if (protectedPage) requestTlbShootdown(virtualAddress);
    return protectedPage;
}

uint64_t getPhysicalAddress(uint64_t virtualAddress) {
    return getPhysicalAddressInAddressSpace(getKernelAddressSpaceRoot(),
                                            virtualAddress);
}

uint64_t getPhysicalAddressInAddressSpace(uint64_t rootPhysicalAddress,
                                          uint64_t virtualAddress,
                                          uint64_t* flags) {
    InterruptSpinLockGuard guard(virtualMemoryLock);
    return translateLocked(rootPhysicalAddress, virtualAddress, flags);
}

bool validateUserMemory(uint64_t rootPhysicalAddress, uint64_t address,
                        uint64_t size, bool writable) {
    if (size == 0) {
        return true;
    }
    const uint64_t end = address + size - 1;
    if (end < address || end >= 0x0000800000000000ULL) {
        return false;
    }
    InterruptSpinLockGuard guard(virtualMemoryLock);
    uint64_t page           = address & ~0xfffULL;
    const uint64_t lastPage = end & ~0xfffULL;
    while (true) {
        uint64_t flags = 0;
        if (translateLocked(rootPhysicalAddress, page, &flags) == 0 ||
            (flags & pageUser) == 0 ||
            (writable && (flags & pageWritable) == 0)) {
            return false;
        }
        if (page == lastPage) {
            return true;
        }
        page += 4096;
    }
}

UserPageMapResult allocateUserPageInAddressSpace(uint64_t rootPhysicalAddress,
                                                 uint64_t virtualAddress,
                                                 uint64_t flags) {
    constexpr uint64_t pageSize       = 4096;
    constexpr uint64_t userAddressEnd = 0x0000800000000000ULL;
    if (rootPhysicalAddress == 0 || (virtualAddress & (pageSize - 1)) != 0 ||
        virtualAddress < pageSize || virtualAddress >= userAddressEnd ||
        (flags & pageUser) == 0 ||
        ((flags & pageWritable) != 0 && (flags & pageNoExecute) == 0)) {
        return UserPageMapResult::InvalidArgument;
    }

    const uint64_t physicalAddress = allocatePhysicalPage();
    if (physicalAddress == 0) {
        return UserPageMapResult::OutOfMemory;
    }
    uint8_t* page = reinterpret_cast<uint8_t*>(directMapBase + physicalAddress);
    for (uint64_t index = 0; index < pageSize; ++index) {
        page[index] = 0;
    }
    if (!mapPageInAddressSpace(rootPhysicalAddress, virtualAddress,
                               physicalAddress, flags)) {
        freePhysicalPage(physicalAddress);
        return UserPageMapResult::MappingFailed;
    }
    return UserPageMapResult::Success;
}

bool copyToAddressSpace(uint64_t rootPhysicalAddress, uint64_t destination,
                        const void* source, uint64_t size) {
    constexpr uint64_t pageSize       = 4096;
    constexpr uint64_t userAddressEnd = 0x0000800000000000ULL;
    if (source == nullptr || size == 0 || destination < pageSize ||
        destination >= userAddressEnd || size > userAddressEnd - destination) {
        return false;
    }

    InterruptSpinLockGuard guard(virtualMemoryLock);
    const uint8_t* input = static_cast<const uint8_t*>(source);
    while (size != 0) {
        uint64_t flags = 0;
        const uint64_t physical =
            translateLocked(rootPhysicalAddress, destination, &flags);
        if (physical == 0 || (flags & pageUser) == 0) {
            return false;
        }
        const uint64_t pageRemaining =
            pageSize - (destination & (pageSize - 1));
        const uint64_t chunk = size < pageRemaining ? size : pageRemaining;
        uint8_t* output = reinterpret_cast<uint8_t*>(directMapBase + physical);
        for (uint64_t index = 0; index < chunk; ++index) {
            output[index] = input[index];
        }
        destination += chunk;
        input += chunk;
        size -= chunk;
    }
    return true;
}

bool copyFromAddressSpace(void* destination, uint64_t rootPhysicalAddress,
                          uint64_t source, uint64_t size) {
    constexpr uint64_t pageSize = 4096;
    constexpr uint64_t userAddressEnd = 0x0000800000000000ULL;
    if (destination == nullptr || size == 0 || source < pageSize ||
        source >= userAddressEnd || size > userAddressEnd - source) {
        return false;
    }

    InterruptSpinLockGuard guard(virtualMemoryLock);
    uint8_t* output = static_cast<uint8_t*>(destination);
    while (size != 0) {
        uint64_t flags = 0;
        const uint64_t physical =
            translateLocked(rootPhysicalAddress, source, &flags);
        if (physical == 0 || (flags & pageUser) == 0) {
            return false;
        }
        const uint64_t pageRemaining = pageSize - (source & (pageSize - 1));
        const uint64_t chunk = size < pageRemaining ? size : pageRemaining;
        const uint8_t* input =
            reinterpret_cast<const uint8_t*>(directMapBase + physical);
        for (uint64_t index = 0; index < chunk; ++index) {
            output[index] = input[index];
        }
        source += chunk;
        output += chunk;
        size -= chunk;
    }
    return true;
}

uint64_t getKernelAddressSpaceRoot() {
    return kernelAddressSpaceRoot;
}

uint64_t createAddressSpaceRoot() {
    InterruptSpinLockGuard guard(virtualMemoryLock);
    const uint64_t rootPhysicalAddress = allocatePhysicalPage();
    if (rootPhysicalAddress == 0) {
        return 0;
    }
    PageTable* root       = getTable(rootPhysicalAddress);
    PageTable* kernelRoot = getTable(getKernelAddressSpaceRoot());
    for (uint32_t index = 0; index < 256; ++index) {
        root->entries[index] = 0;
    }
    for (uint32_t index = 256; index < 512; ++index) {
        root->entries[index] = kernelRoot->entries[index];
    }
    return rootPhysicalAddress;
}

bool cloneAddressSpaceRoot(uint64_t sourceRootPhysicalAddress,
                           uint64_t destinationRootPhysicalAddress) {
    if (sourceRootPhysicalAddress == 0 || destinationRootPhysicalAddress == 0 ||
        sourceRootPhysicalAddress == destinationRootPhysicalAddress) {
        return false;
    }

    constexpr uint64_t largePage = 1ULL << 7;
    constexpr uint64_t preservedFlags =
        pageWritable | pageUser | pageWriteThrough | pageCacheDisable |
        pageGlobal | pageExternal | pageNoExecute;
    InterruptSpinLockGuard guard(virtualMemoryLock);
    PageTable* sourcePml4 = getTable(sourceRootPhysicalAddress);
    for (uint32_t pml4Index = 0; pml4Index < 256; ++pml4Index) {
        const uint64_t pml4Entry = sourcePml4->entries[pml4Index];
        if ((pml4Entry & pagePresent) == 0) continue;
        PageTable* sourcePdpt = getTable(pml4Entry);
        for (uint32_t pdptIndex = 0; pdptIndex < 512; ++pdptIndex) {
            const uint64_t pdptEntry = sourcePdpt->entries[pdptIndex];
            if ((pdptEntry & pagePresent) == 0) continue;
            if ((pdptEntry & largePage) != 0) return false;
            PageTable* sourcePd = getTable(pdptEntry);
            for (uint32_t pdIndex = 0; pdIndex < 512; ++pdIndex) {
                const uint64_t pdEntry = sourcePd->entries[pdIndex];
                if ((pdEntry & pagePresent) == 0) continue;
                if ((pdEntry & largePage) != 0) return false;
                PageTable* sourcePt = getTable(pdEntry);
                for (uint32_t ptIndex = 0; ptIndex < 512; ++ptIndex) {
                    const uint64_t entry = sourcePt->entries[ptIndex];
                    if ((entry & pagePresent) == 0) continue;
                    const uint64_t virtualAddress =
                        (static_cast<uint64_t>(pml4Index) << 39) |
                        (static_cast<uint64_t>(pdptIndex) << 30) |
                        (static_cast<uint64_t>(pdIndex) << 21) |
                        (static_cast<uint64_t>(ptIndex) << 12);
                    const uint64_t sourcePhysical = entry & pageMask;
                    uint64_t destinationPhysical = sourcePhysical;
                    if ((entry & pageExternal) == 0) {
                        destinationPhysical = allocatePhysicalPage();
                        if (destinationPhysical == 0) return false;
                        const uint8_t* source = reinterpret_cast<const uint8_t*>(
                            directMapBase + sourcePhysical);
                        uint8_t* destination = reinterpret_cast<uint8_t*>(
                            directMapBase + destinationPhysical);
                        for (uint32_t byte = 0; byte < 4096; ++byte) {
                            destination[byte] = source[byte];
                        }
                    }
                    if (!mapPageLocked(destinationRootPhysicalAddress,
                                       virtualAddress, destinationPhysical,
                                       entry & preservedFlags)) {
                        if ((entry & pageExternal) == 0) {
                            freePhysicalPage(destinationPhysical);
                        }
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

void destroyAddressSpaceRoot(uint64_t rootPhysicalAddress) {
    if (rootPhysicalAddress == 0 ||
        rootPhysicalAddress == getKernelAddressSpaceRoot()) {
        return;
    }
    InterruptSpinLockGuard guard(virtualMemoryLock);
    PageTable* root = getTable(rootPhysicalAddress);
    for (uint32_t index = 0; index < 256; ++index) {
        const uint64_t entry = root->entries[index];
        if ((entry & pagePresent) != 0) {
            releaseUserPageTables(entry & pageMask, 3);
        }
    }
    freePhysicalPage(rootPhysicalAddress);
}

void switchAddressSpace(uint64_t rootPhysicalAddress) {
    if (rootPhysicalAddress == 0) {
        rootPhysicalAddress = getKernelAddressSpaceRoot();
    }
    uint64_t currentRoot;
    asm volatile("movq %%cr3, %0" : "=r"(currentRoot));
    if ((currentRoot & pageMask) != (rootPhysicalAddress & pageMask)) {
        asm volatile("movq %0, %%cr3" : : "r"(rootPhysicalAddress) : "memory");
    }
}

}  // namespace kernel
