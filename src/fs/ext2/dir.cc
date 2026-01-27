#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/fs/ext2.h"
#include "kernel/mm.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

namespace ext2 {

int FindEntry(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *dir_inode,
              const char *name, Ext2DirEntry *entry, std::uint32_t ext2_lba) {
    if (!dev || !sb || !dir_inode || !name || !entry) {
        return -1;
    }

    // tty::printk("EXT2: FindEntry: name = %s\n", name);
    if (!(dir_inode->i_mode & EXT2_S_IFDIR)) {
        tty::printk("EXT2: FindEntry: not a directory: inode %u\n",
                    dir_inode->i_mode);
        return -2;
    }

    std::uint32_t block_size  = 1024 << sb->s_log_block_size;
    std::uint32_t file_size   = dir_inode->i_size;
    std::uint32_t block_count = (file_size + block_size - 1) / block_size;

    std::uint8_t *block_buf = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!block_buf) {
        tty::printk("EXT2: Failed to allocate buffer for directory\n");
        return -3;
    }

    for (std::uint32_t i = 0; i < block_count; i++) {
        std::uint32_t block_num = GetBlockNum(dir_inode, i, sb, dev, ext2_lba);
        if (block_num == 0) {
            continue;
        }

        if (ReadBlock(dev, sb, block_num, block_buf, ext2_lba) != 0) {
            continue;
        }

        std::uint32_t offset = 0;
        while (offset < block_size) {
            Ext2DirEntry *dir = (Ext2DirEntry *)(block_buf + offset);

            if (dir->inode != 0 && dir->name_len > 0) {
                if (strncmp(name, dir->name, dir->name_len) == 0 &&
                    name[dir->name_len] == '\0') {
                    memcpy(entry, dir, sizeof(Ext2DirEntry));
                    mm::page::Free(block_buf);
                    return 0;
                }
            }

            offset += dir->rec_len;
        }
    }

    mm::page::Free(block_buf);
    return -4;
}

int AddEntry(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *dir_inode,
             std::uint32_t inode_num, const char *name, std::uint8_t file_type,
             std::uint32_t ext2_lba) {
    if (!dev || !sb || !dir_inode || !name) {
        return -1;
    }

    if (!(dir_inode->i_mode & EXT2_S_IFDIR)) {
        return -2;
    }

    std::uint32_t name_len = strlen(name);
    if (name_len > 255) {
        return -3;
    }

    std::uint32_t block_size  = 1024 << sb->s_log_block_size;
    std::uint32_t file_size   = dir_inode->i_size;
    std::uint32_t block_count = (file_size + block_size - 1) / block_size;

    std::uint8_t *block_buf = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!block_buf) {
        tty::printk("EXT2: Failed to allocate buffer for directory\n");
        return -4;
    }

    std::uint16_t entry_len = sizeof(Ext2DirEntry) - 256 + name_len;
    entry_len               = (entry_len + 3) & ~3;

    for (std::uint32_t i = 0; i < block_count; i++) {
        std::uint32_t block_num = GetBlockNum(dir_inode, i, sb, dev, ext2_lba);
        if (block_num == 0) {
            block_num = AllocBlock(dev, sb, ext2_lba);
            if (block_num == 0) {
                mm::page::Free(block_buf);
                return -5;
            }
            SetBlockNum(dir_inode, i, block_num, sb, dev, ext2_lba);
            memset(block_buf, 0, block_size);
        } else {
            if (ReadBlock(dev, sb, block_num, block_buf, ext2_lba) != 0) {
                continue;
            }
        }

        std::uint32_t offset = 0;
        while (offset < block_size) {
            Ext2DirEntry *dir = (Ext2DirEntry *)(block_buf + offset);

            if (dir->inode != 0) {
                std::uint16_t used_len =
                    sizeof(Ext2DirEntry) - 256 + dir->name_len;
                used_len = (used_len + 3) & ~3;

                if (dir->rec_len - used_len >= entry_len) {
                    Ext2DirEntry *new_entry =
                        (Ext2DirEntry *)(block_buf + offset + used_len);
                    new_entry->inode     = inode_num;
                    new_entry->rec_len   = dir->rec_len - used_len;
                    new_entry->name_len  = name_len;
                    new_entry->file_type = file_type;
                    memcpy(new_entry->name, name, name_len);

                    dir->rec_len = used_len;

                    if (WriteBlock(dev, sb, block_num, block_buf, ext2_lba) !=
                        0) {
                        mm::page::Free(block_buf);
                        return -6;
                    }

                    mm::page::Free(block_buf);
                    return 0;
                }
            } else {
                if (dir->rec_len >= entry_len) {
                    dir->inode     = inode_num;
                    dir->rec_len   = entry_len;
                    dir->name_len  = name_len;
                    dir->file_type = file_type;
                    memcpy(dir->name, name, name_len);

                    if (WriteBlock(dev, sb, block_num, block_buf, ext2_lba) !=
                        0) {
                        mm::page::Free(block_buf);
                        return -6;
                    }

                    mm::page::Free(block_buf);
                    return 0;
                }
            }

            offset += dir->rec_len;
        }
    }

    std::uint32_t new_block = AllocBlock(dev, sb, ext2_lba);
    if (new_block == 0) {
        mm::page::Free(block_buf);
        return -5;
    }

    SetBlockNum(dir_inode, block_count, new_block, sb, dev, ext2_lba);
    memset(block_buf, 0, block_size);

    Ext2DirEntry *new_entry = (Ext2DirEntry *)block_buf;
    new_entry->inode        = inode_num;
    new_entry->rec_len      = block_size;
    new_entry->name_len     = name_len;
    new_entry->file_type    = file_type;
    memcpy(new_entry->name, name, name_len);

    if (WriteBlock(dev, sb, new_block, block_buf, ext2_lba) != 0) {
        mm::page::Free(block_buf);
        return -6;
    }

    dir_inode->i_size += block_size;

    mm::page::Free(block_buf);
    return 0;
}

