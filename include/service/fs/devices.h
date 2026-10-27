#pragma once

// The catalog capacity belongs to the shared storage service contract.
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include "service/fs/block.h"
#include "service/fs/format.h"
#include <os/storage.h>

namespace vfs::server {

struct DeviceRecord {
    os::wire::StorageDeviceIdentity identity{};
    Format format = Format::Unknown;
    char uuid[40]{};
    char label[40]{};
    char opticalName[16]{};
};

class DeviceCatalog {
   public:
    static constexpr int missing   = -1;
    static constexpr int ambiguous = -2;
    bool add(const os::wire::StorageDeviceIdentity& identity,
             SectorDevice& device);
    int resolve(const char* source) const;
    const DeviceRecord* get(uint32_t index) const;
    uint32_t count() const { return count_; }
    int list(const char* directory, uint32_t index,
             DirectoryEntry& entry) const;

   private:
    DeviceRecord records_[os::maximumStorageDevices]{};
    uint32_t count_        = 0;
    uint32_t opticalCount_ = 0;
};

}  // namespace vfs::server
