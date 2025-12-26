#include "vfs.h"

#include <cstdint>
#include <cstring>

#include "block.h"
#include "fs/ext2.h"
#include "tty.h"

namespace vfs {

// Global file systems list
static FileSystem *registered_filesystems = nullptr;

// Global mount points list
static MountFs *mount_points = nullptr;

// VFS initialization
int Proc(int argc, char *argv[]) {
    // 查找第一个IDE设备
    block::Device *dev = block::FindDevice("hda");
    if (dev) {
        registered_filesystems = nullptr;
        mount_points           = nullptr;

        // Register built-in file systems
        RegisterFileSystems();

        // Mount root filesystem (ext2) on /
        int ret = Mount("hda", "/", "ext2", 0);
        if (ret != 0) {
            // Failed to mount root filesystem
            tty::Panic("Failed to mount root filesystem on /");
            return -2;
        }
    } else {
        tty::Panic("hda not found.");
        return -1;
    }

    while (true) {
        for (int i = 0; i < 0xffffff; i++);
        tty::printf("F");
    }

    return 1;
}

// Register a new file system
int RegisterFileSystem(FileSystem *fs) {
    if (!fs || strlen(fs->name) == 0) {
        return -1;
    }

    // Check if already registered
    FileSystem *current = registered_filesystems;
    while (current) {
        if (strcmp(current->name, fs->name) == 0) {
            return -2;  // Already registered
        }
        current = current->next;
    }

    // Add to list
    fs->next               = registered_filesystems;
    registered_filesystems = fs;

    return 0;
}

// Find a registered file system by name
FileSystem *GetFileSystem(const char *name) {
    if (!name) {
        return nullptr;
    }

    FileSystem *current = registered_filesystems;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }

    return nullptr;
}

// Mount a file system
int Mount(const char *device, const char *path, const char *fs_type, std::uint32_t flags) {
    if (!device || !path || !fs_type) {
        return -1;
    }

    // Find file system
    FileSystem *fs = GetFileSystem(fs_type);
    if (!fs || !fs->ops.mount) {
        return -2;
    }

    // Check if path is already mounted
    MountFs *current = mount_points;
    while (current) {
        if (strcmp(current->path, path) == 0) {
            return -3;
        }
        current = current->next;
    }

    // Call file system-specific mount
    MountFs *mount = fs->ops.mount(fs, device, path, flags);
    if (!mount) {
        return -4;
    }

    // Add to mount points list
    mount->fs    = fs;
    mount->next  = mount_points;
    mount_points = mount;

    return 0;
}

// Unmount a file system
int Umount(const char *path) {
    if (!path) {
        return -1;
    }

    // Find mount point
    MountFs *mount = nullptr;
    MountFs *prev  = nullptr;

    if (mount_points && strcmp(mount_points->path, path) == 0) {
        mount        = mount_points;
        mount_points = mount->next;
    } else {
        prev = mount_points;
        while (prev && prev->next) {
            if (strcmp(prev->next->path, path) == 0) {
                mount      = prev->next;
                prev->next = mount->next;
                break;
            }
            prev = prev->next;
        }
    }

    if (!mount) {
        tty::printf("VFS: Path '%s' is not mounted\n", path);
        return -2;
    }

    // Call file system-specific umount
    if (mount->fs->ops.umount) {
        mount->fs->ops.umount(mount);
    }

    // Free mount point
    delete mount;

    tty::printf("VFS: Unmounted '%s'\n", path);
    return 0;
}

// Open a file
File *Open(const char *path, std::uint32_t flags) {
    if (!path) {
        return nullptr;
    }

    // Find the mount point for this path
    MountFs *mount = FindMountPoint(path);
    if (!mount) {
        tty::printf("VFS: No mount point for path '%s'\n", path);
        return nullptr;
    }

    // Extract the relative path (after mount point)
    char rel_path[256];
    ExtractRelativePath(mount, path, rel_path, sizeof(rel_path));

    // Call file system-specific open
    if (!mount->fs->ops.open) {
        tty::printf("VFS: open() not supported by file system\n");
        return nullptr;
    }

    File *file = mount->fs->ops.open(mount, rel_path, flags);
    if (!file) {
        tty::printf("VFS: Failed to open file '%s'\n", path);
        return nullptr;
    }

    // Set file mount point
    file->mount = mount;

    return file;
}

