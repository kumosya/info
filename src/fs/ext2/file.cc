#include "fs/ext2.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "block.h"
#include "tty.h"
#include "vfs.h"

namespace ext2 {

// Read a block from the device
int ReadBlock(MountFs *mount, std::uint32_t block_num, void *buf) {
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
        return -3;  // Block is beyond device capacity
    }

    uint64_t sector = mount->partition_offset + block_sectors;

    return block::Read(mount->device, sector, sectors_per_block, buf);
}

// Write a block to the device
int WriteBlock(MountFs *mount, std::uint32_t block_num, const void *buf) {
    if (!mount || !buf) {
        return -1;
    }

    // Calculate sector offset (including partition offset) and write
    uint64_t sector        = mount->partition_offset + block_num * (mount->block_size / 512);
    uint32_t sectors_count = mount->block_size / 512;

    return block::Write(mount->device, sector, sectors_count, buf);
}


// File operations
static File *file_list = nullptr;

File *Open(MountFs *mount, std::uint32_t inode_num, std::uint32_t flags) {
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
    if (ReadInode(mount, inode_num, &file->inode) != 0) {
        Close(file);
        return nullptr;
    }

    // Handle open flags
    if (flags & O_TRUNC) {
        // Truncate file to zero
        file->inode.i_size = 0;
        WriteInode(mount, inode_num, &file->inode);
    }

    if (flags & O_APPEND) {
        // Seek to end of file
        file->offset = file->inode.i_size;
    }

    return file;
}

int Close(File *file) {
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

ssize_t Read(File *file, void *buf, std::size_t count) {
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
        std::uint32_t block_num = GetBlock(&file->inode, block_idx, file->mount, false);
        if (!block_num) {
            break;
        }

        // Read block
        char *block_buf = new char[block_size];
        if (!block_buf) {
            break;
        }

        if (ReadBlock(file->mount, block_num, block_buf) != 0) {
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

ssize_t Write(File *file, const void *buf, std::size_t count) {
    if (!file || !buf || count == 0) {
        return -1;
    }

    // Check if file is writable
    if (!(file->flags & (O_WRONLY | O_RDWR))) {
        return -1;
    }

    // Calculate block size and initial block
    std::uint32_t block_size      = file->mount->block_size;
    std::uint32_t block_idx       = file->offset / block_size;
    std::uint32_t offset_in_block = file->offset % block_size;

    const char *data_buf = static_cast<const char *>(buf);
    std::size_t bytes_written = 0;

    while (count > 0) {
        // Get or allocate block
        std::uint32_t block = GetBlock(&file->inode, block_idx, file->mount, true);
        if (!block) {
            break;
        }

        // Read existing block if we're not writing the whole block
        char *block_buf = new char[block_size];
        if (!block_buf) {
            break;
        }

        if (offset_in_block > 0) {
            if (ReadBlock(file->mount, block, block_buf) != 0) {
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
        if (WriteBlock(file->mount, block, block_buf) != 0) {
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
        WriteInode(file->mount, file->inode_num, &file->inode);
    }

    return bytes_written;
}

}