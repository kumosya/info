#pragma once

#include <stdint.h>

namespace kernel {

struct BootInfo;

constexpr uint64_t pagePresent      = 1ULL << 0;
constexpr uint64_t pageWritable     = 1ULL << 1;
constexpr uint64_t pageUser         = 1ULL << 2;
constexpr uint64_t pageWriteThrough = 1ULL << 3;
constexpr uint64_t pageCacheDisable = 1ULL << 4;
constexpr uint64_t pageGlobal       = 1ULL << 8;
constexpr uint64_t pageExternal     = 1ULL << 9;
constexpr uint64_t pageNoExecute    = 1ULL << 63;

enum class UserPageMapResult : uint8_t {
    Success,
    InvalidArgument,
    OutOfMemory,
    MappingFailed
};

namespace memory {
    void initVir(const BootInfo* bootInfo);
    bool mapPage(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t flags);
    bool unmapPage(uint64_t virtualAddress);
    uint64_t getPhysicalAddress(uint64_t virtualAddress);
    bool mapPageInAddressSpace(uint64_t rootPhysicalAddress,
                            uint64_t virtualAddress, uint64_t physicalAddress,
                            uint64_t flags);
    bool unmapPageInAddressSpace(uint64_t rootPhysicalAddress,
                                uint64_t virtualAddress);
    bool protectPageInAddressSpace(uint64_t rootPhysicalAddress,
                                uint64_t virtualAddress, uint64_t flags);
    uint64_t getPhysicalAddressInAddressSpace(uint64_t rootPhysicalAddress,
                                            uint64_t virtualAddress,
                                            uint64_t* flags = nullptr);
    bool validateUserMemory(uint64_t rootPhysicalAddress, uint64_t address,
                            uint64_t size, bool writable);
    UserPageMapResult allocateUserPageInAddressSpace(uint64_t rootPhysicalAddress,
                                                    uint64_t virtualAddress,
                                                    uint64_t flags);
    bool copyToAddressSpace(uint64_t rootPhysicalAddress, uint64_t destination,
                            const void* source, uint64_t size);
    bool copyFromAddressSpace(void* destination, uint64_t rootPhysicalAddress,
                            uint64_t source, uint64_t size);
    uint64_t getKernelAddressSpaceRoot();
    uint64_t createAddressSpaceRoot();
    bool cloneAddressSpaceRoot(uint64_t sourceRootPhysicalAddress,
                            uint64_t destinationRootPhysicalAddress);
    void destroyAddressSpaceRoot(uint64_t rootPhysicalAddress);
    void switchAddressSpace(uint64_t rootPhysicalAddress);

}  // namespace memory
}  // namespace kernel
