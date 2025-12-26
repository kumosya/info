#include "block.h"

#include <cstring>
#include <stddef.h>

#include "ide.h"
#include "tty.h"
#include "vfs.h"
#include "task.h"

namespace block {

static Device *device_list = nullptr;

char *argv_vfs[10] = {"vfs", NULL};
int Proc(int argc, char *argv[]) {
    device_list = nullptr;
    tty::printf("Block device driver initialized.\n");
    
    ide::Init();
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(vfs::Proc), argv_vfs, 0);
    while (true) {
        for (int i = 0; i < 0xffffff; i++);
        tty::printf("B");
    }

    return 1;
}

int RegisterDevice(Device *dev) {
    if (!dev) {
        return -1;
    }

    // Set default sector size if not specified
    if (dev->sector_size == 0) {
        dev->sector_size = 512;
    }

    // Add device to list
    dev->next   = device_list;
    device_list = dev;

    return 0;
}

Device *FindDevice(const char *name) {
    Device *dev = device_list;
    while (dev) {
        if (strcmp(dev->name, name) == 0) {
            return dev;
        }
        dev = dev->next;
    }
    return nullptr;
}

// Read sectors from block device
int Read(Device *dev, std::uint64_t sector, std::uint32_t count, void *buf) {
    if (!dev || !buf) {
        return -1;
    }

    switch (dev->type) {
        case DEVICE_TYPE_IDE:
            return ide::Read(dev, sector, count, buf);
        default:
            return -2;  // Unsupported device type
    }
}

// Write sectors to block device
int Write(Device *dev, std::uint64_t sector, std::uint32_t count, const void *buf) {
    if (!dev || !buf) {
        return -1;
    }

    switch (dev->type) {
        case DEVICE_TYPE_IDE:
            return ide::Write(dev, sector, count, buf);
        default:
            return -2;  // Unsupported device type
    }
}

// Control device with ioctl
int Ioctl(Device *dev, std::uint32_t cmd, void *arg) {
    if (!dev) {
        return -1;
    }

    switch (dev->type) {
        case DEVICE_TYPE_IDE:
            return ide::Ioctl(dev, cmd, arg);
        default:
            return -2;  // Unsupported device type
    }
}

}  // namespace block