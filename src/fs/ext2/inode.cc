#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/fs/ext2.h"
#include "kernel/mm.h"
#include "kernel/tty.h"

namespace ext2 {

std::uint32_t Inode2Block(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t inode_num, std::uint32_t ext2_lba) {
    if (!dev || !sb) {
        return 0;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t inodes_per_block = block_size / sb->s_inode_size;
    std::uint32_t inodes_per_group = sb->s_inodes_per_group;
    std::uint32_t group = (inode_num - 1) / inodes_per_group;
    std::uint32_t inode_in_group = (inode_num - 1) % inodes_per_group;

    Ext2GroupDesc gd;
    if (ReadGroupDesc(dev, sb, group, &gd, ext2_lba) != 0) {
        return 0;
    }

    std::uint32_t block = gd.bg_inode_table + (inode_in_group / inodes_per_block);
    return block;
}

int ReadInode(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t inode_num, 
              Ext2Inode *inode, std::uint32_t ext2_lba) {
    if (!dev || !sb || !inode || inode_num == 0) {
        return -1;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t inodes_per_block = block_size / sb->s_inode_size;
    std::uint32_t inodes_per_group = sb->s_inodes_per_group;
    std::uint32_t group = (inode_num - 1) / inodes_per_group;
    std::uint32_t inode_in_group = (inode_num - 1) % inodes_per_group;

    Ext2GroupDesc gd;
    if (ReadGroupDesc(dev, sb, group, &gd, ext2_lba) != 0) {
        tty::printk("EXT2: ReadInode - ReadGroupDesc failed\n");
        return -2;
    }

    std::uint32_t table_block = gd.bg_inode_table + (inode_in_group / inodes_per_block);
    std::uint32_t inode_offset = (inode_in_group % inodes_per_block) * sb->s_inode_size;
    
    //tty::printk("EXT2: ReadInode - inode=%u, group=%u, inode_in_group=%u\n", inode_num, group, inode_in_group);
    //tty::printk("EXT2: ReadInode - gd.bg_inode_table=%u, inodes_per_block=%u\n", gd.bg_inode_table, inodes_per_block);
    //tty::printk("EXT2: ReadInode - table_block=%u, inode_offset=%u\n", table_block, inode_offset);

    std::uint8_t *buf = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!buf) {
        return -3;
    }

    if (ReadBlock(dev, sb, table_block, buf, ext2_lba) != 0) {
        mm::page::Free(buf);
        return -4;
    }

    memcpy(inode, buf + inode_offset, sizeof(Ext2Inode));
    mm::page::Free(buf);

    return 0;
}

int WriteInode(block::BlockDevice *dev, Ext2SuperBlock *sb, std::uint32_t inode_num, 
               Ext2Inode *inode, std::uint32_t ext2_lba) {
    if (!dev || !sb || !inode || inode_num == 0) {
        return -1;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t inodes_per_block = block_size / sb->s_inode_size;
    std::uint32_t inodes_per_group = sb->s_inodes_per_group;
    std::uint32_t group = (inode_num - 1) / inodes_per_group;
    std::uint32_t inode_in_group = (inode_num - 1) % inodes_per_group;

    Ext2GroupDesc gd;
    if (ReadGroupDesc(dev, sb, group, &gd, ext2_lba) != 0) {
        return -2;
    }

    std::uint32_t table_block = gd.bg_inode_table + (inode_in_group / inodes_per_block);
    std::uint32_t inode_offset = (inode_in_group % inodes_per_block) * sb->s_inode_size;

    std::uint8_t *buf = (std::uint8_t *)mm::page::Alloc(block_size);
    if (!buf) {
        tty::printk("EXT2: Failed to allocate buffer for inode\n");
        return -3;
    }

    if (ReadBlock(dev, sb, table_block, buf, ext2_lba) != 0) {
        mm::page::Free(buf);
        return -4;
    }

    memcpy(buf + inode_offset, inode, sizeof(Ext2Inode));

    if (WriteBlock(dev, sb, table_block, buf, ext2_lba) != 0) {
        mm::page::Free(buf);
        return -5;
    }

    mm::page::Free(buf);
    return 0;
}

std::uint32_t GetBlockNum(Ext2Inode *inode, std::uint32_t block, 
                          Ext2SuperBlock *sb, block::BlockDevice *dev, std::uint32_t ext2_lba) {
    if (!inode || !sb || !dev) {
        return 0;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t ptrs_per_block = block_size / 4;

    if (block < EXT2_NDIR_BLOCKS) {
        return inode->i_block[block];
    }

    block -= EXT2_NDIR_BLOCKS;

    if (block < ptrs_per_block) {
        if (inode->i_block[EXT2_IND_BLOCK] == 0) {
            return 0;
        }

        std::uint32_t *ind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
        if (!ind_buf) {
            return 0;
        }

        if (ReadBlock(dev, sb, inode->i_block[EXT2_IND_BLOCK], ind_buf, ext2_lba) != 0) {
            mm::page::Free(ind_buf);
            return 0;
        }

        std::uint32_t result = ind_buf[block];
        mm::page::Free(ind_buf);
        return result;
    }

    block -= ptrs_per_block;

    if (block < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[EXT2_DIND_BLOCK] == 0) {
            return 0;
        }

        std::uint32_t *dind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
        if (!dind_buf) {
            return 0;
        }

        if (ReadBlock(dev, sb, inode->i_block[EXT2_DIND_BLOCK], dind_buf, ext2_lba) != 0) {
            mm::page::Free(dind_buf);
            return 0;
        }

        std::uint32_t ind_block = dind_buf[block / ptrs_per_block];
        mm::page::Free(dind_buf);

        if (ind_block == 0) {
            return 0;
        }

        std::uint32_t *ind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
        if (!ind_buf) {
            return 0;
        }

        if (ReadBlock(dev, sb, ind_block, ind_buf, ext2_lba) != 0) {
            mm::page::Free(ind_buf);
            return 0;
        }

        std::uint32_t result = ind_buf[block % ptrs_per_block];
        mm::page::Free(ind_buf);
        return result;
    }

    block -= ptrs_per_block * ptrs_per_block;

    if (inode->i_block[EXT2_TIND_BLOCK] == 0) {
        return 0;
    }

    std::uint32_t *tind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
    if (!tind_buf) {
        return 0;
    }

    if (ReadBlock(dev, sb, inode->i_block[EXT2_TIND_BLOCK], tind_buf, ext2_lba) != 0) {
        mm::page::Free(tind_buf);
        return 0;
    }

    std::uint32_t dind_block = tind_buf[block / (ptrs_per_block * ptrs_per_block)];
    mm::page::Free(tind_buf);

    if (dind_block == 0) {
        return 0;
    }

    std::uint32_t *dind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
    if (!dind_buf) {
        return 0;
    }

    if (ReadBlock(dev, sb, dind_block, dind_buf, ext2_lba) != 0) {
        mm::page::Free(dind_buf);
        return 0;
    }

    block %= (ptrs_per_block * ptrs_per_block);
    std::uint32_t ind_block = dind_buf[block / ptrs_per_block];
    mm::page::Free(dind_buf);

    if (ind_block == 0) {
        return 0;
    }

    std::uint32_t *ind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
    if (!ind_buf) {
        return 0;
    }

    if (ReadBlock(dev, sb, ind_block, ind_buf, ext2_lba) != 0) {
        mm::page::Free(ind_buf);
        return 0;
    }

    std::uint32_t result = ind_buf[block % ptrs_per_block];
    mm::page::Free(ind_buf);
    return result;
}

int SetBlockNum(Ext2Inode *inode, std::uint32_t block, std::uint32_t num,
                Ext2SuperBlock *sb, block::BlockDevice *dev, std::uint32_t ext2_lba) {
    if (!inode || !sb || !dev) {
        return -1;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t ptrs_per_block = block_size / 4;

    if (block < EXT2_NDIR_BLOCKS) {
        inode->i_block[block] = num;
        return 0;
    }

    block -= EXT2_NDIR_BLOCKS;

    if (block < ptrs_per_block) {
        if (inode->i_block[EXT2_IND_BLOCK] == 0) {
            if (num == 0) {
                return 0;
            }
            inode->i_block[EXT2_IND_BLOCK] = AllocBlock(dev, sb, ext2_lba);
            if (inode->i_block[EXT2_IND_BLOCK] == 0) {
                return -2;
            }
        }

        std::uint32_t *ind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
        if (!ind_buf) {
            return -3;
        }

        if (ReadBlock(dev, sb, inode->i_block[EXT2_IND_BLOCK], ind_buf, ext2_lba) != 0) {
            mm::page::Free(ind_buf);
            return -4;
        }

        ind_buf[block] = num;

        if (WriteBlock(dev, sb, inode->i_block[EXT2_IND_BLOCK], ind_buf, ext2_lba) != 0) {
            mm::page::Free(ind_buf);
            return -5;
        }

        mm::page::Free(ind_buf);
        return 0;
    }

    block -= ptrs_per_block;

    if (block < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[EXT2_DIND_BLOCK] == 0) {
            if (num == 0) {
                return 0;
            }
            inode->i_block[EXT2_DIND_BLOCK] = AllocBlock(dev, sb, ext2_lba);
            if (inode->i_block[EXT2_DIND_BLOCK] == 0) {
                return -2;
            }
        }

        std::uint32_t *dind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
        if (!dind_buf) {
            return -3;
        }

        if (ReadBlock(dev, sb, inode->i_block[EXT2_DIND_BLOCK], dind_buf, ext2_lba) != 0) {
            mm::page::Free(dind_buf);
            return -4;
        }

        std::uint32_t ind_block = dind_buf[block / ptrs_per_block];
        if (ind_block == 0) {
            if (num == 0) {
                mm::page::Free(dind_buf);
                return 0;
            }
            ind_block = AllocBlock(dev, sb, ext2_lba);
            if (ind_block == 0) {
                mm::page::Free(dind_buf);
                return -2;
            }
            dind_buf[block / ptrs_per_block] = ind_block;
        }

        if (WriteBlock(dev, sb, inode->i_block[EXT2_DIND_BLOCK], dind_buf, ext2_lba) != 0) {
            mm::page::Free(dind_buf);
            return -5;
        }

        mm::page::Free(dind_buf);

        std::uint32_t *ind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
        if (!ind_buf) {
            return -3;
        }

        if (ReadBlock(dev, sb, ind_block, ind_buf, ext2_lba) != 0) {
            mm::page::Free(ind_buf);
            return -4;
        }

        ind_buf[block % ptrs_per_block] = num;

        if (WriteBlock(dev, sb, ind_block, ind_buf, ext2_lba) != 0) {
            mm::page::Free(ind_buf);
            return -5;
        }

        mm::page::Free(ind_buf);
        return 0;
    }

    block -= ptrs_per_block * ptrs_per_block;

    if (inode->i_block[EXT2_TIND_BLOCK] == 0) {
        if (num == 0) {
            return 0;
        }
        inode->i_block[EXT2_TIND_BLOCK] = AllocBlock(dev, sb, ext2_lba);
        if (inode->i_block[EXT2_TIND_BLOCK] == 0) {
            return -2;
        }
    }

    std::uint32_t *tind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
    if (!tind_buf) {
        return -3;
    }

    if (ReadBlock(dev, sb, inode->i_block[EXT2_TIND_BLOCK], tind_buf, ext2_lba) != 0) {
        mm::page::Free(tind_buf);
        return -4;
    }

    std::uint32_t dind_block = tind_buf[block / (ptrs_per_block * ptrs_per_block)];
    if (dind_block == 0) {
        if (num == 0) {
            mm::page::Free(tind_buf);
            return 0;
        }
        dind_block = AllocBlock(dev, sb, ext2_lba);
        if (dind_block == 0) {
            mm::page::Free(tind_buf);
            return -2;
        }
        tind_buf[block / (ptrs_per_block * ptrs_per_block)] = dind_block;
    }

    if (WriteBlock(dev, sb, inode->i_block[EXT2_TIND_BLOCK], tind_buf, ext2_lba) != 0) {
        mm::page::Free(tind_buf);
        return -5;
    }

    mm::page::Free(tind_buf);

    block %= (ptrs_per_block * ptrs_per_block);

    std::uint32_t *dind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
    if (!dind_buf) {
        return -3;
    }

    if (ReadBlock(dev, sb, dind_block, dind_buf, ext2_lba) != 0) {
        mm::page::Free(dind_buf);
        return -4;
    }

    std::uint32_t ind_block = dind_buf[block / ptrs_per_block];
    if (ind_block == 0) {
        if (num == 0) {
            mm::page::Free(dind_buf);
            return 0;
        }
        ind_block = AllocBlock(dev, sb, ext2_lba);
        if (ind_block == 0) {
            mm::page::Free(dind_buf);
            return -2;
        }
        dind_buf[block / ptrs_per_block] = ind_block;
    }

    if (WriteBlock(dev, sb, dind_block, dind_buf, ext2_lba) != 0) {
        mm::page::Free(dind_buf);
        return -5;
    }

    mm::page::Free(dind_buf);

    std::uint32_t *ind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
    if (!ind_buf) {
        return -3;
    }

    if (ReadBlock(dev, sb, ind_block, ind_buf, ext2_lba) != 0) {
        mm::page::Free(ind_buf);
        return -4;
    }

    ind_buf[block % ptrs_per_block] = num;

    if (WriteBlock(dev, sb, ind_block, ind_buf, ext2_lba) != 0) {
        mm::page::Free(ind_buf);
        return -5;
    }

    mm::page::Free(ind_buf);
    return 0;
}

int TruncateInode(block::BlockDevice *dev, Ext2SuperBlock *sb, Ext2Inode *inode, std::uint32_t ext2_lba) {
    if (!dev || !sb || !inode) {
        return -1;
    }

    std::uint32_t block_size = 1024 << sb->s_log_block_size;
    std::uint32_t ptrs_per_block = block_size / 4;
    std::uint32_t file_blocks = (inode->i_size + block_size - 1) / block_size;

    for (std::uint32_t i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        if (i < file_blocks && inode->i_block[i] != 0) {
            FreeBlock(dev, sb, inode->i_block[i], ext2_lba);
            inode->i_block[i] = 0;
        }
    }

    if (inode->i_block[EXT2_IND_BLOCK] != 0) {
        std::uint32_t *ind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
        if (ind_buf) {
            if (ReadBlock(dev, sb, inode->i_block[EXT2_IND_BLOCK], ind_buf, ext2_lba) == 0) {
                for (std::uint32_t i = 0; i < ptrs_per_block; i++) {
                    std::uint32_t block_num = EXT2_NDIR_BLOCKS + i;
                    if (block_num < file_blocks && ind_buf[i] != 0) {
                        FreeBlock(dev, sb, ind_buf[i], ext2_lba);
                    }
                }
            }
            mm::page::Free(ind_buf);
        }
        FreeBlock(dev, sb, inode->i_block[EXT2_IND_BLOCK], ext2_lba);
        inode->i_block[EXT2_IND_BLOCK] = 0;
    }

    if (inode->i_block[EXT2_DIND_BLOCK] != 0) {
        std::uint32_t *dind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
        if (dind_buf) {
            if (ReadBlock(dev, sb, inode->i_block[EXT2_DIND_BLOCK], dind_buf, ext2_lba) == 0) {
                for (std::uint32_t i = 0; i < ptrs_per_block; i++) {
                    if (dind_buf[i] != 0) {
                        std::uint32_t *ind_buf = (std::uint32_t *)mm::page::Alloc(block_size);
                        if (ind_buf) {
                            if (ReadBlock(dev, sb, dind_buf[i], ind_buf, ext2_lba) == 0) {
                                for (std::uint32_t j = 0; j < ptrs_per_block; j++) {
                                    std::uint32_t block_num = EXT2_NDIR_BLOCKS + ptrs_per_block + i * ptrs_per_block + j;
                                    if (block_num < file_blocks && ind_buf[j] != 0) {
                                        FreeBlock(dev, sb, ind_buf[j], ext2_lba);
                                    }
                                }
                            }
                            mm::page::Free(ind_buf);
                        }
                        FreeBlock(dev, sb, dind_buf[i], ext2_lba);
                    }
                }
            }
            mm::page::Free(dind_buf);
        }
        FreeBlock(dev, sb, inode->i_block[EXT2_DIND_BLOCK], ext2_lba);
        inode->i_block[EXT2_DIND_BLOCK] = 0;
    }

    if (inode->i_block[EXT2_TIND_BLOCK] != 0) {
        FreeBlock(dev, sb, inode->i_block[EXT2_TIND_BLOCK], ext2_lba);
        inode->i_block[EXT2_TIND_BLOCK] = 0;
    }

    inode->i_size = 0;
    inode->i_blocks = 0;

    return 0;
}

}
