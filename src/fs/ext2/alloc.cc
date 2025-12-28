#include "fs/ext2.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "block.h"
#include "tty.h"
#include "vfs.h"

namespace ext2 {

// Allocate a free block
std::uint32_t AllocBlock(MountFs *mount) {
    if (!mount) {
        return 0;
    }

    // Find a group with free blocks
    for (std::uint32_t group = 0; group < mount->groups_count; group++) {
        if (mount->group_desc[group].bg_free_blocks_count == 0) {
            continue;
        }

        // Read block bitmap
        char *bitmap_buf = new char[mount->block_size];
        if (!bitmap_buf) {
            continue;
        }

        if (ReadBlock(mount, mount->group_desc[group].bg_block_bitmap, bitmap_buf) != 0) {
            delete[] bitmap_buf;
            continue;
        }

        // Find first free block in bitmap
        std::uint32_t block_in_group = 0;

        for (std::uint32_t i = 0; i < mount->block_size; i++) {
            if (bitmap_buf[i] == 0xFF) {
                continue;
            }

            for (std::uint32_t j = 0; j < 8; j++) {
                if (!(bitmap_buf[i] & (1 << j))) {
                    block_in_group = i * 8 + j;
                    goto found;
                }
            }
        }

        delete[] bitmap_buf;
        continue;

    found:
        // Mark block as used
        bitmap_buf[block_in_group / 8] |= (1 << (block_in_group % 8));

        // Write back bitmap
        if (WriteBlock(mount, mount->group_desc[group].bg_block_bitmap, bitmap_buf) != 0) {
            delete[] bitmap_buf;
            return 0;
        }

        // Update free block count
        mount->group_desc[group].bg_free_blocks_count--;
        mount->superblock.s_free_blocks_count--;

        delete[] bitmap_buf;

        // Calculate absolute block number
        std::uint32_t block_num = group * mount->blocks_per_group + block_in_group;

        // Make sure block is within valid range
        if (block_num >= mount->superblock.s_blocks_count || block_num == 0) {
            continue;
        }

        return block_num;
    }

    // No free blocks found
    return 0;
}

// Free a block
int FreeBlock(MountFs *mount, std::uint32_t block_num) {
    if (!mount || block_num == 0 || block_num >= mount->superblock.s_blocks_count) {
        return -1;
    }

    // Calculate group and block in group
    std::uint32_t group          = block_num / mount->blocks_per_group;
    std::uint32_t block_in_group = block_num % mount->blocks_per_group;

    // Read block bitmap
    char *bitmap_buf = new char[mount->block_size];
    if (!bitmap_buf) {
        return -1;
    }

    if (ReadBlock(mount, mount->group_desc[group].bg_block_bitmap, bitmap_buf) != 0) {
        delete[] bitmap_buf;
        return -1;
    }

    // Mark block as free
    bitmap_buf[block_in_group / 8] &= ~(1 << (block_in_group % 8));

    // Write back bitmap
    if (WriteBlock(mount, mount->group_desc[group].bg_block_bitmap, bitmap_buf) != 0) {
        delete[] bitmap_buf;
        return -1;
    }

    // Update free block count
    mount->group_desc[group].bg_free_blocks_count++;
    mount->superblock.s_free_blocks_count++;

    delete[] bitmap_buf;
    return 0;
}
}