#ifndef VFS_H
#define VFS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace std;

// Forward declarations
struct File;
struct DirEntry;
struct Mount;
struct FileSystem;

// Seek constants
#define SEEK_SET 0 // Seek from beginning
#define SEEK_CUR 1 // Seek from current position
#define SEEK_END 2 // Seek from end of file

// File system operations
struct FileSystemOps {
    struct Mount *(*mount)(FileSystem *fs, const char *device, const char *path, uint32_t flags);
    int (*umount)(struct Mount *mount);
    struct File *(*open)(struct Mount *mount, const char *path, uint32_t flags);
    int (*close)(struct File *file);
    ssize_t (*read)(struct File *file, void *buf, size_t count, uint64_t offset);
    ssize_t (*write)(struct File *file, const void *buf, size_t count, uint64_t offset);
    int (*mkdir)(struct Mount *mount, const char *path);
    int (*rmdir)(struct Mount *mount, const char *path);
    struct DirEntry *(*readdir)(struct Mount *mount, const char *path, uint32_t index);
    int (*stat)(struct Mount *mount, const char *path, void *statbuf);
};

// File system structure
struct FileSystem {
    char name[16];            // File system name (e.g., "ext2")
    struct FileSystemOps ops; // File system operations
    struct FileSystem *next;  // Next file system in the list
};

// Mount point structure
struct Mount {
    char path[64];         // Mount path (e.g., "/")
    struct FileSystem *fs; // Associated file system
    void *private_data;    // File system-specific data
    struct Mount *next;    // Next mount point in the list
};

// File structure
struct File {
    uint32_t flags;      // File flags (read/write mode)
    uint64_t position;   // Current file position
    struct Mount *mount; // Mount point of this file
    void *private_data;  // File system-specific file data
};

// Directory entry structure
struct DirEntry {
    char name[256];        // Entry name
    uint32_t inode;        // Inode number
    uint32_t type;         // Entry type (file, directory, etc.)
    struct DirEntry *next; // Next entry (for linked list)
};

namespace vfs {
void init();
int register_filesystem(struct FileSystem *fs);
void register_filesystems();
int mount(const char *device, const char *path, const char *fs_type, uint32_t flags);
int umount(const char *path);
struct File *open(const char *path, uint32_t flags);
int close(struct File *file);
ssize_t read(struct File *file, void *buf, size_t count);
ssize_t write(struct File *file, const void *buf, size_t count);
ssize_t seek(struct File *file, int64_t offset, int whence);
struct DirEntry *readdir(const char *path, uint32_t index);
Mount *find_mount_point(const char *path);
void extract_relative_path(Mount *mount, const char *full_path, char *rel_path, size_t rel_path_len);
Mount *vfs_mount_ext2(FileSystem *fs, const char *device, const char *path, uint32_t flags);
int vfs_umount_ext2(Mount *mount);
struct File *vfs_open_ext2(Mount *mount, const char *path, uint32_t flags);
int vfs_close_ext2(struct File *file);
ssize_t vfs_read_ext2(struct File *file, void *buf, size_t count, uint64_t offset);
ssize_t vfs_write_ext2(struct File *file, const void *buf, size_t count, uint64_t offset);
struct DirEntry *vfs_readdir_ext2(Mount *mount, const char *path, uint32_t index);
} // namespace vfs

#endif // VFS_H
