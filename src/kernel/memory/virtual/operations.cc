#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/mm.h"
#include "kernel/syscall.h"
namespace kernel::memory {
namespace {
constexpr uint64_t pageSize = 4096, maximumMapSize = 64ULL * 1024 * 1024, directMapBase = 0xffff800000000000ULL;
uint64_t error(SyscallError value) { return static_cast<uint64_t>(static_cast<int64_t>(value)); }
bool validUserRange(uint64_t address, uint64_t size) { return size && address >= 0x10000 && address + size > address && address + size <= 0x0000800000000000ULL; }
}
uint64_t MappingOperations::map(Task& target, uint64_t address, uint64_t size, uint64_t protection) {
    kernel::Task* task = &target;
    if (task == nullptr || task == kernel::task::getIdle() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size) ||
        (protection & ~3ULL) != 0) {
        return error(kernel::SyscallError::InvalidArgument);
    }
    if ((protection & 3) == 3) {
        return error(kernel::SyscallError::PermissionDenied);
    }

    kernel::PreemptionSpinLockGuard addressGuard(task->addressSpace().lock());
    if (!task->addressSpace().rootPhysicalAddress()) return error(kernel::SyscallError::NotFound);
    uint64_t flags = kernel::pageUser;
    if ((protection & 1) != 0) {
        flags |= kernel::pageWritable;
    }
    if ((protection & 2) == 0) {
        flags |= kernel::pageNoExecute;
    }

    uint64_t mappedSize = 0;
    while (mappedSize < size) {
        const uint64_t physicalAddress = kernel::memory::allocatePhysicalPage();
        if (physicalAddress == 0 ||
            !kernel::memory::mapPageInAddressSpace(
                task->addressSpace().rootPhysicalAddress(),
                address + mappedSize, physicalAddress, flags)) {
            if (physicalAddress != 0) {
                kernel::memory::freePhysicalPage(physicalAddress);
            }
            while (mappedSize != 0) {
                mappedSize -= pageSize;
                const uint64_t physical =
                    kernel::memory::getPhysicalAddressInAddressSpace(
                        task->addressSpace().rootPhysicalAddress(),
                        address + mappedSize);
                kernel::memory::unmapPageInAddressSpace(
                    task->addressSpace().rootPhysicalAddress(),
                    address + mappedSize);
                kernel::memory::freePhysicalPage(physical & ~(pageSize - 1));
            }
            return error(kernel::SyscallError::OutOfMemory);
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

uint64_t MappingOperations::unmap(Task& target, uint64_t address, uint64_t size) {
    kernel::Task* task = &target;
    if (task == nullptr || task == kernel::task::getIdle() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size)) {
        return error(kernel::SyscallError::InvalidArgument);
    }
    kernel::PreemptionSpinLockGuard addressGuard(task->addressSpace().lock());
    if (!task->addressSpace().rootPhysicalAddress()) return error(kernel::SyscallError::NotFound);
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t flags = 0;
        const uint64_t physical =
            kernel::memory::getPhysicalAddressInAddressSpace(
                task->addressSpace().rootPhysicalAddress(), address + offset,
                &flags);
        if (physical == 0 || (flags & kernel::pageUser) == 0) {
            return error(kernel::SyscallError::NotFound);
        }
    }
    void* reservation = nullptr; void* sharedReservation = nullptr;
    if (!SharedMemory::prepareUnmap(*task, address, size, sharedReservation)) return error(kernel::SyscallError::OutOfMemory);
    if (!PageMappings::prepareUnmap(*task, address, size, reservation)) {
        SharedMemory::cancelUnmap(sharedReservation); return error(kernel::SyscallError::OutOfMemory);
    }
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t flags = 0;
        const uint64_t physical =
            kernel::memory::getPhysicalAddressInAddressSpace(
                task->addressSpace().rootPhysicalAddress(), address + offset,
                &flags);
        kernel::memory::unmapPageInAddressSpace(
            task->addressSpace().rootPhysicalAddress(), address + offset);
        if ((flags & kernel::pageExternal) == 0) {
            kernel::memory::freePhysicalPage(physical & ~(pageSize - 1));
        }
    }
    PageMappings::completeUnmap(*task, address, size, reservation);
    kernel::SharedMemory::releaseMapping(*task, address, size, sharedReservation);
    return 0;
}

uint64_t MappingOperations::protect(Task& target, uint64_t address, uint64_t size,
                           uint64_t protection) {
    kernel::Task* task = &target;
    if (task == nullptr || task == kernel::task::getIdle() ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        size > maximumMapSize || !validUserRange(address, size) ||
        (protection & ~3ULL) != 0 || (protection & 3) == 3) {
        return error(kernel::SyscallError::InvalidArgument);
    }
    kernel::PreemptionSpinLockGuard addressGuard(task->addressSpace().lock());
    if (!task->addressSpace().rootPhysicalAddress()) return error(kernel::SyscallError::NotFound);
    if (!kernel::SharedMemory::protectionAllowed(*task, address, size, protection) ||
        !PageMappings::protectionAllowed(*task, address, size, protection)) {
        return error(kernel::SyscallError::PermissionDenied);
    }
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        uint64_t existing = 0;
        if (!kernel::memory::getPhysicalAddressInAddressSpace(task->addressSpace().rootPhysicalAddress(), address + offset, &existing) || !(existing & kernel::pageUser))
            return error(kernel::SyscallError::NotFound);
    }
    uint64_t flags = kernel::pageUser;
    if ((protection & 1) != 0) flags |= kernel::pageWritable;
    if ((protection & 2) == 0) flags |= kernel::pageNoExecute;
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        if (!kernel::memory::protectPageInAddressSpace(
                task->addressSpace().rootPhysicalAddress(), address + offset,
                flags)) {
            return error(kernel::SyscallError::InvalidArgument);
        }
    }
    return 0;
}
}  // namespace kernel::memory
