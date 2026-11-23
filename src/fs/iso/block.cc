#include "service/fs/iso/filesystem.h"
#include "service/fs/mount.h"

namespace fs {

    uint64_t iso::Probe::blockOffset(uint32_t block) {
        return static_cast<uint64_t>(block) * 2048;
    }

}  // namespace fs
