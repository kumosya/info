#pragma once

#include <stdint.h>

namespace os {

enum class BlockDeviceType : uint32_t {
    VirtioBlock = 1,
    Ahci,
    Nvme,
    UsbStorage
};

enum class StorageError : int64_t {
    Success = 0,
    DeviceNotFound = -1,
    DeviceOffline = -2,
    InvalidBlock = -3,
    Io = -4,
    Timeout = -5,
    ReadOnly = -6,
    NoSpace = -7,
    Dma = -8,
    Corrupted = -9,
    InvalidArgument = -10
};

struct BlockDeviceInfo {
    uint64_t blockCount;
    uint32_t blockSize;
    uint32_t maxTransferBlocks;
    BlockDeviceType type;
    bool readOnly;
};

struct PartitionInfo {
    uint64_t startBlock;
    uint64_t blockCount;
    uint32_t type;
};

}  // namespace os
