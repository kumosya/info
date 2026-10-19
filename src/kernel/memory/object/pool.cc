#include "kernel/mm.h"
namespace kernel::memory {
bool FixedPool::initialize(void* storage, size_t bytes, size_t size) {
    if (!storage || (reinterpret_cast<uint64_t>(storage) & 15) || !size || size > SIZE_MAX - 15) return false;
    const size_t stride = (size + 15) & ~15ULL, capacity = bytes / stride;
    if (!capacity || capacity > 4096 || stride > UINT32_MAX) return false;
    storage_ = storage; stride_ = stride; capacity_ = free_ = capacity; first_ = 0; lock_.init();
    for (auto& word : used_) word = 0;
    for (uint32_t i = 0; i < capacity_; ++i) *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(storage_) + i * stride_) = i + 1 < capacity_ ? i + 1 : UINT32_MAX;
    return true;
}
void* FixedPool::allocate() {
    InterruptSpinLockGuard guard(lock_);
    if (!storage_ || first_ == UINT32_MAX) return nullptr;
    const uint32_t index = first_; void* object = static_cast<uint8_t*>(storage_) + index * stride_;
    first_ = *static_cast<uint32_t*>(object); --free_; used_[index / 64] |= 1ULL << (index % 64); return object;
}
bool FixedPool::release(void* object) {
    InterruptSpinLockGuard guard(lock_);
    const uint64_t offset = reinterpret_cast<uint64_t>(object) - reinterpret_cast<uint64_t>(storage_);
    if (!storage_ || offset % stride_ || offset / stride_ >= capacity_) return false;
    const uint32_t index = offset / stride_; const uint64_t bit = 1ULL << (index % 64);
    if (!(used_[index / 64] & bit)) return false;
    used_[index / 64] &= ~bit; *static_cast<uint32_t*>(object) = first_; first_ = index; ++free_; return true;
}
size_t FixedPool::available() { InterruptSpinLockGuard guard(lock_); return free_; }
}  // namespace kernel::memory
