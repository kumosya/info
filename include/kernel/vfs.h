#ifndef INFO_KERNEL_VFS_H_
#define INFO_KERNEL_VFS_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Seek constants
#define SEEK_SET 0  // Seek from beginning
#define SEEK_CUR 1  // Seek from current position
#define SEEK_END 2  // Seek from end of file

// Forward declarations
class File;
class DirEntry;
class MountFs;
class FileSystem;

// File system structure
class FileSystem {
   public:
    int RegisterFileSystem();
    FileSystem *GetFileSystem(const char *name);

    MountFs *(*mount)(FileSystem *fs, const char *device, const char *path,
                      std::uint32_t flags);
    int (*umount)(MountFs *mount);
    File *(*open)(MountFs *mount, const char *path, std::uint32_t flags);
    int (*close)(File *file);
    ssize_t (*read)(File *file, void *buf, std::size_t count,
                    std::uint64_t offset);
    ssize_t (*write)(File *file, const void *buf, std::size_t count,
                     std::uint64_t offset);
    int (*mkdir)(MountFs *mount, const char *path);
    int (*rmdir)(MountFs *mount, const char *path);
    DirEntry *(*readdir)(MountFs *mount, const char *path, std::uint32_t index);
    int (*stat)(MountFs *mount, const char *path, void *statbuf);

   private:
    char name[16];     // File system name (e.g., "ext2")
    FileSystem *next;  // Next file system in the list
};

// Mount point
class MountFs : public FileSystem {
   public:
    int Mount(const char *device, const char *path, const char *fs_type,
              std::uint32_t flags);
    int Umount(const char *path);

    char *GetPath() { return path; }
    MountFs *next;  // Next mount point in the list
   protected:
    char path[64];       // Mount path (e.g., "/")
    void *private_data;  // File system-specific data
};

// File structure
class File {
   public:
    File *Open(const char *path, std::uint32_t flags);
    int Close();
    ssize_t Read(void *buf, size_t count);
    ssize_t Write(const void *buf, size_t count);
    ssize_t Seek(std::int64_t offset, int whence);

   protected:
    std::uint32_t flags;     // File flags (read/write mode)
    std::uint64_t position;  // Current file position
    MountFs *mount;          // Mount point of this file
    void *private_data;      // File system-specific file data
};

// Directory entry structure
struct DirEntry {
    char name[256];         // Entry name
    std::uint32_t inode;    // Inode number
    std::uint32_t type;     // Entry type (file, directory, etc.)
    struct DirEntry *next;  // Next entry (for linked list)
};

namespace vfs {

void RegisterFileSystems();
int Proc(int argc, char *argv[]);
DirEntry *ReadDir(File *dir, DirEntry *dirent);
MountFs *FindMountPoint(const char *path);
void ExtractRelativePath(MountFs *mount, const char *full_path, char *rel_path,
                         size_t rel_path_len);

}  // namespace vfs

#endif  // INFO_KERNEL_VFS_H_
