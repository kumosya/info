#pragma once
#include "os/abi.h"
namespace os {
enum class MmOperation : uint64_t { MapAnonymous = 1, MapFile, MapShared, Unmap, Protect, Fault, Usage, Reclaim, SharedOpen, SharedUnlink, Fork, Sync };
constexpr uint64_t mmFlagEager = 1U;
constexpr uint64_t mmFlagShared = 2U;
class VirtualMemory {
   public:
    static uint64_t anonymous(uint64_t size, uint32_t protection, bool lazy = true, uint64_t hint = 0);
    static uint64_t shared(uint64_t handle, uint64_t size, uint32_t protection, uint64_t offset = 0, uint64_t hint = 0);
    static uint64_t file(uint64_t descriptor, uint64_t size, uint32_t protection, uint64_t offset = 0, uint64_t hint = 0);
    static int unmap(uint64_t address, uint64_t size);
    static int protect(uint64_t address, uint64_t size, uint32_t protection);
    static int sync(uint64_t address, uint64_t size);
    static uint64_t openShared(const char* name, int flags);
    static int unlinkShared(const char* name);
};
}  // namespace os

namespace os {
struct PageObjectInfo { uint64_t owner, size; uint32_t references; uint16_t node, order; };
struct MemoryUsage { uint64_t total, free, owned, generation; uint32_t nodes, reserved; };
}  // namespace os
