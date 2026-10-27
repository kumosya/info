/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/virtual/virtual.cc
 * x86-64 page tables, address-space lifetime, and cross-root copying.
 */

#include "kernel/boot.h"
#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/task.h"

namespace kernel::mm {

namespace {

constexpr uint64_t pageMask      = 0x000ffffffffff000ULL;
constexpr uint64_t directMapBase = 0xffff800000000000ULL;

// Hardware layout: one 4096-byte page of 512 entries. C++ pointers are
// direct-map aliases; hardware entries contain physical addresses plus flags.
struct PageTable {
    uint64_t entries[512];
};

// Iterate resident user pages at or beyond cursor without faulting in data.
// Skip high-half, absent, huge and device-cache branches; core policy later
// decides whether an external RAM page is safe to copy.
bool findDumpPage(uint64_t tableAddress, unsigned level, uint64_t base,
                  uint64_t cursor, uint64_t& address, uint64_t& flags) {
    constexpr uint64_t physicalMask = 0x000ffffffffff000ULL;
    const auto* entries             = reinterpret_cast<const uint64_t*>(
        0xffff800000000000ULL + (tableAddress & physicalMask));
    const unsigned shift = 12 + 9 * (level - 1);
    const uint64_t span  = 1ULL << shift;
    for (unsigned index = 0; index < (level == 4 ? 256U : 512U); ++index) {
        const uint64_t start = base + index * span;
        if (start + span <= cursor) continue;
        const uint64_t entry = entries[index];
        if ((entry & (pagePresent | pageUser)) != (pagePresent | pageUser))
            continue;
        if (entry & (pageCacheDisable | pageWriteThrough)) continue;
        if (level == 1) {
            address = start;
            flags   = entry;
            return true;
        }
        // OSInfo never allocates ordinary user huge pages. Device aliases are
        // excluded.
        if (entry & (1ULL << 7)) continue;
        if (findDumpPage(entry, level - 1, start, cursor, address, flags))
            return true;
    }
    return false;
}

uint64_t kernelAddressSpaceRoot;
kernel::task::SpinLock virtualMemoryLock;

// Mask entry flags and convert the physical table address to its direct alias.
PageTable* getTable(uint64_t address) {
    return reinterpret_cast<PageTable*>(directMapBase + (address & pageMask));
}

// Invert a direct-map table pointer for storage in a hardware entry.
uint64_t getTablePhysicalAddress(const PageTable* table) {
    return reinterpret_cast<uint64_t>(table) - directMapBase;
}

// Zero new table pages before linking: random old bytes must never become
// hardware-visible present entries. Caller owns any later rollback.
PageTable* allocateTable() {
    const uint64_t physicalAddress = allocatePhysicalPage();
    if (physicalAddress == 0) {
        return nullptr;
    }
    PageTable* table = getTable(physicalAddress);
    for (uint64_t& entry : table->entries) {
        entry = 0;
    }
    return table;
}

// Caller holds virtualMemoryLock. Allocate missing levels and attribute them
// to the root owner. User permission must propagate through parent entries,
// not just the final leaf, for hardware to permit user access.
PageTable* getOrCreateTable(PageTable* parent, uint64_t index, uint64_t flags,
                            uint64_t owner = 0) {
    uint64_t& entry = parent->entries[index];
    if ((entry & pagePresent) == 0) {
        PageTable* table = allocateTable();
        if (table == nullptr) {
            return nullptr;
        }
        if (owner)
            assignPhysicalPageOwner(getTablePhysicalAddress(table), owner);
        entry = getTablePhysicalAddress(table) | pagePresent | pageWritable;
    }
    if ((flags & pageUser) != 0) {
        entry |= pageUser;
    }
    return getTable(entry);
}

// Prepare 512 4 KiB leaves replacing one 2 MiB mapping before publishing its
// new table pointer. Keep supported flags and the original physical sequence.
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
    directory->entries[index] = getTablePhysicalAddress(table) | pagePresent |
                                pageWritable | (largeEntry & pageUser);
    return table;
}

// Free private table pages and ordinary leaves recursively. External leaves
// are borrowed from object/device owners; their registries release backing, so
// freeing them in this walk would decrement ownership twice.
void releaseUserPageTables(uint64_t tablePhysicalAddress, uint32_t level) {
    PageTable* table = getTable(tablePhysicalAddress);
    for (uint32_t index = 0; index < 512; ++index) {
        const uint64_t entry = table->entries[index];
        if ((entry & pagePresent) == 0) {
            continue;
        }
        if (level > 1 && (entry & (1ULL << 7)) == 0) {
            releaseUserPageTables(entry & pageMask, level - 1);
        } else if ((entry & pageExternal) == 0) {
            freePhysicalPage(entry & pageMask);
        }
    }
    freePhysicalPage(tablePhysicalAddress);
}

// Under virtualMemoryLock, walk/create levels and reject occupied leaves. A 2
// MiB alias can split, a 1 GiB leaf cannot. Higher layers keep the root alive
// and own partial-setup cleanup; this is not a replacement operation.
bool mapPageLocked(uint64_t rootPhysicalAddress, uint64_t virtualAddress,
                   uint64_t physicalAddress, uint64_t flags) {
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
    const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
    const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;
    PhysicalPageInfo rootInfo{};
    queryPhysicalPage(rootPhysicalAddress, rootInfo);
    PageTable* pml4 = getTable(rootPhysicalAddress);
    PageTable* pdpt =
        getOrCreateTable(pml4, pml4Index, flags, rootInfo.getOwner());
    if (pdpt == nullptr || (pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) {
        return false;
    }
    PageTable* pd =
        getOrCreateTable(pdpt, pdptIndex, flags, rootInfo.getOwner());
    if (pd == nullptr) {
        return false;
    }
    PageTable* pt;
    if ((pd->entries[pdIndex] & pagePresent) != 0 &&
        (pd->entries[pdIndex] & (1ULL << 7)) != 0) {
        pt = splitLargePage(pd, pdIndex);
    } else {
        pt = getOrCreateTable(pd, pdIndex, flags, rootInfo.getOwner());
    }
    if (pt == nullptr || (pt->entries[ptIndex] & pagePresent) != 0) {
        return false;
    }
    pt->entries[ptIndex] = (physicalAddress & pageMask) | flags | pagePresent;
    if (rootInfo.getOwner() && (flags & pageUser) && !(flags & pageExternal))
        assignPhysicalPageOwner(physicalAddress, rootInfo.getOwner());
    return true;
}

// Clear one present 4 KiB leaf without freeing its backing or pruning tables.
// The caller handles ownership and local/remote translation invalidation.
bool unmapPageLocked(uint64_t rootPhysicalAddress, uint64_t virtualAddress) {
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
    const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
    const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;
    PageTable* pml4          = getTable(rootPhysicalAddress);
    if ((pml4->entries[pml4Index] & pagePresent) == 0) {
        return false;
    }
    PageTable* pdpt = getTable(pml4->entries[pml4Index]);
    if ((pdpt->entries[pdptIndex] & pagePresent) == 0 ||
        (pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) {
        return false;
    }
    PageTable* pd = getTable(pdpt->entries[pdptIndex]);
    if ((pd->entries[pdIndex] & pagePresent) == 0 ||
        (pd->entries[pdIndex] & (1ULL << 7)) != 0) {
        return false;
    }
    PageTable* pt = getTable(pd->entries[pdIndex]);
    if ((pt->entries[ptIndex] & pagePresent) == 0) {
        return false;
    }
    pt->entries[ptIndex] = 0;
    return true;
}

// Replace permissions while keeping physical address, external-owner marker,
// and hardware accessed/dirty bits. Missing or huge leaves are not created or
// silently converted by this protection operation.
bool protectPageLocked(uint64_t rootPhysicalAddress, uint64_t virtualAddress,
                       uint64_t flags) {
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
    const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
    const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;
    PageTable* pml4          = getTable(rootPhysicalAddress);
    if ((pml4->entries[pml4Index] & pagePresent) == 0) return false;
    PageTable* pdpt = getTable(pml4->entries[pml4Index]);
    if ((pdpt->entries[pdptIndex] & pagePresent) == 0 ||
        (pdpt->entries[pdptIndex] & (1ULL << 7)) != 0)
        return false;
    PageTable* pd = getTable(pdpt->entries[pdptIndex]);
    if ((pd->entries[pdIndex] & pagePresent) == 0 ||
        (pd->entries[pdIndex] & (1ULL << 7)) != 0)
        return false;
    PageTable* pt   = getTable(pd->entries[pdIndex]);
    uint64_t& entry = pt->entries[ptIndex];
    if ((entry & pagePresent) == 0) return false;
    const uint64_t preserved =
        entry & (pageMask | pageExternal | (1ULL << 5) | (1ULL << 6));
    entry = preserved | flags | pagePresent;
    return true;
}

// Use the correct page offset for a 1 GiB, 2 MiB or 4 KiB leaf. Optional flags
// receive the actual leaf entry, not an independently computed combined
// permission policy. Missing paths return physical address zero.
uint64_t translateLocked(uint64_t rootPhysicalAddress, uint64_t virtualAddress,
                         uint64_t* flags) {
    const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
    const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
    const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
    const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;
    PageTable* pml4          = getTable(rootPhysicalAddress);
    if ((pml4->entries[pml4Index] & pagePresent) == 0) {
        return 0;
    }
    PageTable* pdpt = getTable(pml4->entries[pml4Index]);
    if ((pdpt->entries[pdptIndex] & pagePresent) == 0) {
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
    if ((pd->entries[pdIndex] & pagePresent) == 0) {
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
    if ((pt->entries[ptIndex] & pagePresent) == 0) {
        return 0;
    }
    if (flags != nullptr) {
        *flags = pt->entries[ptIndex];
    }
    return (pt->entries[ptIndex] & pageMask) | (virtualAddress & 0xfff);
}

}  // namespace

// Resident core iterator; caller stabilizes the target root and advances
// cursor after each selected page. No VMA or pager allocation occurs here.
bool nextDumpPage(uint64_t root, uint64_t cursor, uint64_t& address,
                  uint64_t& flags) {
    return root && cursor < 0x800000000000ULL &&
           findDumpPage(root, 4, 0, cursor, address, flags);
}

// Adopt validated bootstrap tables after higher-half execution is active.
// Remove the low identity entry, then reload CR3 to flush early translations.
void initVir(const BootInfo* bootInfo) {
    if (bootInfo == nullptr || bootInfo->getPageTableRoot() == 0) {
        device::serialWrite("[MEMORY] missing bootstrap page table root\n");
        while (true) {
            asm volatile("cli; hlt");
        }
    }
    kernelAddressSpaceRoot = bootInfo->getPageTableRoot();
    PageTable* pml4        = getTable(kernelAddressSpaceRoot);
    pml4->entries[0]       = 0;
    asm volatile("mov %0, %%cr3" : : "r"(kernelAddressSpaceRoot) : "memory");
    device::serialWrite(
        "[MEMORY] higher-half direct map active; identity map removed\n");
}

// Kernel-root wrapper; backing ownership stays with its allocator client.
bool mapPage(uint64_t virtualAddress, uint64_t physicalAddress,
             uint64_t flags) {
    return mm::mapPageInAddressSpace(mm::getKernelAddressSpaceRoot(),
                                     virtualAddress, physicalAddress, flags);
}

// Serialize leaf installation globally and invalidate locally on success. The
// target owner must stabilize root lifetime; this helper neither locks the
// target Task nor takes an address-space reference itself.
bool mapPageInAddressSpace(uint64_t rootPhysicalAddress,
                           uint64_t virtualAddress, uint64_t physicalAddress,
                           uint64_t flags) {
    if ((virtualAddress & 0xfff) != 0 || (physicalAddress & 0xfff) != 0) {
        return false;
    }
    bool mapped;
    {
        ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
        mapped = mapPageLocked(rootPhysicalAddress, virtualAddress,
                               physicalAddress, flags);
        if (mapped) {
            asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
        }
    }
    return mapped;
}

// Kernel-root leaf removal, without automatic physical-page release.
bool unmapPage(uint64_t virtualAddress) {
    return mm::unmapPageInAddressSpace(mm::getKernelAddressSpaceRoot(),
                                       virtualAddress);
}

// Remove and invalidate locally while locked, then request remote shootdown
// after unlocking. Callers must finish required invalidation before returning
// owned RAM to allocation, or a stale remote translation could access reuse.
bool unmapPageInAddressSpace(uint64_t rootPhysicalAddress,
                             uint64_t virtualAddress) {
    if ((virtualAddress & 0xfff) != 0) {
        return false;
    }
    bool unmapped;
    {
        ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
        unmapped = unmapPageLocked(rootPhysicalAddress, virtualAddress);
        if (unmapped) {
            asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
        }
    }
    if (unmapped) {
        cpu::requestTlbShootdown(virtualAddress);
    }
    return unmapped;
}

// Invalidate cached permissions on all participating CPUs after updating the
// leaf. Clearing writable only in RAM is insufficient for COW while a remote
// CPU can still use its old writable TLB entry.
bool protectPageInAddressSpace(uint64_t rootPhysicalAddress,
                               uint64_t virtualAddress, uint64_t flags) {
    if ((virtualAddress & 0xfff) != 0 || (flags & pageUser) == 0) return false;
    bool protectedPage;
    {
        ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
        protectedPage =
            protectPageLocked(rootPhysicalAddress, virtualAddress, flags);
        if (protectedPage) {
            asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
        }
    }
    if (protectedPage) cpu::requestTlbShootdown(virtualAddress);
    return protectedPage;
}

// Kernel-root translation returning a physical byte address.
uint64_t getPhysicalAddress(uint64_t virtualAddress) {
    return mm::getPhysicalAddressInAddressSpace(mm::getKernelAddressSpaceRoot(),
                                                virtualAddress);
}

// Serialize table walking; caller already keeps the supplied root alive.
uint64_t getPhysicalAddressInAddressSpace(uint64_t rootPhysicalAddress,
                                          uint64_t virtualAddress,
                                          uint64_t* flags) {
    ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
    return translateLocked(rootPhysicalAddress, virtualAddress, flags);
}

// Validate every covered resident page including partial boundary pages. Zero
// size is accepted. Check low-half/user and optional write permission; only
// the matching legacy Sys caller can inspect its supervisor direct map. This
// predicate does not resolve missing pages or COW faults.
bool validateUserMemory(uint64_t rootPhysicalAddress, uint64_t address,
                        uint64_t size, bool writable) {
    if (size == 0) {
        return true;
    }
    const uint64_t end           = address + size - 1;
    ::kernel::task::Task* caller = task::getCurrent();
    const bool supervisor =
        caller && caller->isSystemService() &&
        caller->addressSpace().rootPhysicalAddress() == rootPhysicalAddress &&
        address >= 0xffff800000000000ULL;
    if (end < address || (!supervisor && end >= 0x0000800000000000ULL)) {
        return false;
    }
    ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
    uint64_t page           = address & ~0xfffULL;
    const uint64_t lastPage = end & ~0xfffULL;
    while (true) {
        uint64_t flags = 0;
        if (translateLocked(rootPhysicalAddress, page, &flags) == 0 ||
            (!supervisor && (flags & pageUser) == 0) ||
            (writable && (flags & pageWritable) == 0)) {
            return false;
        }
        if (page == lastPage) {
            return true;
        }
        page += 4096;
    }
}

// Allocate and zero backing before publishing one private user leaf. Reject
// write+execute and free the allocation on mapping failure; return a specific
// failure category instead of leaking the new physical reference.
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

    const uint64_t physicalAddress = mm::allocatePhysicalPage();
    if (physicalAddress == 0) {
        return UserPageMapResult::OutOfMemory;
    }
    uint8_t* page = reinterpret_cast<uint8_t*>(directMapBase + physicalAddress);
    for (uint64_t index = 0; index < pageSize; ++index) {
        page[index] = 0;
    }
    if (!mm::mapPageInAddressSpace(rootPhysicalAddress, virtualAddress,
                                   physicalAddress, flags)) {
        mm::freePhysicalPage(physicalAddress);
        return UserPageMapResult::MappingFailed;
    }
    return UserPageMapResult::Success;
}

// Copy page-sized fragments through direct-map backing without changing CR3.
// This loader helper checks present/user mappings but does not enforce write
// or COW policy; untrusted writes must use syscall/pager checks first.
bool copyToAddressSpace(uint64_t rootPhysicalAddress, uint64_t destination,
                        const void* source, uint64_t size) {
    constexpr uint64_t pageSize       = 4096;
    constexpr uint64_t userAddressEnd = 0x0000800000000000ULL;
    if (source == nullptr || size == 0 || destination < pageSize ||
        destination >= userAddressEnd || size > userAddressEnd - destination) {
        return false;
    }

    ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
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

// Read resident user bytes across noncontiguous backing pages. Failure can
// follow a copied prefix; callers must not publish partial output as success.
bool copyFromAddressSpace(void* destination, uint64_t rootPhysicalAddress,
                          uint64_t source, uint64_t size) {
    constexpr uint64_t pageSize       = 4096;
    constexpr uint64_t userAddressEnd = 0x0000800000000000ULL;
    if (destination == nullptr || size == 0 || source < pageSize ||
        source >= userAddressEnd || size > userAddressEnd - source) {
        return false;
    }

    ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
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

// Shared kernel page-table root physical address.
uint64_t getKernelAddressSpaceRoot() { return kernelAddressSpaceRoot; }

// Create an empty user half and copy shared high-half entries. Private-root
// teardown must never recursively release those shared kernel tables.
uint64_t createAddressSpaceRoot() {
    ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
    const uint64_t rootPhysicalAddress = mm::allocatePhysicalPage();
    if (rootPhysicalAddress == 0) {
        return 0;
    }
    PageTable* root       = getTable(rootPhysicalAddress);
    PageTable* kernelRoot = getTable(mm::getKernelAddressSpaceRoot());
    for (uint32_t index = 0; index < 256; ++index) {
        root->entries[index] = 0;
    }
    for (uint32_t index = 256; index < 512; ++index) {
        root->entries[index] = kernelRoot->entries[index];
    }
    return rootPhysicalAddress;
}

// Clone low-half mappings into a fresh destination. Ordinary RAM is eagerly
// copied; external pages preserve physical addresses and need separate object
// reference inheritance. Reject huge user leaves. Failure leaves a partial
// root for its creating caller to destroy before publishing a child thread.
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
    ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
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
                    uint64_t destinationPhysical  = sourcePhysical;
                    // Only private RAM gets a new physical page. External
                    // backing must be retained by its object registry before
                    // fork makes the child runnable.
                    if ((entry & pageExternal) == 0) {
                        destinationPhysical = mm::allocatePhysicalPage();
                        if (destinationPhysical == 0) return false;
                        const uint8_t* source =
                            reinterpret_cast<const uint8_t*>(directMapBase +
                                                             sourcePhysical);
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
                            mm::freePhysicalPage(destinationPhysical);
                        }
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// Free only private user tables/root, refusing the shared kernel root. Caller
// must stop all CPUs executing this space and release external mapping records
// first; a table walk cannot establish execution quiescence.
void destroyAddressSpaceRoot(uint64_t rootPhysicalAddress) {
    if (rootPhysicalAddress == 0 ||
        rootPhysicalAddress == mm::getKernelAddressSpaceRoot()) {
        return;
    }
    ::kernel::task::InterruptSpinLockGuard guard(virtualMemoryLock);
    PageTable* root = getTable(rootPhysicalAddress);
    for (uint32_t index = 0; index < 256; ++index) {
        const uint64_t entry = root->entries[index];
        if ((entry & pagePresent) != 0) {
            releaseUserPageTables(entry & pageMask, 3);
        }
    }
    mm::freePhysicalPage(rootPhysicalAddress);
}

// Select the kernel root for ownerless workers and avoid redundant CR3 writes.
// A local CR3 switch does not itself quiesce another CPU or invalidate its
// TLB.
void switchAddressSpace(uint64_t rootPhysicalAddress) {
    if (rootPhysicalAddress == 0) {
        rootPhysicalAddress = mm::getKernelAddressSpaceRoot();
    }
    uint64_t currentRoot;
    asm volatile("movq %%cr3, %0" : "=r"(currentRoot));
    if ((currentRoot & pageMask) != (rootPhysicalAddress & pageMask)) {
        asm volatile("movq %0, %%cr3" : : "r"(rootPhysicalAddress) : "memory");
    }
}

}  // namespace kernel::mm
