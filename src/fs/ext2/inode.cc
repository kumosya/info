#include "fs/ext2.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "block.h"
#include "tty.h"
#include "vfs.h"

namespace ext2 {

// Read an inode from disk
int ReadInode(MountFs *mount, std::uint32_t inode_num, Inode *inode) {
    if (!mount || !inode || inode_num == 0 || inode_num > mount->superblock.s_inodes_count) {
        return -1;
    }

    // Calculate block group and inode offset
    std::uint32_t group = (inode_num - 1) / mount->inodes_per_group;
    std::uint32_t index = (inode_num - 1) % mount->inodes_per_group;

    // Calculate inode table block and offset
    std::uint32_t inode_table_block  = mount->group_desc[group].bg_inode_table;
    std::uint32_t inode_size         = mount->superblock.s_inode_size;
    std::uint32_t inode_table_offset = index * inode_size;
    std::uint32_t block_offset       = inode_table_offset / mount->block_size;
    std::uint32_t block_inode_offset = inode_table_offset % mount->block_size;

    // Allocate buffer and read block
    char *buf = new char[mount->block_size];
    if (!buf) {
        return -1;
    }

    if (ReadBlock(mount, inode_table_block + block_offset, buf) != 0) {
        delete[] buf;
        return -1;
    }

    // Copy inode data
    *inode = *(Inode *)(buf + block_inode_offset);

    delete[] buf;
    return 0;
}

// Write an inode to disk
int WriteInode(MountFs *mount, std::uint32_t inode_num, const Inode *inode) {
    if (!mount || !inode || inode_num == 0 || inode_num > mount->superblock.s_inodes_count) {
        return -1;
    }

    // Calculate block group and inode offset
    std::uint32_t group = (inode_num - 1) / mount->inodes_per_group;
    std::uint32_t index = (inode_num - 1) % mount->inodes_per_group;

    // Calculate inode table block and offset
    std::uint32_t inode_table_block  = mount->group_desc[group].bg_inode_table;
    std::uint32_t inode_size         = mount->superblock.s_inode_size;
    std::uint32_t inode_table_offset = index * inode_size;
    std::uint32_t block_offset       = inode_table_offset / mount->block_size;
    std::uint32_t block_inode_offset = inode_table_offset % mount->block_size;

    // Allocate buffer and read block
    char *buf = new char[mount->block_size];
    if (!buf) {
        return -1;
    }

    if (ReadBlock(mount, inode_table_block + block_offset, buf) != 0) {
        delete[] buf;
        return -1;
    }

    // Copy inode data
    *(Inode *)(buf + block_inode_offset) = *inode;

    // Write back to disk
    int ret = WriteBlock(mount, inode_table_block + block_offset, buf);

    delete[] buf;
    return ret;
}

// Allocate a free inode
std::uint32_t AllocInode(MountFs *mount) {
    if (!mount) {
        return 0;
    }

    // Find a group with free inodes
    for (std::uint32_t group = 0; group < mount->groups_count; group++) {
        if (mount->group_desc[group].bg_free_inodes_count == 0) {
            continue;
        }

        // Read inode bitmap
        char *bitmap_buf = new char[mount->block_size];
        if (!bitmap_buf) {
            continue;
        }

        if (ReadBlock(mount, mount->group_desc[group].bg_inode_bitmap, bitmap_buf) != 0) {
            delete[] bitmap_buf;
            continue;
        }

        // Find first free inode in bitmap
        std::uint32_t inode_in_group = 0;
        for (std::uint32_t i = 0; i < mount->block_size; i++) {
            if (bitmap_buf[i] == 0xFF) {
                continue;
            }

            for (int j = 0; j < 8; j++) {
                if (!(bitmap_buf[i] & (1 << j))) {
                    inode_in_group = i * 8 + j;
                    goto found;
                }
            }
        }

        delete[] bitmap_buf;
        continue;

    found:
        // Mark inode as used
        bitmap_buf[inode_in_group / 8] |= (1 << (inode_in_group % 8));

        // Write back bitmap
        WriteBlock(mount, mount->group_desc[group].bg_inode_bitmap, bitmap_buf);

        // Update free inode count
        mount->group_desc[group].bg_free_inodes_count--;
        mount->superblock.s_free_inodes_count--;

        delete[] bitmap_buf;

        // Calculate absolute inode number
        std::uint32_t inode_num = group * mount->inodes_per_group + inode_in_group + 1;

        // Make sure inode is within valid range
        if (inode_num > mount->superblock.s_inodes_count) {
            return 0;
        }

        return inode_num;
    }

    // No free inodes found
    return 0;
}

// Free an inode
int FreeInode(MountFs *mount, std::uint32_t inode_num) {
    if (!mount || inode_num == 0 || inode_num > mount->superblock.s_inodes_count) {
        return -1;
    }

    // Calculate group and inode in group
    std::uint32_t group          = (inode_num - 1) / mount->inodes_per_group;
    std::uint32_t inode_in_group = (inode_num - 1) % mount->inodes_per_group;

    // Read inode bitmap
    char *bitmap_buf = new char[mount->block_size];
    if (!bitmap_buf) {
        return -1;
    }

    if (ReadBlock(mount, mount->group_desc[group].bg_inode_bitmap, bitmap_buf) != 0) {
        delete[] bitmap_buf;
        return -1;
    }

    // Mark inode as free
    bitmap_buf[inode_in_group / 8] &= ~(1 << (inode_in_group % 8));

    // Write back bitmap
    if (WriteBlock(mount, mount->group_desc[group].bg_inode_bitmap, bitmap_buf) != 0) {
        delete[] bitmap_buf;
        return -1;
    }

    // Update free inode count
    mount->group_desc[group].bg_free_inodes_count++;
    mount->superblock.s_free_inodes_count++;

    delete[] bitmap_buf;
    return 0;
}

// Helper function to get block number from inode pointers
std::uint32_t GetBlock(Inode *inode, std::uint32_t block_idx, MountFs *mount, bool allocate) {
    if (!inode || !mount) {
        return 0;
    }

    // Check for valid block index (prevent overflow)
    if (block_idx > 65536) {  // Arbitrary reasonable limit
        return 0;
    }

    // Direct blocks (0-11)
    if (block_idx < 12) {
        if (inode->i_block[block_idx] == 0 && allocate) {
            inode->i_block[block_idx] = AllocBlock(mount);
            if (inode->i_block[block_idx]) {
                inode->i_blocks += (mount->block_size / 512);
            }
        }
        return inode->i_block[block_idx];
    }

    // Single indirect block (12)
    block_idx -= 12;
    if (block_idx < (mount->block_size / 4)) {
        if (inode->i_block[12] == 0) {
            if (!allocate) {
                return 0;
            }
            inode->i_block[12] = AllocBlock(mount);
            if (!inode->i_block[12]) {
                return 0;
            }
            inode->i_blocks += (mount->block_size / 512);

            // Zero the block - use stack buffer for small block sizes
            if (mount->block_size <= 4096) {
                char zero_buf[4096] = {0};
                WriteBlock(mount, inode->i_block[12], zero_buf);
            } else {
                // For large block sizes, allocate but with safety checks
                char *zero_buf = new char[mount->block_size];
                if (zero_buf) {
                    memset(zero_buf, 0, mount->block_size);
                    WriteBlock(mount, inode->i_block[12], zero_buf);
                    delete[] zero_buf;
                }
            }
        }

        // Read indirect block - use stack buffer for small block sizes
        std::uint32_t block = 0;
        if (mount->block_size <= 4096) {
            // Use stack-allocated buffer for safety
            char block_buf[4096];
            if (ReadBlock(mount, inode->i_block[12], block_buf) == 0) {
                std::uint32_t *indirect = reinterpret_cast<std::uint32_t *>(block_buf);
                block              = indirect[block_idx];

                if (block == 0 && allocate) {
                    indirect[block_idx] = AllocBlock(mount);
                    if (indirect[block_idx]) {
                        block = indirect[block_idx];
                        inode->i_blocks += (mount->block_size / 512);
                        WriteBlock(mount, inode->i_block[12], block_buf);
                    }
                }
            }
        } else {
            // For large block sizes, use dynamic allocation with safety checks
            std::uint32_t *indirect = new std::uint32_t[mount->block_size / 4];
            if (indirect) {
                if (ReadBlock(mount, inode->i_block[12], indirect) == 0) {
                    block = indirect[block_idx];

                    if (block == 0 && allocate) {
                        indirect[block_idx] = AllocBlock(mount);
                        if (indirect[block_idx]) {
                            block = indirect[block_idx];
                            inode->i_blocks += (mount->block_size / 512);
                            WriteBlock(mount, inode->i_block[12], indirect);
                        }
                    }
                }
                delete[] indirect;
            }
        }

        return block;
    }

    // Double indirect block (13) - Not implemented for simplicity
    // Triple indirect block (14) - Not implemented for simplicity

    return 0;
}

}  // namespace ext2
