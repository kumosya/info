#include "vfs.h"

#include <cstdint>
#include <cstring>

#include "block.h"
#include "fs/ext2.h"
#include "tty.h"

// EXT2 integration functions

namespace vfs {
// Mount EXT2 file system
MountFs *VfsMountExt2(FileSystem *fs, const char *device, const char *path, std::uint32_t flags) {
    (void)fs;     // Unused
    (void)flags;  // Unused

    // Get block device by name
    block::Device *block_dev = block::FindDevice(device);
    if (!block_dev) {
        tty::printf("VFS: Block device '%s' not found\n", device);
        return nullptr;
    }

    // Mount EXT2 file system
    ext2::MountFs *ext2_mount = ext2::Mount(block_dev);
    if (!ext2_mount) {
        return nullptr;
    }

    // Allocate VFS mount point
    MountFs *mount = new MountFs();
    if (!mount) {
        ext2::Umount(ext2_mount);
        return nullptr;
    }

    // Set mount point information
    strncpy(mount->path, path, sizeof(mount->path) - 1);
    mount->private_data = ext2_mount;

    return mount;
}

// Unmount EXT2 file system
int VfsUmountExt2(MountFs *mount) {
    if (!mount || !mount->private_data) {
        return -1;
    }

    // Get EXT2 mount point
    ext2::MountFs *ext2_mount = static_cast<ext2::MountFs *>(mount->private_data);

    // Unmount EXT2 file system
    int ret = ext2::Umount(ext2_mount);
    if (ret != 0) {
        return ret;
    }

    return 0;
}

// Open file in EXT2
File *VfsOpenExt2(MountFs *mount, const char *path, std::uint32_t flags) {
    if (!mount || !mount->private_data || !path) {
        return nullptr;
    }

    // Get EXT2 mount point
    ext2::MountFs *ext2_mount = static_cast<ext2::MountFs *>(mount->private_data);

    // Resolve path to inode number
    std::uint32_t inode_num = ext2::ResolvePath(ext2_mount, path);

    // Handle file creation
    if (inode_num == 0) {
        if (flags & O_CREAT) {
            // For simplicity, we'll just return an error for now since we don't have full file
            // creation support
            return nullptr;
        } else {
            return nullptr;
        }
    }

    // Open the file using EXT2
    ext2::File *ext2_file = ext2::Open(ext2_mount, inode_num, flags);
    if (!ext2_file) {
        return nullptr;
    }

    // Allocate VFS file
    File *file = new File();
    if (!file) {
        ext2::Close(ext2_file);
        return nullptr;
    }

    // Set file information
    file->flags        = flags;
    file->position     = 0;
    file->private_data = ext2_file;

    return file;
}

// Close file in EXT2
int VfsCloseExt2(File *file) {
    if (!file || !file->private_data) {
        return -1;
    }

    // Get EXT2 file
    ext2::File *ext2_file = static_cast<ext2::File *>(file->private_data);

    // Close the file using EXT2
    int ret = ext2::Close(ext2_file);

    // Free VFS file
    delete file;

    return ret;
}

// Read from file in EXT2
ssize_t VfsReadExt2(File *file, void *buf, size_t count, std::uint64_t offset) {
    if (!file || !file->private_data || !buf || count == 0) {
        return -1;
    }

    // Get EXT2 file
    ext2::File *ext2_file = static_cast<ext2::File *>(file->private_data);

    // Update file offset if needed
    if (offset != ext2_file->offset) {
        ext2_file->offset = offset;
    }

    // Read from file using EXT2
    ssize_t ret = ext2::Read(ext2_file, buf, count);

    // Update VFS file position
    if (ret > 0) {
        file->position = ext2_file->offset;
    }

    return ret;
}

// Write to file in EXT2
ssize_t VfsWriteExt2(File *file, const void *buf, size_t count, std::uint64_t offset) {
    if (!file || !file->private_data || !buf || count == 0) {
        return -1;
    }

    // Get EXT2 file
    ext2::File *ext2_file = static_cast<ext2::File *>(file->private_data);

    // Update file offset if needed
    if (offset != ext2_file->offset) {
        ext2_file->offset = offset;
    }

    // Write to file using EXT2
    ssize_t ret = ext2::Write(ext2_file, buf, count);

    if (ret < 0) {
        return ret;
    } else if (ret == 0) {
        return -1;  // Treat 0 bytes written as failure
    } else {
        // Update VFS file position
        file->position = ext2_file->offset;
    }

    return ret;
}

// Read directory entries in EXT2
DirEntry *VfsReaddirExt2(MountFs *mount, const char *path, std::uint32_t index) {
    if (!mount || !mount->private_data || !path) {
        return nullptr;
    }

    // Get EXT2 mount point
    ext2::MountFs *ext2_mount = static_cast<ext2::MountFs *>(mount->private_data);

    // Resolve path to inode number
    std::uint32_t inode_num = ext2::ResolvePath(ext2_mount, path);
    if (inode_num == 0) {
        return nullptr;
    }

    // Open the directory file
    ext2::File *dir_file = ext2::Open(ext2_mount, inode_num, O_RDONLY);
    if (!dir_file) {
        return nullptr;
    }

    // Check if this is actually a directory
    if (!(dir_file->inode.i_mode & 0x4000)) {  // S_IFDIR flag
        ext2::Close(dir_file);
        return nullptr;
    }

    // Allocate buffer for reading directory blocks
    uint8_t *buffer = new uint8_t[ext2_mount->block_size];
    if (!buffer) {
        ext2::Close(dir_file);
        return nullptr;
    }

    DirEntry *found_entry   = nullptr;
    std::uint32_t current_index  = 0;
    std::uint64_t current_offset = 0;

    // Iterate through directory blocks
    while (current_offset < dir_file->inode.i_size) {
        // Calculate block number and offset within block
        std::uint32_t block_idx    = current_offset / ext2_mount->block_size;
        std::uint32_t block_offset = current_offset % ext2_mount->block_size;

        // Get block number from inode
    std::uint32_t block_num = ext2::GetBlock(&dir_file->inode, block_idx, ext2_mount);
        if (block_num == 0) {
            break;
        }

        // Read block
    if (ext2::ReadBlock(ext2_mount, block_num, buffer) != 0) {
            break;
        }

        // Iterate through directory entries in this block
        std::uint32_t entry_offset = block_offset;
        while (entry_offset < ext2_mount->block_size) {
            // Check if we've reached the end of the block or directory
            if (entry_offset + 8 > ext2_mount->block_size ||
                current_offset >= dir_file->inode.i_size) {
                break;
            }

            // Get pointer to directory entry
            ext2::DirEntry *ext2_entry = reinterpret_cast<ext2::DirEntry *>(buffer + entry_offset);

            // Validate record length to avoid infinite loops
            if (ext2_entry->rec_len < 8 ||
                ext2_entry->rec_len > (ext2_mount->block_size - entry_offset)) {
                break;
            }

            // Only count valid entries (inode != 0)
            if (ext2_entry->inode != 0 && ext2_entry->name_len <= 255) {
                // Check if this is the entry we're looking for
                if (current_index == index) {
                    // Allocate VFS directory entry
                    found_entry = new DirEntry();
                    if (found_entry) {
                        // Copy name (limit to 255 characters)
                        std::uint32_t name_len =
                            (ext2_entry->name_len < 255) ? ext2_entry->name_len : 255;
                        strncpy(found_entry->name, ext2_entry->name, name_len);
                        found_entry->name[name_len] = '\0';

                        // Set other fields
                        found_entry->inode = ext2_entry->inode;
                        found_entry->type  = ext2_entry->file_type;
                        found_entry->next  = nullptr;
                    }

                    // Cleanup and return
                    delete[] buffer;
                    ext2::Close(dir_file);
                    return found_entry;
                }
                current_index++;
            }

            // Always move to next entry, even if current entry is invalid
            entry_offset += ext2_entry->rec_len;
            current_offset += ext2_entry->rec_len;
        }

        // Move to next block
        current_offset = (block_idx + 1) * ext2_mount->block_size;
    }

    // Cleanup
    delete[] buffer;
    ext2::Close(dir_file);

    // No entry found at the given index
    return nullptr;
}


// Register all built-in file systems
void RegisterFileSystems() {
    // Register EXT2 file system
    static FileSystem ext2_fs;
    strncpy(ext2_fs.name, "ext2", sizeof(ext2_fs.name) - 1);

    // Map EXT2 operations to VFS operations
    ext2_fs.ops.mount   = VfsMountExt2;
    ext2_fs.ops.umount  = VfsUmountExt2;
    ext2_fs.ops.open    = VfsOpenExt2;
    ext2_fs.ops.close   = VfsCloseExt2;
    ext2_fs.ops.read    = VfsReadExt2;
    ext2_fs.ops.write   = VfsWriteExt2;
    ext2_fs.ops.mkdir   = nullptr;  // Not implemented yet
    ext2_fs.ops.rmdir   = nullptr;  // Not implemented yet
    ext2_fs.ops.readdir = VfsReaddirExt2;
    ext2_fs.ops.stat    = nullptr;  // Not implemented yet

    RegisterFileSystem(&ext2_fs);
}

}