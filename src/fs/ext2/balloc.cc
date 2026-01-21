#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/fs/ext2.h"
#include "kernel/mm.h"
#include "kernel/tty.h"

namespace ext2 {

int ReadBlock(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t block_num, 
              void *buf, std::uint32_t ext2_lba) {
    if (!dev || !sb || !buf) {
        return -1;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t sectors_per_block = block_size / 512;
    std::uint64_t sector = ext2_lba + (std::uint64_t)block_num * sectors_per_block;
    std::uint32_t count = sectors_per_block;

    return dev->Read(sector, count, buf);
}

int WriteBlock(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t block_num, 
               const void *buf, std::uint32_t ext2_lba) {
    if (!dev || !sb || !buf) {
        return -1;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t sectors_per_block = block_size / 512;
    std::uint64_t sector = ext2_lba + (std::uint64_t)block_num * sectors_per_block;
    std::uint32_t count = sectors_per_block;

    return dev->Write(sector, count, buf);
}

std::uint32_t AllocBlock(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t ext2_lba) {
    if (!dev || !sb) {
        return 0;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t blocks_per_group = sb->s_blocks_per_group;
    std::uint32_t group_count = (sb->s_blocks_count + blocks_per_group - 1) / 
                                blocks_per_group;

    std::uint8_t *bitmap = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!bitmap) {
        tty::printk("EXT2: Failed to allocate bitmap buffer\n");
        return 0;
    }

    for (std::uint32_t group = 0; group < group_count; group++) {
        Ext2GroupDesc gd;
        if (ReadGroupDesc(dev, sb, group, &gd, ext2_lba) != 0) {
            continue;
        }

        if (gd.bg_free_blocks_count == 0) {
            continue;
        }

        if (ReadBlock(dev, sb, gd.bg_block_bitmap, bitmap, ext2_lba) != 0) {
            continue;
        }

        std::uint32_t block_in_group = 0;
        std::uint32_t start_block = group * blocks_per_group;
        if (start_block < sb->s_first_data_block) {
            block_in_group = sb->s_first_data_block - start_block;
        }

        for (; block_in_group < blocks_per_group; block_in_group++) {
            std::uint32_t byte = block_in_group / 8;
            std::uint8_t bit = block_in_group % 8;

            if (!(bitmap[byte] & (1 << bit))) {
                bitmap[byte] |= (1 << bit);

                if (WriteBlock(dev, sb, gd.bg_block_bitmap, bitmap, ext2_lba) != 0) {
                    mm::page::Free(bitmap);
                    return 0;
                }

                gd.bg_free_blocks_count--;
                sb->s_free_blocks_count--;

                WriteGroupDesc(dev, sb, group, &gd, ext2_lba);
                WriteSuperBlock(dev, sb);

                std::uint32_t block = start_block + block_in_group;
                std::uint8_t *zero_buf = (std::uint8_t *)mm::page::Alloc(block_size);
                if (zero_buf) {
                    memset(zero_buf, 0, block_size);
                    WriteBlock(dev, sb, block, zero_buf, ext2_lba);
                    mm::page::Free(zero_buf);
                }

                mm::page::Free(bitmap);
                return block;
            }
        }
    }

    mm::page::Free(bitmap);
    tty::printk("EXT2: No free blocks available\n");
    return 0;
}

void FreeBlock(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t block, std::uint32_t ext2_lba) {
    if (!dev || !sb || block == 0) {
        return;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t blocks_per_group = sb->s_blocks_per_group;
    std::uint32_t group = block / blocks_per_group;
    std::uint32_t block_in_group = block % blocks_per_group;

    Ext2GroupDesc gd;
    if (ReadGroupDesc(dev, sb, group, &gd, ext2_lba) != 0) {
        return;
    }

    std::uint8_t *bitmap = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!bitmap) {
        return;
    }

    if (ReadBlock(dev, sb, gd.bg_block_bitmap, bitmap, ext2_lba) != 0) {
        mm::page::Free(bitmap);
        return;
    }

    std::uint32_t byte = block_in_group / 8;
    std::uint8_t bit = block_in_group % 8;

    if (bitmap[byte] & (1 << bit)) {
        bitmap[byte] &= ~(1 << bit);

        WriteBlock(dev, sb, gd.bg_block_bitmap, bitmap, ext2_lba);

        gd.bg_free_blocks_count++;
        sb->s_free_blocks_count++;

        WriteGroupDesc(dev, sb, group, &gd, ext2_lba);
        WriteSuperBlock(dev, sb);
    }

    mm::page::Free(bitmap);
}


std::uint32_t AllocInode(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t ext2_lba) {
    if (!dev || !sb) {
        return 0;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t inodes_per_group = sb->s_inodes_per_group;
    std::uint32_t group_count = (sb->s_inodes_count + inodes_per_group - 1) / 
                                inodes_per_group;

    std::uint8_t *bitmap = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!bitmap) {
        tty::printk("EXT2: Failed to allocate inode bitmap buffer\n");
        return 0;
    }

    for (std::uint32_t group = 0; group < group_count; group++) {
        Ext2GroupDesc gd;
        if (ReadGroupDesc(dev, sb, group, &gd, ext2_lba) != 0) {
            continue;
        }

        if (gd.bg_free_inodes_count == 0) {
            continue;
        }

        if (ReadBlock(dev, sb, gd.bg_inode_bitmap, bitmap, ext2_lba) != 0) {
            continue;
        }

        for (std::uint32_t i = 0; i < inodes_per_group; i++) {
            std::uint32_t byte = i / 8;
            std::uint8_t bit = i % 8;

            if (!(bitmap[byte] & (1 << bit))) {
                std::uint32_t inode_num = group * inodes_per_group + i + 1;

                if (inode_num < EXT2_ROOT_INO) {
                    continue;
                }

                bitmap[byte] |= (1 << bit);

                if (WriteBlock(dev, sb, gd.bg_inode_bitmap, bitmap, ext2_lba) != 0) {
                    mm::page::Free(bitmap);
                    return 0;
                }

                gd.bg_free_inodes_count--;
                sb->s_free_inodes_count--;

                WriteGroupDesc(dev, sb, group, &gd, ext2_lba);
                WriteSuperBlock(dev, sb);

                mm::page::Free(bitmap);
                return inode_num;
            }
        }
    }

    mm::page::Free(bitmap);
    tty::printk("EXT2: No free inodes available\n");
    return 0;
}

void FreeInode(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t inode, std::uint32_t ext2_lba) {
    if (!dev || !sb || inode == 0) {
        return;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t inodes_per_group = sb->s_inodes_per_group;
    std::uint32_t group = (inode - 1) / inodes_per_group;
    std::uint32_t inode_in_group = (inode - 1) % inodes_per_group;

    Ext2GroupDesc gd;
    if (ReadGroupDesc(dev, sb, group, &gd, ext2_lba) != 0) {
        return;
    }

    std::uint8_t *bitmap = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!bitmap) {
        return;
    }

    if (ReadBlock(dev, sb, gd.bg_inode_bitmap, bitmap, ext2_lba) != 0) {
        mm::page::Free(bitmap);
        return;
    }

    std::uint32_t byte = inode_in_group / 8;
    std::uint8_t bit = inode_in_group % 8;

    if (bitmap[byte] & (1 << bit)) {
        bitmap[byte] &= ~(1 << bit);

        WriteBlock(dev, sb, gd.bg_inode_bitmap, bitmap, ext2_lba);

        gd.bg_free_inodes_count++;
        sb->s_free_inodes_count++;

        WriteGroupDesc(dev, sb, group, &gd, ext2_lba);
        WriteSuperBlock(dev, sb);
    }

    mm::page::Free(bitmap);
}

}
