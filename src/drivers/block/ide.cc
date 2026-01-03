

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kernel/block.h"
#include "kernel/ide.h"
#include "kernel/io.h"
#include "kernel/tty.h"

namespace ide {

static Device devices[4];  // 支持最多4个IDE设备
static int device_count = 0;

static void WaitReady(std::uint16_t io_base) {
    for (int i = 0; i < 10000; i++) {
        std::uint8_t status = inb(io_base + IDE_STATUS);
        if (!(status & IDE_STATUS_BSY) && (status & IDE_STATUS_DRDY)) {
            return;
        }
    }
}

static void WaitIrq() {
    // 简单的忙等待，实际应该使用中断
    for (int i = 0; i < 1000000; i++) {
        // 空循环
    }
}

static int Identify(std::uint16_t io_base, std::uint8_t drive,
                    std::uint16_t *buf) {
    outb(io_base + IDE_DEVICE, 0xA0 | (drive << 4));
    outb(io_base + IDE_SECTOR_COUNT, 0);
    outb(io_base + IDE_LBA_LOW, 0);
    outb(io_base + IDE_LBA_MID, 0);
    outb(io_base + IDE_LBA_HIGH, 0);
    outb(io_base + IDE_COMMAND, IDE_CMD_IDENTIFY);

    std::uint8_t status = inb(io_base + IDE_STATUS);
    if (status == 0) {
        return -1;  // 没有设备
    }

    WaitReady(io_base);

    status = inb(io_base + IDE_STATUS);
    if (status & IDE_STATUS_ERR) {
        return -2;  // 出错
    }

    // 读取识别数据
    for (int i = 0; i < 256; i++) {
        buf[i] = inw(io_base + IDE_DATA);
    }

    return 0;
}

// IDE device read function
int Read(Block *dev, std::uint64_t sector, std::uint32_t count, void *buf) {
    Device *ide_dev       = (Device *)dev;
    std::uint16_t *data   = (std::uint16_t *)buf;
    std::uint16_t io_base = ide_dev->io_base;
    std::uint8_t drive    = ide_dev->drive;
    WaitReady(io_base);

    for (std::uint32_t i = 0; i < count; i++) {
        // 设置LBA模式和驱动器
        outb(io_base + IDE_DEVICE,
             0xE0 | (drive << 4) | (((sector + i) >> 24) & 0x0F));
        outb(io_base + IDE_ERROR, 0);
        outb(io_base + IDE_SECTOR_COUNT, 1);
        outb(io_base + IDE_LBA_LOW, (sector + i) & 0xFF);
        outb(io_base + IDE_LBA_MID, ((sector + i) >> 8) & 0xFF);
        outb(io_base + IDE_LBA_HIGH, ((sector + i) >> 16) & 0xFF);
        outb(io_base + IDE_COMMAND, IDE_CMD_READ_PIO);

        // 等待设备就绪且数据已准备好传输
        int timeout = 0;
        while (timeout < 10000) {
            std::uint8_t status = inb(io_base + IDE_STATUS);
            if (!(status & IDE_STATUS_BSY) && (status & IDE_STATUS_DRDY) &&
                (status & IDE_STATUS_DRQ)) {
                break;
            }
            timeout++;
        }

        // 读取一个扇区的数据
        for (int j = 0; j < 256; j++) {
            *data++ = inw(io_base + IDE_DATA);
        }
    }

    return 0;
}

// IDE device write function
int Write(Block *dev, std::uint64_t sector, std::uint32_t count,
          const void *buf) {
    Device *ide_dev           = (Device *)dev;
    const std::uint16_t *data = (const std::uint16_t *)buf;
    std::uint16_t io_base     = ide_dev->io_base;
    std::uint8_t drive        = ide_dev->drive;

    WaitReady(io_base);

    for (std::uint32_t i = 0; i < count; i++) {
        // 设置LBA模式和驱动器
        outb(io_base + IDE_DEVICE,
             0xE0 | (drive << 4) | (((sector + i) >> 24) & 0x0F));
        outb(io_base + IDE_ERROR, 0);
        outb(io_base + IDE_SECTOR_COUNT, 1);
        outb(io_base + IDE_LBA_LOW, (sector + i) & 0xFF);
        outb(io_base + IDE_LBA_MID, ((sector + i) >> 8) & 0xFF);
        outb(io_base + IDE_LBA_HIGH, ((sector + i) >> 16) & 0xFF);
        outb(io_base + IDE_COMMAND, IDE_CMD_WRITE_PIO);

        WaitReady(io_base);

        // 写入一个扇区的数据
        for (int j = 0; j < 256; j++) {
            outw(io_base + IDE_DATA, *data++);
        }

        // 等待写入完成
        WaitIrq();
    }

    return 0;
}

// IDE device ioctl function
int Ioctl(Block *dev, std::uint32_t cmd, void *arg) {
    // 暂时没有实现
    return -1;
}

static void DetectDevices(std::uint16_t io_base, std::uint8_t irq) {
    for (std::uint8_t drive = 0; drive < 2; drive++) {
        std::uint16_t identify_data[256];
        int ret = Identify(io_base, drive, identify_data);

        if (ret == 0) {
            Device *ide_dev = &devices[device_count++];
            Block *dev      = &ide_dev->block_dev;

            // 初始化设备信息
            ide_dev->io_base = io_base;
            ide_dev->irq     = irq;
            ide_dev->drive   = drive;
            ide_dev->status  = 0;

            // 设置块设备信息
            char s[16];
            snprintf(s, 16, "hd%c", 'a' + (device_count - 1));
            dev->SetName(s);

            // 从识别数据中获取扇区数和扇区大小
            dev->SetSectorSize(512);
            dev->SetType(block::DEVICE_TYPE_IDE);

            // 检查是否支持LBA48
            if (identify_data[83] & (1 << 10)) {
                // 使用LBA48模式，扇区数是64位
                std::uint64_t sectors =
                    ((std::uint64_t)identify_data[103] << 48) |
                    ((std::uint64_t)identify_data[102] << 32) |
                    ((std::uint64_t)identify_data[101] << 16) |
                    (std::uint64_t)identify_data[100];
                dev->SetSectorCount(sectors);
            } else {
                // 使用LBA28模式，扇区数是32位
                std::uint32_t sectors =
                    ((std::uint32_t)identify_data[61] << 16) |
                    identify_data[60];
                dev->SetSectorCount(sectors);
            }

            // 注册块设备
            dev->RegisterDevice();
        }
    }
}

void Init() {
    // 检测IDE设备
    DetectDevices(IDE_PRIMARY_IO, 14);
    DetectDevices(IDE_SECONDARY_IO, 15);
}

}  // namespace ide