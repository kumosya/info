#include "fs/ext2.h"
#include "block.h"
#include "tty.h"
#include "vfs.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
using namespace std;

namespace ext2 {

// Read a block from the device
int read_block(Mount *mount, uint32_t block_num, void *buf) {
    if (!mount || !buf || !mount->device) {
        return -1;
    }

    // Check block size is valid (multiple of 512 bytes)
    if (mount->block_size % 512 != 0 || mount->block_size == 0) {
        return -2;
    }

    // Calculate sectors per block
    uint32_t sectors_per_block = mount->block_size / 512;

    // Calculate sector offset (including partition offset) and read
    // block_num is relative to the partition start (block 0 is first block of partition)
    // Check for potential overflow
    uint64_t block_sectors = (uint64_t)block_num * sectors_per_block;
    if (block_sectors > mount->device->sector_count - mount->partition_offset) {
        return -3; // Block is beyond device capacity
    }

    uint64_t sector = mount->partition_offset + block_sectors;

    return block::read(mount->device, sector, sectors_per_block, buf);
}

// Write a block to the device
int write_block(Mount *mount, uint32_t block_num, const void *buf) {
    if (!mount || !buf) {
        return -1;
    }

    // Calculate sector offset (including partition offset) and write
    uint64_t sector        = mount->partition_offset + block_num * (mount->block_size / 512);
    uint32_t sectors_count = mount->block_size / 512;

    return block::write(mount->device, sector, sectors_count, buf);
}

Mount *mount(block::Device *device) {
    if (!device) {
        return nullptr;
    }

    // Allocate mount structure
    Mount *mount = new Mount();
    if (!mount) {
        return nullptr;
    }

    mount->device = device;
    mount->next   = nullptr; // No longer managed by EXT2

    // Default to no partition offset (whole device is EXT2)
    mount->partition_offset = 0;

    // Read MBR to check for partition table
    char mbr_buf[512];
    if (block::read(device, 0, 1, mbr_buf) == 0) {
        // Check MBR signature directly from buffer
        uint16_t signature = ((unsigned char)mbr_buf[510]) | ((unsigned char)mbr_buf[511] << 8);

        if (signature == 0xAA55) {
            // Look for Linux EXT2 partitions (system_id = 0x83)
            for (int i = 0; i < 4; i++) {
                int offset = 446 + i * sizeof(PartitionEntry);

                // Parse partition entry directly from buffer
                uint8_t system_id  = (unsigned char)mbr_buf[offset + 4];
                uint32_t start_lba = ((unsigned char)mbr_buf[offset + 8]) | ((unsigned char)mbr_buf[offset + 9] << 8) |
                                     ((unsigned char)mbr_buf[offset + 10] << 16) | ((unsigned char)mbr_buf[offset + 11] << 24);
                uint32_t sectors_count = ((unsigned char)mbr_buf[offset + 12]) | ((unsigned char)mbr_buf[offset + 13] << 8) |
                                         ((unsigned char)mbr_buf[offset + 14] << 16) | ((unsigned char)mbr_buf[offset + 15] << 24);

                if (system_id == 0x83 && sectors_count > 0) {
                    // Set partition offset with correct value
                    mount->partition_offset = start_lba;
                    break;
                }
            }
        }
    }

    // Try reading superblock from common locations
    char superblock_buf[512];
    int superblock_found = 0;

    // Try standard superblock locations first
    uint64_t common_sectors[] = {
        mount->partition_offset + 2,     // Standard superblock (block 1)
        mount->partition_offset + 32768, // Alternate superblock for large disks
        mount->partition_offset + 1,     // Just in case it's at sector 1
        mount->partition_offset + 0      // Very beginning of partition
    };

    for (int i = 0; i < sizeof(common_sectors) / sizeof(common_sectors[0]); i++) {
        uint64_t sector = common_sectors[i];

        if (block::read(device, sector, 1, superblock_buf) == 0) {
            mount->superblock = *(Superblock *)superblock_buf;

            if (mount->superblock.s_magic == 0xEF53) {
                superblock_found = 1;
                break;
            }
        }
    }

    // If no superblock found yet, do a linear search through the first 1000 sectors
    if (!superblock_found) {
        for (uint64_t i = 0; i < 1000; i++) {
            uint64_t sector = mount->partition_offset + i;

            if (block::read(device, sector, 1, superblock_buf) == 0) {
                // Check magic number directly from buffer
                uint16_t magic = ((unsigned char)superblock_buf[56]) | ((unsigned char)superblock_buf[57] << 8);

                if (magic == 0xEF53) {
                    mount->superblock = *(Superblock *)superblock_buf;
                    superblock_found  = 1;
                    break;
                }
            }
        }
    }

    // Check if we found a valid superblock
    if (!superblock_found) {
        delete mount;
        return nullptr;
    }

    // Calculate block size
    mount->block_size       = 1024 << mount->superblock.s_log_block_size;
    mount->blocks_per_group = mount->superblock.s_blocks_per_group;
    mount->inodes_per_group = mount->superblock.s_inodes_per_group;

    // Calculate number of block groups
    mount->groups_count = (mount->superblock.s_blocks_count + mount->blocks_per_group - 1) / mount->blocks_per_group;

    // Allocate group descriptors
    mount->group_desc = new GroupDescriptor[mount->groups_count];
    if (!mount->group_desc) {
        delete mount;
        return nullptr;
    }

    // Read group descriptors
    uint32_t group_desc_block  = mount->superblock.s_first_data_block + 1;
    uint32_t group_desc_size   = mount->groups_count * sizeof(GroupDescriptor);
    uint32_t group_desc_blocks = (group_desc_size + mount->block_size - 1) / mount->block_size;

    char *group_desc_buf = new char[group_desc_blocks * mount->block_size];
    if (!group_desc_buf) {
        delete[] mount->group_desc;
        delete mount;
        return nullptr;
    }

    // Read all group descriptor blocks
    for (uint32_t i = 0; i < group_desc_blocks; i++) {
        if (read_block(mount, group_desc_block + i, group_desc_buf + i * mount->block_size) != 0) {
            delete[] group_desc_buf;
            delete[] mount->group_desc;
            delete mount;
            return nullptr;
        }
    }

    // Copy to group descriptor array
    for (uint32_t i = 0; i < mount->groups_count; i++) {
        mount->group_desc[i] = *(GroupDescriptor *)(group_desc_buf + i * sizeof(GroupDescriptor));
    }

    delete[] group_desc_buf;

    return mount;
}

int umount(Mount *mount) {
    if (!mount) {
        return -1;
    }

    // Free resources
    delete[] mount->group_desc;
    delete mount;

    return 0;
}

// Read an inode from disk
int read_inode(Mount *mount, uint32_t inode_num, Inode *inode) {
    if (!mount || !inode || inode_num == 0 || inode_num > mount->superblock.s_inodes_count) {
        return -1;
    }

    // Calculate block group and inode offset
    uint32_t group = (inode_num - 1) / mount->inodes_per_group;
    uint32_t index = (inode_num - 1) % mount->inodes_per_group;

    // Calculate inode table block and offset
    uint32_t inode_table_block  = mount->group_desc[group].bg_inode_table;
    uint32_t inode_size         = mount->superblock.s_inode_size;
    uint32_t inode_table_offset = index * inode_size;
    uint32_t block_offset       = inode_table_offset / mount->block_size;
    uint32_t block_inode_offset = inode_table_offset % mount->block_size;

    // Allocate buffer and read block
    char *buf = new char[mount->block_size];
    if (!buf) {
        return -1;
    }

    if (read_block(mount, inode_table_block + block_offset, buf) != 0) {
        delete[] buf;
        return -1;
    }

    // Copy inode data
    *inode = *(Inode *)(buf + block_inode_offset);

    delete[] buf;
    return 0;
}

// Write an inode to disk
int write_inode(Mount *mount, uint32_t inode_num, const Inode *inode) {
    if (!mount || !inode || inode_num == 0 || inode_num > mount->superblock.s_inodes_count) {
        return -1;
    }

    // Calculate block group and inode offset
    uint32_t group = (inode_num - 1) / mount->inodes_per_group;
    uint32_t index = (inode_num - 1) % mount->inodes_per_group;

    // Calculate inode table block and offset
    uint32_t inode_table_block  = mount->group_desc[group].bg_inode_table;
    uint32_t inode_size         = mount->superblock.s_inode_size;
    uint32_t inode_table_offset = index * inode_size;
    uint32_t block_offset       = inode_table_offset / mount->block_size;
    uint32_t block_inode_offset = inode_table_offset % mount->block_size;

    // Allocate buffer and read block
    char *buf = new char[mount->block_size];
    if (!buf) {
        return -1;
    }

    if (read_block(mount, inode_table_block + block_offset, buf) != 0) {
        delete[] buf;
        return -1;
    }

    // Copy inode data
    *(Inode *)(buf + block_inode_offset) = *inode;

    // Write back to disk
    int ret = write_block(mount, inode_table_block + block_offset, buf);

    delete[] buf;
    return ret;
}

// Allocate a free block
uint32_t alloc_block(Mount *mount) {
    if (!mount) {
        return 0;
    }

    // Find a group with free blocks
    for (uint32_t group = 0; group < mount->groups_count; group++) {
        if (mount->group_desc[group].bg_free_blocks_count == 0) {
            continue;
        }

        // Read block bitmap
        char *bitmap_buf = new char[mount->block_size];
        if (!bitmap_buf) {
            continue;
        }

        if (read_block(mount, mount->group_desc[group].bg_block_bitmap, bitmap_buf) != 0) {
            delete[] bitmap_buf;
            continue;
        }

        // Find first free block in bitmap
        uint32_t block_in_group = 0;

        for (uint32_t i = 0; i < mount->block_size; i++) {
            if (bitmap_buf[i] == 0xFF) {
                continue;
            }

            for (int j = 0; j < 8; j++) {
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
        if (write_block(mount, mount->group_desc[group].bg_block_bitmap, bitmap_buf) != 0) {
            delete[] bitmap_buf;
            return 0;
        }

        // Update free block count
        mount->group_desc[group].bg_free_blocks_count--;
        mount->superblock.s_free_blocks_count--;

        delete[] bitmap_buf;

        // Calculate absolute block number
        uint32_t block_num = group * mount->blocks_per_group + block_in_group;

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
int free_block(Mount *mount, uint32_t block_num) {
    if (!mount || block_num == 0 || block_num >= mount->superblock.s_blocks_count) {
        return -1;
    }

    // Calculate group and block in group
    uint32_t group          = block_num / mount->blocks_per_group;
    uint32_t block_in_group = block_num % mount->blocks_per_group;

    // Read block bitmap
    char *bitmap_buf = new char[mount->block_size];
    if (!bitmap_buf) {
        return -1;
    }

    if (read_block(mount, mount->group_desc[group].bg_block_bitmap, bitmap_buf) != 0) {
        delete[] bitmap_buf;
        return -1;
    }

    // Mark block as free
    bitmap_buf[block_in_group / 8] &= ~(1 << (block_in_group % 8));

    // Write back bitmap
    if (write_block(mount, mount->group_desc[group].bg_block_bitmap, bitmap_buf) != 0) {
        delete[] bitmap_buf;
        return -1;
    }

    // Update free block count
    mount->group_desc[group].bg_free_blocks_count++;
    mount->superblock.s_free_blocks_count++;

    delete[] bitmap_buf;
    return 0;
}

// Allocate a free inode
uint32_t alloc_inode(Mount *mount) {
    if (!mount) {
        return 0;
    }

    // Find a group with free inodes
    for (uint32_t group = 0; group < mount->groups_count; group++) {
        if (mount->group_desc[group].bg_free_inodes_count == 0) {
            continue;
        }

        // Read inode bitmap
        char *bitmap_buf = new char[mount->block_size];
        if (!bitmap_buf) {
            continue;
        }

        if (read_block(mount, mount->group_desc[group].bg_inode_bitmap, bitmap_buf) != 0) {
            delete[] bitmap_buf;
            continue;
        }

        // Find first free inode in bitmap
        uint32_t inode_in_group = 0;
        for (uint32_t i = 0; i < mount->block_size; i++) {
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
        write_block(mount, mount->group_desc[group].bg_inode_bitmap, bitmap_buf);

        // Update free inode count
        mount->group_desc[group].bg_free_inodes_count--;
        mount->superblock.s_free_inodes_count--;

        delete[] bitmap_buf;

        // Calculate absolute inode number
        uint32_t inode_num = group * mount->inodes_per_group + inode_in_group + 1;

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
int free_inode(Mount *mount, uint32_t inode_num) {
    if (!mount || inode_num == 0 || inode_num > mount->superblock.s_inodes_count) {
        return -1;
    }

    // Calculate group and inode in group
    uint32_t group          = (inode_num - 1) / mount->inodes_per_group;
    uint32_t inode_in_group = (inode_num - 1) % mount->inodes_per_group;

    // Read inode bitmap
    char *bitmap_buf = new char[mount->block_size];
    if (!bitmap_buf) {
        return -1;
    }

    if (read_block(mount, mount->group_desc[group].bg_inode_bitmap, bitmap_buf) != 0) {
        delete[] bitmap_buf;
        return -1;
    }

    // Mark inode as free
    bitmap_buf[inode_in_group / 8] &= ~(1 << (inode_in_group % 8));

    // Write back bitmap
    if (write_block(mount, mount->group_desc[group].bg_inode_bitmap, bitmap_buf) != 0) {
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
uint32_t get_block(Inode *inode, uint32_t block_idx, Mount *mount, bool allocate) {
    if (!inode || !mount) {
        return 0;
    }

    // Check for valid block index (prevent overflow)
    if (block_idx > 65536) { // Arbitrary reasonable limit
        return 0;
    }

    // Direct blocks (0-11)
    if (block_idx < 12) {
        if (inode->i_block[block_idx] == 0 && allocate) {
            inode->i_block[block_idx] = alloc_block(mount);
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
            inode->i_block[12] = alloc_block(mount);
            if (!inode->i_block[12]) {
                return 0;
            }
            inode->i_blocks += (mount->block_size / 512);

            // Zero the block - use stack buffer for small block sizes
            if (mount->block_size <= 4096) {
                char zero_buf[4096] = {0};
                write_block(mount, inode->i_block[12], zero_buf);
            } else {
                // For large block sizes, allocate but with safety checks
                char *zero_buf = new char[mount->block_size];
                if (zero_buf) {
                    memset(zero_buf, 0, mount->block_size);
                    write_block(mount, inode->i_block[12], zero_buf);
                    delete[] zero_buf;
                }
            }
        }

        // Read indirect block - use stack buffer for small block sizes
        uint32_t block = 0;
        if (mount->block_size <= 4096) {
            // Use stack-allocated buffer for safety
            char block_buf[4096];
            if (read_block(mount, inode->i_block[12], block_buf) == 0) {
                uint32_t *indirect = reinterpret_cast<uint32_t *>(block_buf);
                block              = indirect[block_idx];

                if (block == 0 && allocate) {
                    indirect[block_idx] = alloc_block(mount);
                    if (indirect[block_idx]) {
                        block = indirect[block_idx];
                        inode->i_blocks += (mount->block_size / 512);
                        write_block(mount, inode->i_block[12], block_buf);
                    }
                }
            }
        } else {
            // For large block sizes, use dynamic allocation with safety checks
            uint32_t *indirect = new uint32_t[mount->block_size / 4];
            if (indirect) {
                if (read_block(mount, inode->i_block[12], indirect) == 0) {
                    block = indirect[block_idx];

                    if (block == 0 && allocate) {
                        indirect[block_idx] = alloc_block(mount);
                        if (indirect[block_idx]) {
                            block = indirect[block_idx];
                            inode->i_blocks += (mount->block_size / 512);
                            write_block(mount, inode->i_block[12], indirect);
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

// File operations
static File *file_list = nullptr;

File *open(Mount *mount, uint32_t inode_num, uint32_t flags) {
    if (!mount || inode_num == 0 || inode_num > mount->superblock.s_inodes_count) {
        return nullptr;
    }

    // Allocate file structure
    File *file = new File();
    if (!file) {
        return nullptr;
    }

    file->mount     = mount;
    file->inode_num = inode_num;
    file->offset    = 0;
    file->flags     = flags;
    file->next      = file_list;
    file_list       = file;

    // Read inode
    if (read_inode(mount, inode_num, &file->inode) != 0) {
        close(file);
        return nullptr;
    }

    // Handle open flags
    if (flags & O_TRUNC) {
        // Truncate file to zero
        file->inode.i_size = 0;
        write_inode(mount, inode_num, &file->inode);
    }

    if (flags & O_APPEND) {
        // Seek to end of file
        file->offset = file->inode.i_size;
    }

    return file;
}

int close(File *file) {
    if (!file) {
        return -1;
    }

    // Remove from file list
    if (file_list == file) {
        file_list = file->next;
    } else {
        File *prev = file_list;
        while (prev && prev->next != file) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = file->next;
        }
    }

    // Free file structure
    delete file;

    return 0;
}

ssize_t read(File *file, void *buf, size_t count) {
    if (!file || !buf || count == 0) {
        return -1;
    }

    // Check if we're at the end of the file
    if (file->offset >= file->inode.i_size) {
        return 0;
    }

    // Calculate how much we can read
    size_t to_read = count;
    if (file->offset + to_read > file->inode.i_size) {
        to_read = file->inode.i_size - file->offset;
    }

    // Calculate block size and initial block
    uint32_t block_size      = file->mount->block_size;
    uint32_t block_idx       = file->offset / block_size;
    uint32_t offset_in_block = file->offset % block_size;

    char *data_buf    = static_cast<char *>(buf);
    size_t bytes_read = 0;

    while (to_read > 0) {
        // Get block number
        uint32_t block = get_block(&file->inode, block_idx, file->mount, false);
        if (!block) {
            break;
        }

        // Read block
        char *block_buf = new char[block_size];
        if (!block_buf) {
            break;
        }

        if (read_block(file->mount, block, block_buf) != 0) {
            delete[] block_buf;
            break;
        }

        // Calculate how much to copy from this block
        size_t copy_size = block_size - offset_in_block;
        if (copy_size > to_read) {
            copy_size = to_read;
        }

        // Copy data
        for (size_t i = 0; i < copy_size; i++) {
            data_buf[bytes_read + i] = block_buf[offset_in_block + i];
        }

        delete[] block_buf;

        // Update counters
        bytes_read += copy_size;
        to_read -= copy_size;
        file->offset += copy_size;
        block_idx++;
        offset_in_block = 0;
    }

    return bytes_read;
}

ssize_t write(File *file, const void *buf, size_t count) {
    if (!file || !buf || count == 0) {
        return -1;
    }

    // Check if file is writable
    if (!(file->flags & (O_WRONLY | O_RDWR))) {
        return -1;
    }

    // Calculate block size and initial block
    uint32_t block_size      = file->mount->block_size;
    uint32_t block_idx       = file->offset / block_size;
    uint32_t offset_in_block = file->offset % block_size;

    const char *data_buf = static_cast<const char *>(buf);
    size_t bytes_written = 0;

    while (count > 0) {
        // Get or allocate block
        uint32_t block = get_block(&file->inode, block_idx, file->mount, true);
        if (!block) {
            break;
        }

        // Read existing block if we're not writing the whole block
        char *block_buf = new char[block_size];
        if (!block_buf) {
            break;
        }

        if (offset_in_block > 0) {
            if (read_block(file->mount, block, block_buf) != 0) {
                delete[] block_buf;
                break;
            }
        } else {
            // Zero the block if we're writing from the beginning
            memset(block_buf, 0, block_size);
        }

        // Calculate how much to copy to this block
        size_t copy_size = block_size - offset_in_block;
        if (copy_size > count) {
            copy_size = count;
        }

        // Copy data
        memcpy(block_buf + offset_in_block, data_buf + bytes_written, copy_size);

        // Write back block
        if (write_block(file->mount, block, block_buf) != 0) {
            delete[] block_buf;
            break;
        }

        delete[] block_buf;

        // Update counters
        bytes_written += copy_size;
        count -= copy_size;
        file->offset += copy_size;
        block_idx++;
        offset_in_block = 0;

        // Update file size if needed
        if (file->offset > file->inode.i_size) {
            file->inode.i_size = file->offset;
        }
    }

    // Write back inode if we wrote anything
    if (bytes_written > 0) {
        write_inode(file->mount, file->inode_num, &file->inode);
    }

    return bytes_written;
}

int lseek(File *file, uint64_t offset, int whence) {
    if (!file) {
        return -1;
    }

    uint64_t new_offset = file->offset;

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

// Helper function to find a directory entry by name
static uint32_t find_dir_entry(Mount *mount, uint32_t dir_inode_num, const char *name) {
    if (!mount || !name || dir_inode_num == 0) {
        return 0;
    }

    // Read directory inode
    Inode dir_inode;
    if (read_inode(mount, dir_inode_num, &dir_inode) != 0) {
        return 0;
    }

    // Check if it's a directory
    if (!(dir_inode.i_mode & 0x4000)) { // S_IFDIR
        return 0;
    }

    // Allocate buffer for directory blocks
    uint8_t *buffer = new uint8_t[mount->block_size];
    if (!buffer) {
        return 0;
    }

    uint32_t found_inode    = 0;
    uint64_t current_offset = 0;

    // Iterate through directory blocks
    while (current_offset < dir_inode.i_size) {
        // Calculate block number and offset within block
        uint32_t block_idx    = current_offset / mount->block_size;
        uint32_t block_offset = current_offset % mount->block_size;

        // Get block number from inode
        uint32_t block_num = get_block(&dir_inode, block_idx, mount);
        if (block_num == 0) {
            break;
        }

        // Read block
        if (read_block(mount, block_num, buffer) != 0) {
            break;
        }

        // Iterate through directory entries in this block
        uint32_t entry_offset = block_offset;
        while (entry_offset < mount->block_size && current_offset < dir_inode.i_size) {
            // Check if we have at least enough space for the minimum directory entry
            if (entry_offset + 8 > mount->block_size) {
                break;
            }

            // Get pointer to directory entry
            DirEntry *entry = reinterpret_cast<DirEntry *>(buffer + entry_offset);

            // Check for invalid entry
            if (entry->rec_len == 0) {
                break;
            }

            // Check if this entry is beyond the block boundary
            if (entry_offset + entry->rec_len > mount->block_size) {
                break;
            }

            // Skip deleted entries (inode == 0)
            if (entry->inode != 0 && entry->name_len > 0) {
                // Compare names properly
                if (strncmp(entry->name, name, entry->name_len) == 0 && name[entry->name_len] == '\0') {
                    found_inode = entry->inode;
                    goto cleanup;
                }
            }

            // Move to next entry
            entry_offset += entry->rec_len;
            current_offset += entry->rec_len;
        }

        // Move to next block
        current_offset = (block_idx + 1) * mount->block_size;
    }

cleanup:
    delete[] buffer;
    return found_inode;
}

// Resolve path to inode number
uint32_t resolve_path(Mount *mount, const char *path, uint32_t start_inode) {
    if (!mount || !path) {
        return 0;
    }

    // Start with the root inode or the specified start inode
    uint32_t current_inode = start_inode;

    // Skip leading slashes
    while (*path == '/') {
        path++;
    }

    // If path is empty, return the current inode
    if (*path == '\0') {
        return current_inode;
    }

    // Allocate buffer for path component
    char component[256];

    while (*path != '\0') {
        // Extract next path component
        uint32_t i = 0;
        while (*path != '/' && *path != '\0' && i < sizeof(component) - 1) {
            component[i++] = *path++;
        }
        component[i] = '\0';

        // Skip any trailing slashes
        while (*path == '/') {
            path++;
        }

        // Handle special cases
        if (strcmp(component, ".") == 0) {
            // Current directory, do nothing
            continue;
        } else if (strcmp(component, "..") == 0) {
            // Parent directory
            // For root directory, parent is itself
            if (current_inode != 2) {
                // Get parent directory inode by looking up ".." entry
                current_inode = find_dir_entry(mount, current_inode, "..");
                if (current_inode == 0) {
                    return 0;
                }
            }
        } else {
            // Regular directory entry
            current_inode = find_dir_entry(mount, current_inode, component);
            if (current_inode == 0) {
                return 0;
            }
        }
    }

    return current_inode;
}

} // namespace ext2
