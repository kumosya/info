#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/fs/ext2.h"
#include "kernel/tty.h"

namespace ext2 {

ssize_t ReadFile(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *inode,
                 void *buf, size_t count, std::uint64_t offset,
                 std::uint32_t ext2_lba) {
    if (!dev || !sb || !inode || !buf) {
        return -1;
    }

    if (inode->i_mode & EXT2_S_IFDIR) {
        return -2;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint64_t file_size  = inode->i_size;

    if (offset >= file_size) {
        return 0;
    }

    std::uint64_t bytes_to_read = count;
    if (offset + bytes_to_read > file_size) {
        bytes_to_read = file_size - offset;
    }

    std::uint8_t *out_buf    = (std::uint8_t *)buf;
    std::uint64_t bytes_read = 0;

    while (bytes_read < bytes_to_read) {
        std::uint64_t file_offset  = offset + bytes_read;
        std::uint32_t block_index  = file_offset / block_size;
        std::uint32_t block_offset = file_offset % block_size;

        std::uint32_t block_num =
            GetBlockNum(inode, block_index, sb, dev, ext2_lba);
        if (block_num == 0) {
            break;
        }

        std::uint8_t *block_buf = new std::uint8_t[block_size];
        if (ReadBlock(dev, sb, block_num, block_buf, ext2_lba) != 0) {
            delete[] block_buf;
            break;
        }

        std::uint32_t bytes_in_block  = block_size - block_offset;
        std::uint64_t bytes_remaining = bytes_to_read - bytes_read;

        if (bytes_in_block > bytes_remaining) {
            bytes_in_block = bytes_remaining;
        }

        memcpy(out_buf + bytes_read, block_buf + block_offset, bytes_in_block);
        bytes_read += bytes_in_block;

        delete[] block_buf;
    }

    return bytes_read;
}

ssize_t WriteFile(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *inode,
                  const void *buf, size_t count, std::uint64_t offset,
                  std::uint32_t ext2_lba) {
    if (!dev || !sb || !inode || !buf) {
        return -1;
    }

    if (inode->i_mode & EXT2_S_IFDIR) {
        return -2;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint64_t file_size  = inode->i_size;

    std::uint64_t bytes_to_write = count;
    std::uint64_t new_size       = offset + bytes_to_write;

    if (new_size > file_size) {
        inode->i_size = new_size;
    }

    const std::uint8_t *in_buf  = (const std::uint8_t *)buf;
    std::uint64_t bytes_written = 0;

    while (bytes_written < bytes_to_write) {
        std::uint64_t file_offset  = offset + bytes_written;
        std::uint32_t block_index  = file_offset / block_size;
        std::uint32_t block_offset = file_offset % block_size;

        std::uint32_t block_num =
            GetBlockNum(inode, block_index, sb, dev, ext2_lba);
        if (block_num == 0) {
            block_num = AllocBlock(dev, sb, ext2_lba);
            if (block_num == 0) {
                break;
            }
            SetBlockNum(inode, block_index, block_num, sb, dev, ext2_lba);
        }

        std::uint8_t *block_buf = new std::uint8_t[block_size];
        if (ReadBlock(dev, sb, block_num, block_buf, ext2_lba) != 0) {
            delete[] block_buf;
            break;
        }

        std::uint32_t bytes_in_block  = block_size - block_offset;
        std::uint64_t bytes_remaining = bytes_to_write - bytes_written;

        if (bytes_in_block > bytes_remaining) {
            bytes_in_block = bytes_remaining;
        }

        memcpy(block_buf + block_offset, in_buf + bytes_written,
               bytes_in_block);

        if (WriteBlock(dev, sb, block_num, block_buf, ext2_lba) != 0) {
            delete[] block_buf;
            break;
        }

        bytes_written += bytes_in_block;
        delete[] block_buf;
    }

    return bytes_written;
}

int CreateFile(block::BlockDevice *dev, Ext2SuperBlock *sb, const char *path,
               std::uint16_t mode, std::uint32_t ext2_lba) {
    if (!dev || !sb || !path) {
        return -1;
    }

    char parent_path[256];
    char filename[256];

    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        tty::printk("EXT2: CreateFile - no slash in path '%s'\n", path);
        return -2;
    }

    if (last_slash == path) {
        strcpy(parent_path, "/");
    } else {
        std::uint32_t len = last_slash - path;
        if (len >= sizeof(parent_path)) {
            tty::printk("EXT2: CreateFile - parent path too long\n");
            return -3;
        }
        memcpy(parent_path, path, len);
        parent_path[len] = '\0';
    }

    strcpy(filename, last_slash + 1);
    if (filename[0] == '\0') {
        tty::printk("EXT2: CreateFile - empty filename\n");
        return -4;
    }

    // tty::printk("EXT2: CreateFile - path='%s', parent='%s', filename='%s'\n",
    //             path, parent_path, filename);

    std::uint32_t parent_inode_num;
    if (LookupPath(dev, sb, parent_path, &parent_inode_num, ext2_lba) != 0) {
        tty::printk("EXT2: CreateFile - LookupPath failed for parent '%s'\n",
                    parent_path);
        return -5;
    }
    // tty::printk("EXT2: CreateFile - parent_inode_num=%u\n",
    // parent_inode_num);

    Ext2Inode parent_inode;
    if (ReadInode(dev, sb, parent_inode_num, &parent_inode, ext2_lba) != 0) {
        tty::printk("EXT2: CreateFile - ReadInode failed for inode %u\n",
                    parent_inode_num);
        return -6;
    }
    // tty::printk("EXT2: CreateFile - parent_inode.i_mode=0x%x,
    // EXT2_S_IFDIR=0x%x\n",
    //             parent_inode.i_mode, EXT2_S_IFDIR);

    if (!(parent_inode.i_mode & EXT2_S_IFDIR)) {
        tty::printk("EXT2: CreateFile - parent is not a directory\n");
        return -7;
    }

    Ext2DirEntry entry;
    if (FindEntry(dev, sb, &parent_inode, filename, &entry, ext2_lba) == 0) {
        return -8;
    }

    std::uint32_t new_inode_num = AllocInode(dev, sb, ext2_lba);
    if (new_inode_num == 0) {
        return -9;
    }

    Ext2Inode new_inode;
    memset(&new_inode, 0, sizeof(Ext2Inode));
    new_inode.i_mode        = mode;
    new_inode.i_uid         = 0;
    new_inode.i_gid         = 0;
    new_inode.i_size        = 0;
    new_inode.i_atime       = 0;
    new_inode.i_ctime       = 0;
    new_inode.i_mtime       = 0;
    new_inode.i_dtime       = 0;
    new_inode.i_links_count = 1;
    new_inode.i_blocks      = 0;
    new_inode.i_flags       = 0;
    memset(new_inode.i_block, 0, sizeof(new_inode.i_block));
    new_inode.i_generation = 0;
    new_inode.i_file_acl   = 0;
    new_inode.i_dir_acl    = 0;
    new_inode.i_faddr      = 0;

    if (WriteInode(dev, sb, new_inode_num, &new_inode, ext2_lba) != 0) {
        FreeInode(dev, sb, new_inode_num, ext2_lba);
        return -10;
    }

    std::uint8_t file_type = 1;
    if (AddEntry(dev, sb, &parent_inode, new_inode_num, filename, file_type,
                 ext2_lba) != 0) {
        FreeInode(dev, sb, new_inode_num, ext2_lba);
        return -11;
    }

    parent_inode.i_mtime = 0;
    WriteInode(dev, sb, parent_inode_num, &parent_inode, ext2_lba);

    return new_inode_num;
}

int DeleteFile(block::BlockDevice *dev, Ext2SuperBlock *sb, const char *path,
               std::uint32_t ext2_lba) {
    if (!dev || !sb || !path) {
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

    if (inode.i_mode & EXT2_S_IFDIR) {
        return -4;
    }

    char parent_path[256];
    char filename[256];

    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        return -5;
    }

    if (last_slash == path) {
        strcpy(parent_path, "/");
    } else {
        std::uint32_t len = last_slash - path;
        if (len >= sizeof(parent_path)) {
            return -6;
        }
        memcpy(parent_path, path, len);
        parent_path[len] = '\0';
    }

    strcpy(filename, last_slash + 1);

    std::uint32_t parent_inode_num;
    if (LookupPath(dev, sb, parent_path, &parent_inode_num, ext2_lba) != 0) {
        return -7;
    }

    Ext2Inode parent_inode;
    if (ReadInode(dev, sb, parent_inode_num, &parent_inode, ext2_lba) != 0) {
        return -8;
    }

    if (RemoveEntry(dev, sb, &parent_inode, filename, ext2_lba) != 0) {
        return -9;
    }

    TruncateInode(dev, sb, &inode, ext2_lba);
    FreeInode(dev, sb, inode_num, ext2_lba);

    parent_inode.i_mtime = 0;
    WriteInode(dev, sb, parent_inode_num, &parent_inode, ext2_lba);

    return 0;
}

}  // namespace ext2
