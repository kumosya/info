/* ATA 识别数据和命令编码；AHCI 与传统 IDE 共用容量、扇区及缓存规则。 */
#pragma once
#include <cstdint>

namespace storage {

    /* 保存磁盘真正支持的寻址方式，不用截断容量的方式兼容大硬盘。 */
    class AtaIdentity {
        public:
            AtaIdentity() = default;
            ~AtaIdentity() = default;

            uint64_t getBlockCount() const {
                return blocks_;
            }

            uint32_t getLogicalBlockSize() const {
                return logicalBytes_;
            }

            bool hasLba48() const {
                return lba48_;
            }

            bool isWriteCacheEnabled() const {
                return cacheEnabled_;
            }

            bool isCacheKnown() const {
                return cacheKnown_;
            }

            bool supportsFlush() const {
                return flush_;
            }

            bool usesExtendedFlush() const {
                return flushExtended_;
            }

            /* IDENTIFY 的字单位为 16 位；扩展字段必须先检查有效位。 */
            bool decode(const uint16_t* words);

        private:
            uint64_t blocks_ = 0;
            uint32_t logicalBytes_ = 512;
            bool lba48_ = false;
            bool cacheEnabled_ = false;
            bool cacheKnown_ = false;
            bool flush_ = false;
            bool flushExtended_ = false;
    };

}  // namespace storage