int RemoveEntry(block::BlockDevice *dev, Ext2SuperBlock *sb,
                Ext2Inode *dir_inode, const char *name,
                std::uint32_t ext2_lba) {
    if (!dev || !sb || !dir_inode || !name) {
        return -1;
    }

    if (!(dir_inode->i_mode & EXT2_S_IFDIR)) {
        return -2;
    }

    std::uint32_t block_size  = 1024 << sb->s_log_block_size;
    std::uint32_t file_size   = dir_inode->i_size;
    std::uint32_t block_count = (file_size + block_size - 1) / block_size;

    std::uint8_t *block_buf = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!block_buf) {
        tty::printk("EXT2: Failed to allocate buffer for directory\n");
        return -3;
    }

    for (std::uint32_t i = 0; i < block_count; i++) {
        std::uint32_t block_num = GetBlockNum(dir_inode, i, sb, dev, ext2_lba);
        if (block_num == 0) {
            continue;
        }

        if (ReadBlock(dev, sb, block_num, block_buf, ext2_lba) != 0) {
            continue;
        }

        std::uint32_t offset      = 0;
        std::uint32_t prev_offset = 0;
        Ext2DirEntry *prev_entry  = nullptr;

        while (offset < block_size) {
            Ext2DirEntry *dir = (Ext2DirEntry *)(block_buf + offset);

            if (dir->inode != 0 && dir->name_len > 0) {
                if (strncmp(name, dir->name, dir->name_len) == 0 &&
                    name[dir->name_len] == '\0') {
                    if (prev_entry) {
                        prev_entry->rec_len += dir->rec_len;
                    } else {
                        dir->inode = 0;
                    }

                    if (WriteBlock(dev, sb, block_num, block_buf, ext2_lba) !=
                        0) {
                        mm::page::Free(block_buf);
                        return -4;
                    }

                    mm::page::Free(block_buf);
                    return 0;
                }
            }

            prev_offset = offset;
            prev_entry  = dir;
            offset += dir->rec_len;
        }
    }

    mm::page::Free(block_buf);
    return -5;
}

int LookupPath(block::BlockDevice *dev, Ext2SuperBlock *sb, const char *path,
               std::uint32_t *inode_num, std::uint32_t ext2_lba) {
    if (!dev || !sb || !path || !inode_num) {
        return -1;
    }

    if (path[0] != '/') {
        return -2;
    }

    Ext2Inode inode;
    if (ReadInode(dev, sb, EXT2_ROOT_INO, &inode, ext2_lba) != 0) {
        return -3;
    }

    *inode_num = EXT2_ROOT_INO;

    if (path[1] == '\0') {
        return 0;
    }

    const char *start = path + 1;
    char name[256];

    while (*start) {
        const char *end = start;
        while (*end && *end != '/') {
            end++;
        }

        std::uint32_t len = end - start;
        if (len >= sizeof(name)) {
            return -4;
        }

        memcpy(name, start, len);
        name[len] = '\0';

        Ext2DirEntry entry;
        if (FindEntry(dev, sb, &inode, name, &entry, ext2_lba) != 0) {
            return -5;
        }

        *inode_num = entry.inode;

        if (ReadInode(dev, sb, entry.inode, &inode, ext2_lba) != 0) {
            return -3;
        }

        if (*end == '\0') {
            break;
        }

        start = end + 1;
    }

    return 0;
}

vfs::DirEntry *Readdir(block::BlockDevice *dev, Ext2SuperBlock *sb,
                       std::uint32_t dir_inode_num, std::uint32_t index,
                       std::uint32_t ext2_lba) {
    if (!dev || !sb) {
        return nullptr;
    }

    Ext2Inode dir_inode;
    if (ReadInode(dev, sb, dir_inode_num, &dir_inode, ext2_lba) != 0) {
        return nullptr;
    }

    if (!(dir_inode.i_mode & EXT2_S_IFDIR)) {
        return nullptr;
    }

    std::uint32_t block_size  = 1024 << sb->s_log_block_size;
    std::uint32_t file_size   = dir_inode.i_size;
    std::uint32_t block_count = (file_size + block_size - 1) / block_size;

    std::uint8_t *block_buf = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!block_buf) {
        return nullptr;
    }

    std::uint32_t entry_index = 0;

    for (std::uint32_t i = 0; i < block_count; i++) {
        std::uint32_t block_num = GetBlockNum(&dir_inode, i, sb, dev, ext2_lba);
        if (block_num == 0) {
            continue;
        }

        if (ReadBlock(dev, sb, block_num, block_buf, ext2_lba) != 0) {
            continue;
        }

        std::uint32_t offset = 0;
        while (offset < block_size) {
            Ext2DirEntry *dir = (Ext2DirEntry *)(block_buf + offset);

            if (dir->inode != 0 && dir->name_len > 0) {
                if (entry_index == index) {
                    vfs::DirEntry *result =
                        (vfs::DirEntry *)mm::page::Alloc(sizeof(vfs::DirEntry));
                    if (!result) {
                        mm::page::Free(block_buf);
                        return nullptr;
                    }

                    result->inode = dir->inode;
                    result->type  = dir->file_type;
                    memcpy(result->name, dir->name, dir->name_len);
                    result->name[dir->name_len] = '\0';
                    result->next                = nullptr;

                    mm::page::Free(block_buf);
                    return result;
                }
                entry_index++;
            }

            offset += dir->rec_len;
        }
    }

    mm::page::Free(block_buf);
    return nullptr;
}

}  // namespace ext2
