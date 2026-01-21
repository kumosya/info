
#include <cstring>
#include <cstdio>
#include <fcntl.h>

#include "kernel/ide.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/block.h"
#include "kernel/io.h"


namespace ide {

class IDEDeviceController {
 public:
    IDEDeviceController(std::uint16_t io, std::uint8_t drv, block::IDEBlockDevice *dev)
        : io_base(io), drive(drv), status(0), lba_address(0), ide_dev(dev) {}

    std::uint16_t io_base;
    std::uint8_t drive;
    std::uint8_t status;
    std::uint64_t lba_address;
    block::IDEBlockDevice *ide_dev;
};

static task::SpinLock ide_lock;
static IDEDeviceController *devices[4];
static int device_count = 0;

static void WaitReady(std::uint16_t io_base) {
    int t = timer::GetTicks();
    while ((timer::GetTicks() - t) * 1000 / TIMER_FREQUENCY < IDE_TIMEOUT) {
        std::uint8_t status = inb(io_base + IDE_STATUS);
        if (!(status & IDE_STATUS_BSY) && (status & IDE_STATUS_DRDY)) {
            return;
        }
    }
}

static void WaitDrq(std::uint16_t io_base) {
    int t = timer::GetTicks();
    while ((timer::GetTicks() - t) * 1000 / TIMER_FREQUENCY < IDE_TIMEOUT) {
        std::uint8_t status = inb(io_base + IDE_STATUS);
        if (status & IDE_STATUS_DRQ) {
            return;
        }
        //task::ipc::Send(&msg);
    }
}

static void WaitNotBusy(std::uint16_t io_base) {
    int t = timer::GetTicks();
    while ((timer::GetTicks() - t) * 1000 / TIMER_FREQUENCY < IDE_TIMEOUT) {
        std::uint8_t status = inb(io_base + IDE_STATUS);
        if (!(status & IDE_STATUS_BSY)) {
            return;
        }
        //task::ipc::Send(&msg);
    }
}

static int Identify(std::uint16_t io_base, std::uint8_t drive, std::uint16_t *buf) {
    ide_lock.lock();

    outb(io_base + IDE_DEVICE, 0xA0 | (drive << 4));
    outb(io_base + IDE_SECTOR_COUNT, 0);
    outb(io_base + IDE_LBA_LOW, 0);
    outb(io_base + IDE_LBA_MID, 0);
    outb(io_base + IDE_LBA_HIGH, 0);
    outb(io_base + IDE_COMMAND, IDE_CMD_IDENTIFY);

    std::uint8_t status = inb(io_base + IDE_STATUS);
    if (status == 0) {
        ide_lock.unlock();
        return -1;
    }

    WaitReady(io_base);

    status = inb(io_base + IDE_STATUS);
    if (status & IDE_STATUS_ERR) {
        ide_lock.unlock();
        return -2;
    }

    for (int i = 0; i < 256; i++) {
        buf[i] = inw(io_base + IDE_DATA);
    }
    ide_lock.unlock();

    return 0;
}

static void IDEProcessRequest(block::Request *req) {
    if (!req) return;

    if (device_count > 0) {
        IDEDeviceController *ide_dev = devices[0];

        if (req->rw == block::REQ_TYPE_READ) {
            IDEDeviceRead(ide_dev->ide_dev, ide_dev->io_base, ide_dev->drive,
                         req->sector, req->count, req->buf);
        } else if (req->rw == block::REQ_TYPE_WRITE) {
            IDEDeviceWrite(ide_dev->ide_dev, ide_dev->io_base, ide_dev->drive,
                          req->sector, req->count, req->buf);
        }
    }

    delete req;
}

int IDEDeviceRead(block::IDEBlockDevice *dev, std::uint16_t io_base,
                  std::uint8_t drive, std::uint64_t sector,
                  std::uint32_t count, void *buf) {
    ide_lock.lock();
                    
    std::uint16_t *data = (std::uint16_t *)buf;
    WaitReady(io_base);
    
    for (std::uint32_t i = 0; i < count; i++) {
        outb(io_base + IDE_DEVICE,
             0xE0 | (drive << 4) | (((sector + i) >> 24) & 0x0F));
        outb(io_base + IDE_ERROR, 0);
        outb(io_base + IDE_SECTOR_COUNT, 1);
        outb(io_base + IDE_LBA_LOW, (sector + i) & 0xFF);
        outb(io_base + IDE_LBA_MID, ((sector + i) >> 8) & 0xFF);
        outb(io_base + IDE_LBA_HIGH, ((sector + i) >> 16) & 0xFF);
        outb(io_base + IDE_COMMAND, IDE_CMD_READ_PIO);

        WaitDrq(io_base);

        for (int j = 0; j < 256; j++) {
            *data++ = inw(io_base + IDE_DATA);
        }
    }
    ide_lock.unlock();

    return 0;
}

int IDEDeviceWrite(block::IDEBlockDevice *dev, std::uint16_t io_base,
                   std::uint8_t drive, std::uint64_t sector,
                   std::uint32_t count, const void *buf) {
    ide_lock.lock();

    const std::uint16_t *data = (const std::uint16_t *)buf;
    WaitReady(io_base);

    for (std::uint32_t i = 0; i < count; i++) {
        outb(io_base + IDE_DEVICE,
             0xE0 | (drive << 4) | (((sector + i) >> 24) & 0x0F));
        outb(io_base + IDE_ERROR, 0);
        outb(io_base + IDE_SECTOR_COUNT, 1);
        outb(io_base + IDE_LBA_LOW, (sector + i) & 0xFF);
        outb(io_base + IDE_LBA_MID, ((sector + i) >> 8) & 0xFF);
        outb(io_base + IDE_LBA_HIGH, ((sector + i) >> 16) & 0xFF);
        outb(io_base + IDE_COMMAND, IDE_CMD_WRITE_PIO);

        WaitReady(io_base);

        for (int j = 0; j < 256; j++) {
            outw(io_base + IDE_DATA, *data++);
        }

        WaitReady(io_base);
    }
    ide_lock.unlock();

    return 0;
}

static void DetectDevices(std::uint16_t io_base, std::uint8_t irq) {
    ide_lock.lock();

    for (std::uint8_t drive = 0; drive < 2; drive++) {
        std::uint16_t identify_data[256];
        int ret = Identify(io_base, drive, identify_data);

        if (ret == 0) {
            char device_name[16];
            snprintf(device_name, 16, "hd%c", 'a' + device_count);

            block::IDEBlockDevice *blk_dev =
                new block::IDEBlockDevice(device_name, io_base, drive);

            devices[device_count] = new IDEDeviceController(io_base, drive, blk_dev);
            device_count++;

            std::uint64_t sectors = 0;
            if (identify_data[83] & (1 << 10)) {
                sectors =
                    ((std::uint64_t)identify_data[103] << 48) |
                    ((std::uint64_t)identify_data[102] << 32) |
                    ((std::uint64_t)identify_data[101] << 16) |
                    (std::uint64_t)identify_data[100];
            } else {
                sectors =
                    ((std::uint32_t)identify_data[61] << 16) |
                    identify_data[60];
            }

            blk_dev->capacity = sectors;
            blk_dev->queue->set_request_fn(IDEProcessRequest);

            block::RegisterDevice(blk_dev);

            block::add_partition(blk_dev, 0, sectors, 1);

            //tty::printk("IDE: Detected device %s with %lu sectors\n",
            //           device_name, sectors);
        }
    }
    ide_lock.unlock();
}

void Init() {
    DetectDevices(IDE_PRIMARY_IO, 14);
    DetectDevices(IDE_SECONDARY_IO, 15);
}

}  // namespace ide
