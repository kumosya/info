#ifndef _OS_STORAGE_H
#define _OS_STORAGE_H

#include <stdint.h>

namespace os {

enum class BlockDeviceType : uint32_t {
    VirtioBlock = 1,
    Ahci,
    Nvme,
    UsbStorage
};

enum class StorageError : int64_t {
    Success         = 0,
    DeviceNotFound  = -1,
    DeviceOffline   = -2,
    InvalidBlock    = -3,
    Io              = -4,
    Timeout         = -5,
    ReadOnly        = -6,
    NoSpace         = -7,
    Dma             = -8,
    Corrupted       = -9,
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
    uint32_t number;
    char uuid[40];
};

constexpr uint32_t maximumStorageDevices = 32;
constexpr uint32_t maximumStorageTransferBlocks = 7;
constexpr uint32_t storageIdentityWord   = 6;

// Device numbers identify live nodes; PARTUUID identifies on-disk partitions.
struct StorageDeviceIdentity {
    uint32_t id;
    uint32_t major;
    uint32_t minor;
    uint32_t parentId;
    uint32_t partitionNumber;
    uint32_t reserved;
    uint64_t startBlock;
    char name[32];
    char partitionUuid[40];
    char hardwarePath[64];
};

constexpr uint64_t deviceNumber(uint32_t major, uint32_t minor) {
    return (static_cast<uint64_t>(major) << 32) | minor;
}

}  // namespace os

#endif
