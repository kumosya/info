#include "fs/ext2.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "block.h"
#include "tty.h"
#include "vfs.h"

namespace ext2 {


int Lseek(File *file, std::uint64_t offset, int whence) {
    if (!file) {
        return -1;
    }

    std::uint64_t new_offset = file->offset;

    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset += offset;
            break;
        case SEEK_END:
            new_offset = file->inode.i_size + offset;
            break;
        default:
            return -1;
    }

    // Check for overflow
    if (new_offset > (uint64_t)file->inode.i_size && !(file->flags & (O_WRONLY | O_RDWR))) {
        return -1;
    }

    file->offset = new_offset;
    return new_offset;
}

}