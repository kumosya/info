#ifndef _BLOCK_H_
#define _BLOCK_H_

#include <cstdint>

namespace block {

// Device type definitions
enum DeviceType {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_IDE     = 1,
    DEVICE_TYPE_SCSI    = 2,
    DEVICE_TYPE_VIRTIO  = 3,
    DEVICE_TYPE_ATA     = 4
};

struct Device {
    char name[16];               // Device name
    std::uint64_t sector_count;  // Total sectors
    std::uint16_t sector_size;   // Sector size (bytes)
    enum DeviceType type;        // Device type for switch-based dispatch
    Device *next;                // Next device in list
};

// Block device operations
int Read(Device *dev, std::uint64_t sector, std::uint32_t count, void *buf);
int Write(Device *dev, std::uint64_t sector, std::uint32_t count,
          const void *buf);
int Ioctl(Device *dev, std::uint32_t cmd, void *arg);

// Block device management
int Proc(int argc, char *argv[]);
int RegisterDevice(Device *dev);
Device *FindDevice(const char *name);

}  // namespace block

#endif  // _BLOCK_H_