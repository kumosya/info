#include "service/common/runtime.h"
#pragma once
#include <os/service.h>

namespace services {
// Local handles are a cache; only protected tokens are serialized.
class RecoveryCapabilities {
   public:
    explicit RecoveryCapabilities(os::Sys::Checkpoint& checkpoint) : checkpoint_(checkpoint) {}
    void begin() { ++visit_; }
    bool keep(uint64_t token) {
        if (!token) return true;
        const uint32_t index = static_cast<uint32_t>(token) - 1;
        if (index >= capacity) return false;
        entries_[index] = {token, visit_}; return true;
    }
    uint64_t encode(uint64_t handle) {
        if (!handle) return 0;
        const uint32_t slot = static_cast<uint32_t>(handle) - 1;
        uint64_t token = slot < capacity && cache_[slot].handle == handle ? cache_[slot].token : 0;
        const uint32_t tokenSlot = static_cast<uint32_t>(token) - 1;
        if (!token || tokenSlot >= capacity || entries_[tokenSlot].token != token) {
            token = checkpoint_.put(handle);
            if (!token) return 0;
            if (slot < capacity) cache_[slot] = {handle, token};
        }
        const uint32_t index = static_cast<uint32_t>(token) - 1;
        if (index >= capacity) return 0;
        entries_[index] = {token, visit_}; return token;
    }
    uint64_t decode(uint64_t token) {
        if (!token) return 0;
        const auto handle = checkpoint_.get(token);
        if (!handle) return 0;
        const uint32_t index = static_cast<uint32_t>(token) - 1, slot = static_cast<uint32_t>(handle) - 1;
        if (index >= capacity) { os::closeHandle(handle); return 0; }
        entries_[index] = {token, visit_};
        if (slot < capacity) cache_[slot] = {handle, token};
        return handle;
    }
    bool collect() {
        for (auto& entry : entries_) if (entry.token && entry.visit != visit_) {
            if (!checkpoint_.drop(entry.token)) return false;
            entry = {};
        }
        return true;
    }
    bool discardOrphans() {
        uint64_t cursor = 0;
        while (const auto token = checkpoint_.next(cursor)) {
            const uint32_t slot = static_cast<uint32_t>(token) - 1;
            if ((slot >= capacity || entries_[slot].token != token) && !checkpoint_.drop(token)) return false;
        }
        return true;
    }
   private:
    static constexpr uint32_t capacity = 16384;
    struct Cached { uint64_t handle = 0, token = 0; } cache_[capacity];
    struct Entry { uint64_t token = 0, visit = 0; } entries_[capacity];
    uint64_t visit_ = 1;
    os::Sys::Checkpoint& checkpoint_;
};
}
