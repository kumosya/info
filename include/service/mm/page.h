#pragma once
#include <cstddef>
#include <cstdint>
namespace mm {
enum class BackingKind : uint8_t { Anonymous, File, Shared };
struct Region {
    uint64_t task = 0, address = 0, size = 0, offset = 0, backing = 0;
    uint32_t protection = 0, maximumProtection = 0;
    BackingKind kind = BackingKind::Anonymous;
    bool lazy = true, shared = false, used = false;
};
class RegionTable {
   public:
    static constexpr size_t capacity = 512;
    static constexpr uint64_t lower = 0x10000, upper = 0x0000800000000000ULL;
    static bool validRange(uint64_t address, uint64_t size);
    const Region* find(uint64_t task, uint64_t address) const;
    Region* find(uint64_t task, uint64_t address);
    uint64_t findGap(uint64_t task, uint64_t size,
                     uint64_t hint = 0x50000000) const;
    bool insert(const Region& region);
    bool remove(uint64_t task, uint64_t address, uint64_t size);
    bool protect(uint64_t task, uint64_t address, uint64_t size,
                 uint32_t protection);
    bool covered(uint64_t task, uint64_t address, uint64_t size) const;
    void removeTask(uint64_t task);
    size_t references(uint64_t backing) const;
    const Region& at(size_t index) const { return regions_[index]; }

   private:
    Region regions_[capacity]{};
    Region* vacant();
    size_t vacancies() const;
    void merge(uint64_t task);
};
}  // namespace mm
