// Host baseline: exercise the actual production partition parser, without OS
// I/O.
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include "service/device/storage/partition.h"

// Host definitions for the production Part value object's used accessors only.
// The real public class declaration is included; IPC and hardware are not
// mocked as if tested. No production object files are linked into this host
// executable.
namespace os {
uint64_t Storage::Part::getStartBlock() const { return startBlock_; }
uint64_t Storage::Part::getBlockCount() const { return blockCount_; }
void Storage::Part::setStartBlock(uint64_t value) { startBlock_ = value; }
void Storage::Part::setBlockCount(uint64_t value) { blockCount_ = value; }
void Storage::Part::setType(uint32_t value) { type_ = value; }
void Storage::Part::setNumber(uint32_t value) { number_ = value; }
void Storage::Part::setUuid(const char* value) {
    std::snprintf(uuid_, sizeof(uuid_), "%s", value);
}
}  // namespace os
using Sector = std::array<uint8_t, 512>;
class Disk {
   public:
    uint64_t capacity = 100;
    std::map<uint64_t, Sector> sectors;
    uint64_t getBlockCount() const { return capacity; }
    os::Storage::Error readBlock(uint64_t lba, void* output) {
        auto item = sectors.find(lba);
        if (lba >= capacity || item == sectors.end())
            return os::Storage::Error::Io;
        std::memcpy(output, item->second.data(), 512);
        return os::Storage::Error::Success;
    }
};
void le32(uint8_t* p, uint32_t value) {
    for (unsigned n = 0; n < 4; ++n) p[n] = uint8_t(value >> (n * 8));
}
void le64(uint8_t* p, uint64_t value) {
    for (unsigned n = 0; n < 8; ++n) p[n] = uint8_t(value >> (n * 8));
}
// Fixture checksum generator; the known vector below independently pins CRC32.
uint32_t crc(const uint8_t* data, size_t size) {
    uint32_t value = 0xffffffffU;
    while (size--) {
        value ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320U : 0);
    }
    return value ^ 0xffffffffU;
}
Disk mbr(uint32_t start = 10, uint32_t count = 20) {
    Disk disk;
    auto& sector = disk.sectors[0];
    sector[510]  = 0x55;
    sector[511]  = 0xaa;
    sector[450]  = 0x83;
    le32(&sector[454], start);
    le32(&sector[458], count);
    return disk;
}
Disk gpt() {
    Disk disk            = mbr(1, 99);
    disk.sectors[0][450] = 0xee;
    auto& entries        = disk.sectors[2];
    entries[0]           = 1;
    entries[16]          = 2;
    le64(&entries[32], 10);
    le64(&entries[40], 19);
    auto& header = disk.sectors[1];
    std::memcpy(header.data(), "EFI PART", 8);
    le32(&header[8], 0x10000);
    le32(&header[12], 92);
    le64(&header[24], 1);
    le64(&header[32], 99);
    le64(&header[40], 3);
    le64(&header[48], 98);
    le64(&header[72], 2);
    le32(&header[80], 4);
    le32(&header[84], 128);
    le32(&header[88], crc(entries.data(), entries.size()));
    le32(&header[16], crc(header.data(), 92));
    return disk;
}
void refreshGptCrc(Disk& disk) {
    auto& header        = disk.sectors[1];
    const auto& entries = disk.sectors[2];
    le32(&header[88], crc(entries.data(), entries.size()));
    le32(&header[16], 0);
    le32(&header[16], crc(header.data(), 92));
}
bool parse(Disk disk, uint32_t expected, uint64_t start, uint64_t length,
           uint32_t slots = 4) {
    os::Storage::Part parts[4]{};
    uint32_t found = 0;
    return storage::findPartitions(disk, parts, slots, found) &&
           found == expected && parts[0].getStartBlock() == start &&
           parts[0].getBlockCount() == length;
}
bool rejected(Disk disk, uint32_t slots = 4) {
    os::Storage::Part parts[4]{};
    uint32_t found = 0;
    return !storage::findPartitions(disk, parts, slots, found);
}
int main() {
    unsigned passed = 0, failed = 0;
    auto check = [&](const char* name, bool result) {
        std::printf("%s %s\n", result ? "PASS" : "FAIL", name);
        result ? ++passed : ++failed;
    };
    const auto* vector = reinterpret_cast<const uint8_t*>("123456789");
    check("crc32-known-vector",
          crc(vector, 9) == 0xcbf43926U &&
              (storage::detail::updateCrc32(0xffffffffU, vector, 9) ^
               0xffffffffU) == 0xcbf43926U);
    check("mbr-valid", parse(mbr(), 1, 10, 20));
    check("mbr-last-block", parse(mbr(99, 1), 1, 99, 1));
    check("mbr-range-overrun", rejected(mbr(99, 2)));
    check("mbr-start-at-capacity", rejected(mbr(100, 1)));
    check("mbr-zero-length", rejected(mbr(10, 0)));
    check("mbr-output-capacity", rejected(mbr(), 0));
    Disk pair            = mbr();
    pair.sectors[0][466] = 0x83;
    le32(&pair.sectors[0][470], 30);
    le32(&pair.sectors[0][474], 10);
    check("mbr-adjacent-partitions", parse(pair, 2, 10, 20));
    le32(&pair.sectors[0][470], 29);
    check("mbr-overlapping-partitions", rejected(pair));
    Disk bad            = mbr();
    bad.sectors[0][510] = 0;
    check("mbr-bad-signature", rejected(bad));
    check("backend-read-error", rejected(Disk{}));
    check("gpt-valid", parse(gpt(), 1, 10, 10));
    bad = gpt();
    bad.sectors[1][16] ^= 1;
    check("gpt-bad-header-crc", rejected(bad));
    bad = gpt();
    bad.sectors[2][16] ^= 1;
    check("gpt-bad-entry-crc", rejected(bad));
    bad = gpt();
    le64(&bad.sectors[2][32], 25);
    refreshGptCrc(bad);
    check("gpt-reversed-range", rejected(bad));
    bad = gpt();
    le64(&bad.sectors[1][72], UINT64_MAX);
    refreshGptCrc(bad);
    check("gpt-table-start-overflow", rejected(bad));
    bad = gpt();
    le32(&bad.sectors[1][80], 8);
    refreshGptCrc(bad);
    check("gpt-table-overlaps-usable-space", rejected(bad));
    bad                 = gpt();
    bad.sectors[2][128] = 1;
    le64(&bad.sectors[2][160], 19);
    le64(&bad.sectors[2][168], 21);
    refreshGptCrc(bad);
    check("gpt-overlapping-partitions", rejected(bad));
    std::printf("RESULT passed=%u failed=%u\n", passed, failed);
    return failed ? 1 : 0;
}
