/* 内存服务对象的初始状态、区域拆分和快照复制。 */
#include <cassert>
#include <type_traits>
#include "service/mm/mm.h"

int main() {
    static_assert(std::is_standard_layout_v<mm::ResidentPage>);
    static_assert(std::is_trivially_copyable_v<mm::ResidentPage>);
    static_assert(std::is_standard_layout_v<mm::Backing>);
    static_assert(std::is_trivially_copyable_v<mm::Backing>);

    const mm::ResidentPage empty;
    assert(!empty.isUsed() && !empty.isMapped());
    const mm::ResidentPage page(1, 0x10000, 9);
    assert(page.isUsed() && page.getTask() == 1);
    assert(page.getAddress() == 0x10000 && page.getHandle() == 9);
    const mm::ResidentPage copy = page;
    assert(copy.getHandle() == 9 && !copy.isDirty());

    const mm::Backing backing(3, mm::BackingKind::Shared, 12);
    assert(backing.isUsed() && backing.getId() == 3);
    assert(backing.getKind() == mm::BackingKind::Shared);
    assert(backing.getHandle() == 12);

    mm::RegionTable table;
    assert(table.insert(mm::Region(1, 0x10000, 0x4000, 3)));
    assert(table.covered(1, 0x10000, 0x4000));
    assert(!table.insert(mm::Region(1, 0x11000, 0x1000, 3)));
    assert(table.remove(1, 0x11000, 0x1000));
    assert(table.find(1, 0x11000) == nullptr);
    const mm::Region* right = table.find(1, 0x12000);
    assert(right && right->getAddress() == 0x12000);
    assert(right->getSize() == 0x2000 && right->getTask() == 1);

    return 0;
}
