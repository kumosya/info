#ifndef INFO_KERNEL_IDE_H_
#define INFO_KERNEL_IDE_H_

#include <cstdint>

#include "kernel/block.h"

namespace ide {

#define IDE_TIMEOUT 1000

#define IDE_PRIMARY_IO 0x1F0
#define IDE_SECONDARY_IO 0x170

#define IDE_PRIMARY_CONTROL 0x3F6
#define IDE_SECONDARY_CONTROL 0x376

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

#define IDE_CMD_READ_PIO 0x20
#define IDE_CMD_WRITE_PIO 0x30
#define IDE_CMD_IDENTIFY 0xEC

#define IDE_STATUS_BSY 0x80
#define IDE_STATUS_DRDY 0x40
#define IDE_STATUS_DF 0x20
#define IDE_STATUS_ERR 0x01
#define IDE_STATUS_DRQ 0x08

void Init();
int IDEDeviceRead(block::IDEBlockDevice *dev, std::uint16_t io_base,
                  std::uint8_t drive, std::uint64_t sector, std::uint32_t count,
                  void *buf);
int IDEDeviceWrite(block::IDEBlockDevice *dev, std::uint16_t io_base,
                   std::uint8_t drive, std::uint64_t sector,
                   std::uint32_t count, const void *buf);

}  // namespace ide

#endif  // INFO_KERNEL_IDE_H_