// Close a file
int Close(File *file) {
    if (!file) {
        return -1;
    }

    MountFs *mount = file->mount;
    if (!mount || !mount->fs->ops.close) {
        return -2;
    }

    return mount->fs->ops.close(file);
}

// Read from a file
ssize_t Read(File *file, void *buf, size_t count) {
    if (!file || !buf || count == 0) {
        return -1;
    }

    MountFs *mount = file->mount;
    if (!mount || !mount->fs->ops.read) {
        return -2;
    }

    return mount->fs->ops.read(file, buf, count, file->position);
}

// Write to a file
ssize_t Write(File *file, const void *buf, size_t count) {
    if (!file || !buf || count == 0) {
        return -1;
    }

    MountFs *mount = file->mount;
    if (!mount || !mount->fs->ops.write) {
        return -2;
    }

    return mount->fs->ops.write(file, buf, count, file->position);
}

// Seek in a file
ssize_t Seek(File *file, std::int64_t offset, int whence) {
    if (!file) {
        return -1;
    }

    std::uint64_t new_pos = file->position;

    // For EXT2 files, we can access the inode directly through the private_data
    if (file->private_data) {
        ext2::File *ext2_file = static_cast<ext2::File *>(file->private_data);
        std::uint64_t file_size    = ext2_file->inode.i_size;

        // Calculate new position based on whence
        switch (whence) {
            case SEEK_SET:
                new_pos = static_cast<std::uint64_t>(offset);
                break;
            case SEEK_CUR:
                new_pos += offset;
                break;
            case SEEK_END:
                new_pos = file_size + offset;
                break;
            default:
                return -1;
        }

        // Ensure the new position is within bounds (0 <= new_pos)
        if (new_pos < 0) {
            new_pos = 0;
        }

        // Update positions in both VFS file and underlying EXT2 file
        file->position    = new_pos;
        ext2_file->offset = new_pos;

        return new_pos;
    }

    // Fallback if we can't access the file system specific data
    return -1;
}

// List directory entries
DirEntry *readdir(const char *path, std::uint32_t index) {
    if (!path) {
        return nullptr;
    }

    // Find the mount point for this path
    MountFs *mount = FindMountPoint(path);
    if (!mount) {
        return nullptr;
    }

    // Extract the relative path (after mount point)
    char rel_path[256];
    ExtractRelativePath(mount, path, rel_path, sizeof(rel_path));

    // Call file system-specific readdir
    if (!mount->fs->ops.readdir) {
        return nullptr;
    }

    return mount->fs->ops.readdir(mount, rel_path, index);
}

// Helper function to find mount point for a path
MountFs *FindMountPoint(const char *path) {
    if (!path || path[0] != '/') {
        return nullptr;
    }

    MountFs *best_match = nullptr;
    size_t best_len   = 0;

    MountFs *current = mount_points;
    while (current) {
        size_t mount_len = strlen(current->path);

        // Check if the path starts with the mount point path
        if (strncmp(path, current->path, mount_len) == 0) {
            // Check if this is a better match than the current best
            if (mount_len > best_len) {
                best_len   = mount_len;
                best_match = current;
            }
        }

        current = current->next;
    }

    return best_match;
}

// Helper function to extract relative path after mount point
void ExtractRelativePath(MountFs *mount, const char *full_path, char *rel_path,
                           size_t rel_path_len) {
    if (!mount || !full_path || !rel_path) {
        return;
    }

    const char *mount_path = mount->path;
    size_t mount_len       = strlen(mount_path);
    size_t full_len        = strlen(full_path);

    // If the mount path is '/' and the full path is also '/', the relative path is '/' or empty
    if (mount_len == 1 && mount_path[0] == '/' && full_len == 1) {
        strncpy(rel_path, "/", rel_path_len - 1);
        return;
    }

    // Extract the part after the mount path
    const char *rel = full_path + mount_len;
    if (*rel == '/') {
        rel++;
    }

    // If the relative path is empty, set it to '/'
    if (*rel == '\0') {
        strncpy(rel_path, "/", rel_path_len - 1);
    } else {
        strncpy(rel_path, rel, rel_path_len - 1);
    }

    // Ensure null termination
    rel_path[rel_path_len - 1] = '\0';
}

// EXT2 integration functions

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

}  // namespace vfs
