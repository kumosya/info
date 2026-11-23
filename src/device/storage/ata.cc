/* 驱动实现放在源文件，头文件只公开类型和接口。 */
#include "service/device/storage/ata.h"

namespace storage {

    /* 扩展字段先检查有效位，再读取容量、逻辑扇区大小和缓存命令支持位。 */
    bool AtaIdentity::decode(const uint16_t* words) {
        AtaIdentity parsed;

        if (!words || (words[0] & 0x8000) || !(words[49] & (1U << 9))) {
            return false;
        }

        const bool extended = (words[83] & 0xc000) == 0x4000;
        parsed.lba48_ = extended && (words[83] & (1U << 10));
        parsed.blocks_ = uint64_t(words[60]) | (uint64_t(words[61]) << 16);

        if (parsed.lba48_) {
            parsed.blocks_ =
                uint64_t(words[100]) | (uint64_t(words[101]) << 16) |
                (uint64_t(words[102]) << 32) | (uint64_t(words[103]) << 48);
        }

        if (!parsed.blocks_ ||
            parsed.blocks_ > (parsed.lba48_ ? (1ULL << 48) : (1ULL << 28))) {
            return false;
        }

        parsed.logicalBytes_ = 512;

        if ((words[106] & 0xc000) == 0x4000 && (words[106] & (1U << 12))) {
            const uint32_t wordCount =
                uint32_t(words[117]) | (uint32_t(words[118]) << 16);
            if (wordCount < 256 || wordCount > 2048 ||
                (wordCount & (wordCount - 1))) {
                return false;
            }

            parsed.logicalBytes_ = wordCount * 2;
        }

        parsed.cacheEnabled_ =
            (words[82] & (1U << 5)) && (words[85] & (1U << 5));
        parsed.cacheKnown_ = extended && (!(words[82] & (1U << 5)) ||
                                          (words[87] & 0xc000) == 0x4000);
        parsed.flushExtended_ =
            parsed.lba48_ && extended && (words[83] & (1U << 13));
        parsed.flush_ =
            extended && ((words[83] & (1U << 12)) || parsed.flushExtended_);
        if (parsed.blocks_ > UINT64_MAX / parsed.logicalBytes_) {
            return false;
        }

        *this = parsed;
        return true;
    }

}  // namespace storage
