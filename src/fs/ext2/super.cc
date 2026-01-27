#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/fs/ext2.h"
#include "kernel/mm.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace ext2 {

std::uint32_t FindExt2Partition(block::BlockDevice *dev) {
    if (!dev) {
        return 0;
    }

    std::uint8_t *mbr_buf = (std::uint8_t *)mm::page::Alloc(0x1000);
    if (!mbr_buf) {
        tty::printk("EXT2: Failed to allocate MBR buffer\n");
        return 0;
    }

    int ret = dev->Read(0, 1, mbr_buf);
    if (ret != 0) {
        tty::printk("EXT2: Failed to read MBR\n");
        mm::page::Free(mbr_buf);
        return 0;
    }

    MBR *mbr = (MBR *)mbr_buf;
    if (mbr->signature != 0xAA55) {
        tty::printk("EXT2: Invalid MBR signature 0x%x\n", mbr->signature);
        mm::page::Free(mbr_buf);
        return 0;
    }

    std::uint32_t ext2_lba = 0;
    for (int i = 0; i < 4; i++) {
        if (mbr->partitions[i].partition_type == PARTITION_TYPE_LINUX) {
            ext2_lba = mbr->partitions[i].starting_lba;
            // tty::printk("EXT2: Found Linux partition at LBA %u\n", ext2_lba);
            break;
        }
    }

    mm::page::Free(mbr_buf);

    if (ext2_lba == 0) {
        tty::printk("EXT2: No Linux partition found\n");
        return 0;
    }

    return ext2_lba;
}

int ReadSuperBlock(block::BlockDevice *dev, Ext2SuperBlock *sb,
                   std::uint32_t ext2_lba) {
    if (!dev || !sb) {
        return -1;
    }

    std::uint8_t *buf = (std::uint8_t *)mm::page::Alloc(2048);
    if (!buf) {
        tty::printk("EXT2: Failed to allocate buffer for superblock\n");
        return -3;
    }

    // 超级块在 block 1，即 sector (ext2_lba + 2)
    // 读取2个sector (1024字节 = block_size)
    int ret = dev->Read(ext2_lba + 2, 2, buf);
    if (ret != 0) {
        tty::printk("EXT2: Failed to read superblock (ret=%d, lba=%u)\n", ret,
                    ext2_lba);
        mm::page::Free(buf);
        return -4;
    }

    memcpy(sb, buf, sizeof(Ext2SuperBlock));
    mm::page::Free(buf);

    if (sb->s_magic != EXT2_SUPER_MAGIC) {
        tty::printk("EXT2: Invalid magic number 0x%x (expected 0x%x)\n",
                    sb->s_magic, EXT2_SUPER_MAGIC);
        return -5;
    }
    // tty::printk("EXT2: Superblock:\n");
    // tty::printk("  Inodes: %u, Blocks: %u\n", sb->s_inodes_count,
    // sb->s_blocks_count); tty::printk("  Block size: %u, Inode size: %u\n",
    //             1024 << sb->s_log_block_size, sb->s_inode_size);
    // tty::printk("  Inodes per group: %u, Blocks per group: %u\n",
    //             sb->s_inodes_per_group, sb->s_blocks_per_group);
    // tty::printk("  First data block: %u\n", sb->s_first_data_block);

    return 0;
}

int ReadGroupDesc(block::BlockDevice *dev, Ext2SuperBlock *sb,
                  std::uint32_t group, Ext2GroupDesc *gd,
                  std::uint32_t ext2_lba) {
    if (!dev || !sb || !gd) {
        return -1;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t gd_block   = 2;

    std::uint8_t *buf = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!buf) {
        tty::printk("EXT2: Failed to allocate buffer for group descriptor\n");
        return -2;
    }

    std::uint32_t sectors_per_block = block_size / 512;
    std::uint64_t sector =
        ext2_lba + (std::uint64_t)gd_block * sectors_per_block;
    std::uint32_t count = sectors_per_block;

    int ret = dev->Read(sector, count, buf);
    if (ret != 0) {
        tty::printk(
            "EXT2: Failed to read group descriptor (ret=%d, sector=%lu, "
            "ext2_lba=%u)\n",
            ret, sector, ext2_lba);
        mm::page::Free(buf);
        return -3;
    }

    memcpy(gd, buf + group * sizeof(Ext2GroupDesc), sizeof(Ext2GroupDesc));

    // tty::printk("EXT2: ReadGroupDesc - group=%u, gd_block=%u, sector=%lu,
    // ext2_lba=%u\n", group, gd_block, sector, ext2_lba); tty::printk("EXT2:
    // ReadGroupDesc - bg_block_bitmap=%u, bg_inode_bitmap=%u,
    // bg_inode_table=%u\n",
    //             gd->bg_block_bitmap, gd->bg_inode_bitmap,
    //             gd->bg_inode_table);
    // tty::printk("EXT2: ReadGroupDesc - bg_free_blocks_count=%u,
    // bg_free_inodes_count=%u\n",
    //             gd->bg_free_blocks_count, gd->bg_free_inodes_count);
    // tty::printk("EXT2: ReadGroupDesc - bg_used_dirs_count=%u\n",
    // gd->bg_used_dirs_count);

    mm::page::Free(buf);

    return 0;
}

int WriteSuperBlock(block::BlockDevice *dev, Ext2SuperBlock *sb) {
    if (!dev || !sb) {
        return -1;
    }

    std::uint8_t *buf = (std::uint8_t *)mm::page::Alloc(1024);
    if (!buf) {
        tty::printk("EXT2: Failed to allocate buffer for superblock\n");
        return -2;
    }

    memset(buf, 0, 1024);
    memcpy(buf, sb, sizeof(Ext2SuperBlock));

    int ret = dev->Write(2, 2, buf);
    if (ret != 0) {
        tty::printk("EXT2: Failed to write superblock\n");
        mm::page::Free(buf);
        return -3;
    }

    mm::page::Free(buf);
    return 0;
}

int WriteGroupDesc(block::BlockDevice *dev, Ext2SuperBlock *sb,
                   std::uint32_t group, Ext2GroupDesc *gd,
                   std::uint32_t ext2_lba) {
    if (!dev || !sb || !gd) {
        return -1;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t gd_block   = 2;

    std::uint8_t *buf = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!buf) {
        tty::printk("EXT2: Failed to allocate buffer for group descriptor\n");
        return -2;
    }

    std::uint32_t sectors_per_block = block_size / 512;
    std::uint64_t sector =
        ext2_lba + (std::uint64_t)gd_block * sectors_per_block;
    std::uint32_t count = sectors_per_block;
    int ret             = dev->Read(sector, count, buf);
    if (ret != 0) {
        tty::printk("EXT2: Failed to read group descriptor for writing\n");
        mm::page::Free(buf);
        return -3;
    }

    memcpy(buf + group * sizeof(Ext2GroupDesc), gd, sizeof(Ext2GroupDesc));

    ret = dev->Write(sector, count, buf);
    if (ret != 0) {
        tty::printk("EXT2: Failed to write group descriptor\n");
        mm::page::Free(buf);
        return -4;
    }

    mm::page::Free(buf);
    return 0;
}

}  // namespace ext2