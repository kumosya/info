#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "kernel/block.h"
#include "kernel/fs/ext2.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

namespace ext2 {

std::int64_t Lseek(block::BlockDevice *dev, Ext2SuperBlock *sb,
                   Ext2Inode *inode, std::int64_t offset, int whence) {
    if (!dev || !sb || !inode) {
        return -1;
    }

    std::int64_t new_offset;
    std::uint64_t file_size = inode->i_size;

    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = offset;
            break;
        case SEEK_END:
            new_offset = file_size + offset;
            break;
        default:
            return -2;
    }

    if (new_offset < 0) {
        return -3;
    }

    return new_offset;
}

int Truncate(block::BlockDevice *dev, Ext2SuperBlock *sb,
             std::uint32_t inode_num, std::uint64_t new_size,
             std::uint32_t ext2_lba) {
    if (!dev || !sb || inode_num == 0) {
        return -1;
    }

    Ext2Inode inode;
    if (ReadInode(dev, sb, inode_num, &inode, ext2_lba) != 0) {
        return -2;
    }

    std::uint64_t old_size = inode.i_size;

    if (new_size == old_size) {
        return 0;
    }

    if (new_size < old_size) {
        std::uint32_t block_size = 1024 << sb->s_log_block_size;
        std::uint32_t old_blocks = (old_size + block_size - 1) / block_size;
        std::uint32_t new_blocks = (new_size + block_size - 1) / block_size;

        for (std::uint32_t i = new_blocks; i < old_blocks; i++) {
            std::uint32_t block_num = GetBlockNum(&inode, i, sb, dev, ext2_lba);
            if (block_num != 0) {
                FreeBlock(dev, sb, block_num, ext2_lba);
                SetBlockNum(&inode, i, 0, sb, dev, ext2_lba);
            }
        }

        inode.i_size = new_size;
    } else {
        inode.i_size = new_size;
    }

    if (WriteInode(dev, sb, inode_num, &inode, ext2_lba) != 0) {
        return -3;
    }

    return 0;
}

int GetFileSize(block::BlockDevice *dev, Ext2SuperBlock *sb, const char *path,
                std::uint64_t *size, std::uint32_t ext2_lba) {
    if (!dev || !sb || !path || !size) {
        return -1;
    }

    std::uint32_t inode_num;
    if (LookupPath(dev, sb, path, &inode_num, ext2_lba) != 0) {
        return -2;
    }

    Ext2Inode inode;
    if (ReadInode(dev, sb, inode_num, &inode, ext2_lba) != 0) {
        return -3;
    }

    *size = inode.i_size;
    return 0;
}

}  // namespace ext2
