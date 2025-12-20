#ifndef _IDE_H_
#define _IDE_H_

#include "block.h"

namespace ide {

// IDE端口定义
#define IDE_PRIMARY_IO 0x1F0
#define IDE_SECONDARY_IO 0x170

#define IDE_PRIMARY_CONTROL 0x3F6
#define IDE_SECONDARY_CONTROL 0x376

// IDE寄存器定义
#define IDE_DATA 0
#define IDE_ERROR 1
#define IDE_FEATURES 1
#define IDE_SECTOR_COUNT 2
#define IDE_LBA_LOW 3
#define IDE_LBA_MID 4
#define IDE_LBA_HIGH 5
#define IDE_DEVICE 6
#define IDE_COMMAND 7
#define IDE_STATUS 7

// IDE命令定义
#define IDE_CMD_READ_PIO 0x20
#define IDE_CMD_WRITE_PIO 0x30
#define IDE_CMD_IDENTIFY 0xEC

// IDE状态位定义
#define IDE_STATUS_BSY 0x80
#define IDE_STATUS_DRDY 0x40
#define IDE_STATUS_DF 0x20
#define IDE_STATUS_ERR 0x01
#define IDE_STATUS_DRQ 0x08

struct Device {
    block::Device block_dev;
    uint16_t io_base;
    uint8_t irq;
    uint8_t drive;
    uint8_t status;
    uint64_t lba_address;
};

// IDE device operations
int read(block::Device *dev, uint64_t sector, uint32_t count, void *buf);
int write(block::Device *dev, uint64_t sector, uint32_t count, const void *buf);
int ioctl(block::Device *dev, uint32_t cmd, void *arg);

// IDE driver initialization
void init();

} // namespace ide

#endif // _IDE_H_