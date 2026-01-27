
#include "kernel/block.h"

#include <cstring>
#include <fcntl.h>

#include "kernel/ide.h"
#include "kernel/io.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

namespace block {

BlockDevice *device_list = nullptr;

int Service(int argc, char *argv[]) {
    device_list = nullptr;
    bool reply;

    ide::Init();

    task::ipc::Message msg;
    while (true) {
        reply = true;

        if (task::ipc::Receive(&msg)) {
            switch (msg.type) {
                case SYS_BLOCK_GET:
                    // tty::printk("Block: Request for device %s\n", msg.data);
                    msg.num[0] = reinterpret_cast<uint64_t>(
                        FindDevice(reinterpret_cast<const char *>(msg.data)));
                    reply = true;
                    break;
                default:
                    tty::printk("Block: Unknown message type: %d\n", msg.type);
                    reply = false;
                    break;
            }
            if (reply) {
                msg.dst_pid = msg.sender->pid;
                msg.sender  = task::current_proc;
                task::ipc::Send(&msg);
            }
        }
    }

    return 1;
}

BlockDevice *FindDevice(const char *name) {
    BlockDevice *dev = device_list;
    while (dev) {
        if (strcmp(dev->disk_name, name) == 0) {
            return dev;
        }
        dev = dev->next;
    }
    return nullptr;
}

void RegisterDevice(BlockDevice *dev) {
    if (!dev) return;

    if (dev->sector_size == 0) {
        dev->sector_size = 512;
    }

    dev->next   = device_list;
    device_list = dev;

    // tty::printk("Block: Registered device %s with %lu sectors\n",
    //            dev->disk_name, dev->capacity);
}

int submit_bio(Bio *bio) {
    if (!bio || !bio->buf) {
        return -1;
    }

    return 0;
}

void blk_queue_make_request(RequestQueue *q, void (*fn)(Request *)) {
    if (q) {
        q->set_request_fn(fn);
    }
}

void add_partition(BlockDevice *dev, std::uint64_t start, std::uint64_t num,
                   int minor) {
    if (!dev || num == 0) return;

    HdStruct *part = new HdStruct(start, num, dev->disk_name);
    snprintf(part->name, sizeof(part->name), "%s%d", dev->disk_name, minor);

    part->next      = dev->partitions;
    dev->partitions = part;
    dev->part_count++;

    // tty::printk("Block: Added partition %s: start=%lu, size=%lu\n",
    //            part->name, start, num);
}

int IDEBlockDevice::Read(std::uint64_t sector, std::uint32_t count, void *buf) {
    return ide::IDEDeviceRead(this, io_base_, drive_, sector, count, buf);
}

int IDEBlockDevice::Write(std::uint64_t sector, std::uint32_t count,
                          const void *buf) {
    return ide::IDEDeviceWrite(this, io_base_, drive_, sector, count, buf);
}

int IDEBlockDevice::Ioctl(std::uint32_t cmd, void *arg) { return -1; }

}  // namespace block
