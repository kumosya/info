/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/virtual/operations.cc
 * Checked user mapping, protection, unmapping, and split preparation.
 */

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/task.h"

namespace kernel::mm {

namespace {
// Eager mechanisms accept aligned ranges up to 64 MiB. User MM retains VMA,
// file-backed and lazy-allocation policy above these PTE operations.
constexpr uint64_t pageSize = 4096, maximumMapSize = 64ULL * 1024 * 1024,
                   directMapBase = 0xffff800000000000ULL;
// Native negative error encoded in the unsigned mechanism result.
uint64_t error(::kernel::task::SyscallError value) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
}
// Reject empty, low-guard, wrapping or high-half extents before touching
// tables.
bool validUserRange(uint64_t address, uint64_t size) {
    return size && address >= 0x10000 && address + size > address &&
           address + size <= 0x0000800000000000ULL;
}
}  // namespace
// Keep the target root stable, reject write+execute, then allocate/map eager
// anonymous pages. Failure unwinds only this operation's installed prefix,
// leaving occupied preexisting leaves untouched.
uint64_t mapUserPages(::kernel::task::Task& target, uint64_t address,
                      uint64_t size, uint64_t protection) {
    kernel::task::Task* task = &target;
    if (task == nullptr || task == kernel::task::getIdle() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size) ||
        (protection & ~3ULL) != 0) {
        return error(kernel::task::SyscallError::InvalidArgument);
    }
    if ((protection & 3) == 3) {
        return error(kernel::task::SyscallError::PermissionDenied);
    }

    kernel::task::PreemptionSpinLockGuard addressGuard(
        task->addressSpace().lock());
    if (!task->addressSpace().rootPhysicalAddress())
        return error(kernel::task::SyscallError::NotFound);
    uint64_t flags = pageUser;
    if ((protection & 1) != 0) {
        flags |= pageWritable;
    }
    if ((protection & 2) == 0) {
        flags |= pageNoExecute;
    }

    uint64_t mappedSize = 0;
    while (mappedSize < size) {
        const uint64_t physicalAddress = allocatePhysicalPage();
        if (physicalAddress == 0 ||
            !mapPageInAddressSpace(task->addressSpace().rootPhysicalAddress(),
                                   address + mappedSize, physicalAddress,
                                   flags)) {
            if (physicalAddress != 0) {
                freePhysicalPage(physicalAddress);
            }
            // Unmap and finish shootdown before freeing each privately owned
            // page, so remote stale translations cannot reach a frame after
            // allocator reuse.
            while (mappedSize != 0) {
                mappedSize -= pageSize;
                const uint64_t physical = getPhysicalAddressInAddressSpace(
                    task->addressSpace().rootPhysicalAddress(),
                    address + mappedSize);
                unmapPageInAddressSpace(
                    task->addressSpace().rootPhysicalAddress(),
                    address + mappedSize);
                freePhysicalPage(physical & ~(pageSize - 1));
            }
            return error(kernel::task::SyscallError::OutOfMemory);
        }
        uint8_t* page =
            reinterpret_cast<uint8_t*>(directMapBase + physicalAddress);
        for (uint64_t index = 0; index < pageSize; ++index) {
            page[index] = 0;
        }
        mappedSize += pageSize;
    }
    return address;
}

// Preflight all leaves and reserve split records in both registries before any
// destructive PTE edit. Ordinary backing releases pages; external backing
// releases object references through the completed record updates.
uint64_t unmapUserPages(::kernel::task::Task& target, uint64_t address,
                        uint64_t size) {
    kernel::task::Task* task = &target;
    if (task == nullptr || task == kernel::task::getIdle() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size)) {
        return error(kernel::task::SyscallError::InvalidArgument);
    }
    kernel::task::PreemptionSpinLockGuard addressGuard(
        task->addressSpace().lock());
    if (!task->addressSpace().rootPhysicalAddress())
        return error(kernel::task::SyscallError::NotFound);
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t flags          = 0;
        const uint64_t physical = getPhysicalAddressInAddressSpace(
            task->addressSpace().rootPhysicalAddress(), address + offset,
            &flags);
        if (physical == 0 || (flags & pageUser) == 0) {
            return error(kernel::task::SyscallError::NotFound);
        }
    }
    void* reservation       = nullptr;
    void* sharedReservation = nullptr;
    if (!sharedMemory().prepareUnmap(*task, address, size, sharedReservation))
        return error(kernel::task::SyscallError::OutOfMemory);
    // Cancel the already prepared shared split if page-object preparation
    // fails; no PTE has changed yet, so both mappings and record state remain
    // intact.
    if (!maps().prepareUnmap(*task, address, size, reservation)) {
        sharedMemory().cancelUnmap(sharedReservation);
        return error(kernel::task::SyscallError::OutOfMemory);
    }
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t flags          = 0;
        const uint64_t physical = getPhysicalAddressInAddressSpace(
            task->addressSpace().rootPhysicalAddress(), address + offset,
            &flags);
        unmapPageInAddressSpace(task->addressSpace().rootPhysicalAddress(),
                                address + offset);
        if ((flags & pageExternal) == 0) {
            freePhysicalPage(physical & ~(pageSize - 1));
        }
    }
    // Commit whole removal, boundary trimming or middle splitting using
    // prepared metadata. No post-removal allocation should now be needed for a
    // split.
    maps().completeUnmap(*task, address, size, reservation);
    sharedMemory().releaseMapping(*task, address, size, sharedReservation);
    return 0;
}

// Check every leaf and original object permission ceiling before changing
// PTEs. COW records cannot regain write through ordinary mprotect; trusted
// pager resolution must establish writable backing first.
uint64_t protectUserPages(::kernel::task::Task& target, uint64_t address,
                          uint64_t size, uint64_t protection) {
    kernel::task::Task* task = &target;
    if (task == nullptr || task == kernel::task::getIdle() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size) ||
        (protection & ~3ULL) != 0 || (protection & 3) == 3) {
        return error(kernel::task::SyscallError::InvalidArgument);
    }
    kernel::task::PreemptionSpinLockGuard addressGuard(
        task->addressSpace().lock());
    if (!task->addressSpace().rootPhysicalAddress())
        return error(kernel::task::SyscallError::NotFound);
    if (!sharedMemory().protectionAllowed(*task, address, size, protection) ||
        !maps().protectionAllowed(*task, address, size, protection)) {
        return error(kernel::task::SyscallError::PermissionDenied);
    }
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t existing = 0;
        if (!getPhysicalAddressInAddressSpace(
                task->addressSpace().rootPhysicalAddress(), address + offset,
                &existing) ||
            !(existing & pageUser))
            return error(kernel::task::SyscallError::NotFound);
    }
    // Translate public write/execute into PTE bits; low-level protection
    // retains physical ownership markers and hardware accessed/dirty state.
    uint64_t flags = pageUser;
    if ((protection & 1) != 0) flags |= pageWritable;
    if ((protection & 2) == 0) flags |= pageNoExecute;
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        if (!protectPageInAddressSpace(
                task->addressSpace().rootPhysicalAddress(), address + offset,
                flags)) {
            return error(kernel::task::SyscallError::InvalidArgument);
        }
    }
    return 0;
}

}  // namespace kernel::mm
