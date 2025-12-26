#ifndef VFS_H
#define VFS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Seek constants
#define SEEK_SET 0  // Seek from beginning
#define SEEK_CUR 1  // Seek from current position
#define SEEK_END 2  // Seek from end of file

namespace vfs {

// Forward declarations
struct File;
struct DirEntry;
struct MountFs;
struct FileSystem;

// Mount point structure
struct MountFs {
    char path[64];          // Mount path (e.g., "/")
    struct FileSystem *fs;  // Associated file system
    void *private_data;     // File system-specific data
    MountFs *next;     // Next mount point in the list
};

// File system operations
struct FileSystemOps {
    MountFs *(*mount)(FileSystem *fs, const char *device, const char *path, std::uint32_t flags);
    int (*umount)(MountFs *mount);
    File *(*open)(MountFs *mount, const char *path, std::uint32_t flags);
    int (*close)(File *file);
    ssize_t (*read)(File *file, void *buf, std::size_t count, std::uint64_t offset);
    ssize_t (*write)(File *file, const void *buf, std::size_t count, std::uint64_t offset);
    int (*mkdir)(MountFs *mount, const char *path);
    int (*rmdir)(MountFs *mount, const char *path);
    DirEntry *(*readdir)(MountFs *mount, const char *path, std::uint32_t index);
    int (*stat)(MountFs *mount, const char *path, void *statbuf);
};

// File system structure
struct FileSystem {
    char name[16];             // File system name (e.g., "ext2")
    struct FileSystemOps ops;  // File system operations
    struct FileSystem *next;   // Next file system in the list
};

// File structure
struct File {
    std::uint32_t flags;       // File flags (read/write mode)
    std::uint64_t position;    // Current file position
    MountFs *mount;  // Mount point of this file
    void *private_data;   // File system-specific file data
};

// Directory entry structure
struct DirEntry {
    char name[256];         // Entry name
    std::uint32_t inode;         // Inode number
    std::uint32_t type;          // Entry type (file, directory, etc.)
    struct DirEntry *next;  // Next entry (for linked list)
};


int Proc(int argc, char *argv[]);
int RegisterFileSystem(struct FileSystem *fs);
void RegisterFileSystems();
int Mount(const char *device, const char *path, const char *fs_type, std::uint32_t flags);
int Umount(const char *path);
struct File *Open(const char *path, std::uint32_t flags);
int Close(struct File *file);
ssize_t Read(struct File *file, void *buf, size_t count);
ssize_t Write(struct File *file, const void *buf, size_t count);
ssize_t Seek(struct File *file, std::int64_t offset, int whence);
struct DirEntry *ReadDir(const char *path, std::uint32_t index);
MountFs *FindMountPoint(const char *path);
void ExtractRelativePath(MountFs *mount, const char *full_path, char *rel_path,
                           size_t rel_path_len);
}  // namespace vfs

#endif  // VFS_H
