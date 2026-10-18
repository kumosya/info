#include "../format.h"

namespace vfs {

uint64_t FormatProbe::fatBlockOffset(uint32_t cluster, uint32_t sectorsPerCluster,
                        uint32_t firstDataSector, uint32_t bytesPerSector) {
    if (cluster < 2) return 0;
    return static_cast<uint64_t>(firstDataSector +
                                 (cluster - 2) * sectorsPerCluster) *
           bytesPerSector;
}

}  // namespace vfs
