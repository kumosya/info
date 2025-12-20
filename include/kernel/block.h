#ifndef _BLOCK_H_
#define _BLOCK_H_

#include <cstdint>
using namespace std;

namespace block {

// Device type definitions
enum DeviceType { DEVICE_TYPE_UNKNOWN = 0, DEVICE_TYPE_IDE = 1, DEVICE_TYPE_SCSI = 2, DEVICE_TYPE_VIRTIO = 3, DEVICE_TYPE_ATA = 4 };

struct Device {
    char name[16];         // Device name
    uint64_t sector_count; // Total sectors
    uint16_t sector_size;  // Sector size (bytes)
    enum DeviceType type;  // Device type for switch-based dispatch
    Device *next;     // Next device in list
};

// Block device operations
int read(Device *dev, uint64_t sector, uint32_t count, void *buf);
int write(Device *dev, uint64_t sector, uint32_t count, const void *buf);
int ioctl(Device *dev, uint32_t cmd, void *arg);

// Block device management
void init();
int register_device(Device *dev);
Device *find_device(const char *name);

} // namespace block

#endif // _BLOCK_H_