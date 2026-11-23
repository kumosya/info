/* 检查跨位图字边界的事务状态，确保回滚标记互不覆盖。 */
#include "service/mm/mm.h"
#include <cassert>

int main() {
    mm::ResidentPageFlags original;
    for (size_t index = 0; index < mm::MemoryState::pageCapacity; ++index) {
        assert(!original.get(index));
        original.set(index, index % 3 == 0);
    }

    mm::ResidentPageFlags restored = original;
    restored.set(63, false);
    restored.set(64, true);
    restored.set(mm::MemoryState::pageCapacity - 1, true);
    for (size_t index = 0; index < mm::MemoryState::pageCapacity; ++index) {
        assert(original.get(index) == (index % 3 == 0));
        const bool expected = index == 63 ? false :
            index == 64 || index == mm::MemoryState::pageCapacity - 1 ? true : index % 3 == 0;
        assert(restored.get(index) == expected);
    }
    assert(sizeof(original) <= mm::MemoryState::pageCapacity / 8);
}
