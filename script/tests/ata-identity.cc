/* ATA IDENTIFY 字段的有效位、容量和扇区大小回归。 */
#include <cassert>
#include "service/device/storage/ata.h"

int main()
{
    uint16_t words[256]{};
    storage::AtaIdentity identity;
    assert(!identity.decode(words));
    words[49] = 1U << 9;
    words[60] = 12345;
    assert(identity.decode(words) && identity.getBlockCount() == 12345 && !identity.hasLba48());
    assert(!identity.isCacheKnown() && !identity.supportsFlush());

    words[83] = 0x4000 | (1U << 10) | (1U << 12);
    words[100] = 0x4321;
    words[101] = 0x1234;
    words[102] = 1;
    words[82] = 1U << 5;
    words[85] = 1U << 5;
    words[87] = 0x4000;
    assert(identity.decode(words) && identity.hasLba48() && identity.getBlockCount() == 0x112344321ULL);
    assert(identity.supportsFlush() && !identity.usesExtendedFlush() && identity.isCacheKnown() && identity.isWriteCacheEnabled());
    words[83] |= 1U << 13;
    words[106] = 0x5000;
    words[117] = 2048;
    assert(identity.decode(words) && identity.getLogicalBlockSize() == 4096 && identity.usesExtendedFlush());
    words[117] = 2049;
    assert(!identity.decode(words));
    assert(identity.getLogicalBlockSize() == 4096);
    assert(identity.getBlockCount() == 0x112344321ULL);
    words[117] = 2048;
    words[103] = 1;
    assert(!identity.decode(words));
    words[103] = 0;
    words[0] = 0x8000;
    assert(!identity.decode(words));
    return 0;
}
