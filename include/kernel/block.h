#ifndef INFO_KERNEL_BLOCK_H_
#define INFO_KERNEL_BLOCK_H_

#include <cstdint>
#include <cstring>

class Block;

namespace block {

// Device type definitions
enum DeviceType {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_IDE     = 1,
    DEVICE_TYPE_SCSI    = 2,
    DEVICE_TYPE_VIRTIO  = 3,
    DEVICE_TYPE_ATA     = 4
};

// Block device management
int Proc(int argc, char *argv[]);
Block *FindDevice(const char *name);

}  // namespace block

class Block {
   public:
    // Block device operations
    int Read(std::uint64_t sector, std::uint32_t count, void *buf);
    int Write(std::uint64_t sector, std::uint32_t count, const void *buf);
    int Ioctl(std::uint32_t cmd, void *arg);
    int RegisterDevice();

    // Getters
    const char *GetName() const { return name; }
    std::uint64_t GetSectorCount() const { return sector_count; }
    std::uint16_t GetSectorSize() const { return sector_size; }
    enum block::DeviceType GetType() const { return type; }

    // Setters
    void SetName(const char *n) {
        strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    void SetSectorCount(std::uint64_t count) { sector_count = count; }
    void SetSectorSize(std::uint16_t size) { sector_size = size; }
    void SetType(enum block::DeviceType t) { type = t; }

    Block *next;  // Next device in list

   protected:
    char name[16];                // Device name
    std::uint64_t sector_count;   // Total sectors
    std::uint16_t sector_size;    // Sector size (bytes)
    enum block::DeviceType type;  // Device type for switch-based dispatch
};

#endif  // INFO_KERNEL_BLOCK_H_