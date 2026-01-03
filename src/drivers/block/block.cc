

#include <cstring>

#include "kernel/ide.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"
#include "kernel/block.h"

namespace block {

static Block *device_list = nullptr;

int Proc(int argc, char *argv[]) {
    device_list = nullptr;
    tty::printf("Block device driver initialized.\n");

    ide::Init();
    while (true) {
    }

    return 1;
}

Block *FindDevice(const char *name) {
    Block *dev = device_list;
    while (dev) {
        if (strcmp(dev->GetName(), name) == 0) {
            return dev;
        }
        dev = dev->next;
    }
    return nullptr;
}

}  // namespace block

int Block::RegisterDevice() {
    // Set default sector size if not specified
    if (sector_size == 0) {
        sector_size = 512;
    }

    // Add device to list
    next               = block::device_list;
    block::device_list = this;

    return 0;
}

// Read sectors from block device
int Block::Read(std::uint64_t sector, std::uint32_t count, void *buf) {
    if (!buf) {
        return -1;
    }

    switch (type) {
        case block::DEVICE_TYPE_IDE:
            return ide::Read(this, sector, count, buf);
        default:
            return -2;  // Unsupported device type
    }
}

// Write sectors to block device
int Block::Write(std::uint64_t sector, std::uint32_t count, const void *buf) {
    if (!buf) {
        return -1;
    }

    switch (type) {
        case block::DEVICE_TYPE_IDE:
            return ide::Write(this, sector, count, buf);
        default:
            return -2;  // Unsupported device type
    }
}

// Control device with ioctl
int Block::Ioctl(std::uint32_t cmd, void *arg) {
    if (!arg) {
        return -1;
    }

    switch (type) {
        case block::DEVICE_TYPE_IDE:
            return ide::Ioctl(this, cmd, arg);
        default:
            return -2;  // Unsupported device type
    }
}
