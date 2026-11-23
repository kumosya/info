/* 分区表读取接口和解析器；磁盘结构及校验算法由源文件实现。 */
#pragma once
#include <os/storage.h>

#include <cstdint>

namespace storage {

    /* 解析器只依赖扇区读取能力，不绑定 VirtIO、AHCI 或 IDE。 */
    class PartitionReader {
        public:
            virtual uint64_t getBlockCount() const = 0;
            virtual os::Storage::Error readBlock(uint64_t block,
                                                 void* data) = 0;

        protected:
            ~PartitionReader() = default;
    };

    /* 绑定一块磁盘，读取并校验其 MBR 或 GPT 分区表。 */
    class PartitionTable {
            PartitionReader& reader_;

        public:
            explicit PartitionTable(PartitionReader& reader) : reader_(reader) {
            }

            bool read(os::Storage::Part* partitions, uint32_t capacity,
                      uint32_t& count);
    };

}  // namespace storage
