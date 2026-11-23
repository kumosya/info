/* 用内存扇区验证新的分区读取接口，包括 GPT CRC 和分区重叠拒绝。 */
#include <cassert>
#include <cstring>
#include "service/device/storage/partition.h"

class MemoryDisk final : public storage::PartitionReader {
public:
    uint8_t sectors[128][512]{};
    uint64_t getBlockCount() const override { return 128; }
    os::Storage::Error readBlock(uint64_t block, void* data) override {
        if (block >= 128) {
            return os::Storage::Error::InvalidBlock;
        }
        memcpy(data, sectors[block], 512);
        return os::Storage::Error::Success;
    }
};

/* 测试数据按磁盘小端字节序生成，不依赖主机结构体布局。 */
static void integer(uint8_t* bytes, uint64_t value, unsigned count)
{
    for (unsigned index = 0; index < count; ++index) {
        bytes[index] = value >> (index * 8);
    }
}

static uint32_t crc32(const uint8_t* bytes, unsigned count)
{
    uint32_t crc = UINT32_MAX;
    for (unsigned index = 0; index < count; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (crc & 1 ? 0xedb88320U : 0);
        }
    }
    return ~crc;
}

int main()
{
    assert(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926);
    MemoryDisk disk;
    storage::PartitionTable table(disk);
    os::Storage::Part parts[4];
    uint32_t count = 0;
    assert(!table.read(parts, 4, count));

    disk.sectors[0][510] = 0x55;
    disk.sectors[0][511] = 0xaa;
    uint8_t* first = disk.sectors[0] + 446;
    first[4] = 0x83;
    integer(first + 8, 8, 4);
    integer(first + 12, 32, 4);
    assert(table.read(parts, 4, count) && count == 1);
    assert(parts[0].getStartBlock() == 8 && parts[0].getBlockCount() == 32);
    uint8_t* second = first + 16;
    second[4] = 0x83;
    integer(second + 8, 20, 4);
    integer(second + 12, 10, 4);
    assert(!table.read(parts, 4, count));

    memset(disk.sectors[0] + 446, 0, 64);
    first[4] = 0xee;
    integer(first + 8, 1, 4);
    integer(first + 12, 127, 4);
    uint8_t* header = disk.sectors[1];
    memcpy(header, "EFI PART", 8);
    integer(header + 8, 0x10000, 4);
    integer(header + 12, 92, 4);
    integer(header + 24, 1, 8);
    integer(header + 32, 127, 8);
    integer(header + 40, 34, 8);
    integer(header + 48, 126, 8);
    integer(header + 72, 2, 8);
    integer(header + 80, 4, 4);
    integer(header + 84, 128, 4);
    uint8_t* entry = disk.sectors[2];
    entry[0] = 1;
    entry[16] = 0x12;
    integer(entry + 32, 34, 8);
    integer(entry + 40, 60, 8);
    integer(header + 88, crc32(entry, 512), 4);
    integer(header + 16, crc32(header, 92), 4);
    assert(table.read(parts, 4, count) && count == 1);
    assert(parts[0].getStartBlock() == 34 && parts[0].getBlockCount() == 27);
    entry[80] ^= 1;
    assert(!table.read(parts, 4, count));
    entry[80] ^= 1;
    header[16] ^= 1;
    assert(!table.read(parts, 4, count));
    return 0;
}
