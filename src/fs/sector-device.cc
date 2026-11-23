/* 服务对象的成员函数实现。 */
#include "service/fs/vfs.h"

namespace fs {

    /* 通过逐扇区接口组合连续读取。 */
    bool SectorDevice::readSectors(uint64_t firstSector, uint32_t count,
                                   void* buffer) {
        for (uint32_t index = 0; index < count; ++index) {
            if (!readSector(firstSector + index,
                            static_cast<uint8_t*>(buffer) + index * 512)) {
                return false;
            }
        }

        return true;
    }

    /* 通过逐扇区接口组合连续写入。 */
    bool SectorDevice::writeSectors(uint64_t firstSector, uint32_t count,
                                    const void* buffer) {
        for (uint32_t index = 0; index < count; ++index) {
            if (!writeSector(
                    firstSector + index,
                    static_cast<const uint8_t*>(buffer) + index * 512)) {
                return false;
            }
        }

        return true;
    }

}  // namespace fs
