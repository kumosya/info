#ifndef INFO_KERNEL_VFS_H_
#define INFO_KERNEL_VFS_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Seek constants
#define SEEK_SET 0  // Seek from beginning
#define SEEK_CUR 1  // Seek from current position
#define SEEK_END 2  // Seek from end of file

namespace vfs {
// Forward declarations
class File;
class DirEntry;
class MountFs;
class FileSystem;

// File system structure
class FileSystem {
   public:
    char name[16];     // File system name (e.g., "ext2")
    FileSystem *next;  // Next file system in the list

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
    int (*unlink)(MountFs *mount, const char *path);
    DirEntry *(*readdir)(MountFs *mount, const char *path, std::uint32_t index);
    int (*stat)(MountFs *mount, const char *path, void *statbuf);
};

// Mount point
class MountFs : public FileSystem {
   public:
    char path[64];       // Mount path (e.g., "/")
    MountFs *next;       // Next mount point in the list
    void *private_data;  // File system-specific data

    char *GetPath() { return path; }
};

// File structure
class File {
public:
    std::uint32_t flags;     // File flags (read/write mode)
    std::uint64_t position;  // Current file position
    MountFs *mount;          // Mount point of this file
    void *private_data;      // File system-specific file data
    char path[256];          // File path for directory operations
};

#define MAX_FD 64

struct FileDescriptor {
    bool used;
    File *file;
    std::uint32_t flags;
};

class FileDescriptorTable {
   public:
    FileDescriptor fds[MAX_FD];
    FileDescriptorTable() {
        for (std::uint32_t i = 0; i < MAX_FD; i++) {
            fds[i].used  = false;
            fds[i].file  = nullptr;
            fds[i].flags = 0;
        }
        fds[0].used = true;
        fds[0].file = nullptr;
        fds[1].used = true;
        fds[1].file = nullptr;
    }
    ~FileDescriptorTable() {/* TODO */}
    int Alloc(File *file, std::uint32_t flags);
    int Free(int fd);
    File *Get(int fd);
    int SetFlags(int fd, std::uint32_t flags);
};

int FdAlloc(File *file, std::uint32_t flags);
int FdFree(int fd);
File *FdGet(int fd);
int FdDup(int oldfd);
int FdDup2(int oldfd, int newfd);

struct DirEntry {
    char name[256];
    std::uint32_t inode;
    std::uint32_t type;
    struct DirEntry *next;
};

struct DirStream {
    char path[256];
    std::uint32_t index;
    MountFs *mount;
};

DirEntry *Readdir(const char *path, std::uint32_t index);
int Mkdir(const char *path);
int Rmdir(const char *path);
int Unlink(const char *path);
DirStream *Opendir(const char *path);
int Closedir(DirStream *dir);
int Chdir(DirStream *current_dir, const char *path);
char *GetCwd(DirStream *current_dir, char *buf, size_t size);
int Service(void *arg);
DirEntry *Readdir(const char *path, std::uint32_t index);
MountFs *FindMountPoint(const char *path);
void ExtractRelativePath(MountFs *mount, const char *full_path, char *rel_path,
                         size_t rel_path_len);
void RegisterFileSystems();
int RegisterFileSystem(FileSystem *fs);
FileSystem *GetFileSystem(const char *name);
int Mount(const char *device, const char *path, const char *fs_type,
          std::uint32_t flags);
int Umount(const char *path);
File *Open(const char *path, std::uint32_t flags);
int Close(File *file);
ssize_t Read(File *file, void *buf, std::size_t count);
ssize_t Write(File *file, const void *buf, std::size_t count);
ssize_t Seek(File *file, std::int64_t offset, int whence);

MountFs *Ext2Mount(class FileSystem *fs, const char *device, const char *path,
                   std::uint32_t flags);
int Ext2Umount(MountFs *mount);
File *Ext2Open(MountFs *mount, const char *path, std::uint32_t flags);
int Ext2Close(File *file);
ssize_t Ext2Read(File *file, void *buf, std::size_t count,
                 std::uint64_t offset);
ssize_t Ext2Write(File *file, const void *buf, std::size_t count,
                  std::uint64_t offset);
int Ext2Mkdir(MountFs *mount, const char *path);
int Ext2Rmdir(MountFs *mount, const char *path);
int Ext2Unlink(MountFs *mount, const char *path);
DirEntry *Ext2Readdir(MountFs *mount, const char *path, std::uint32_t index);
int Ext2Stat(MountFs *mount, const char *path, void *statbuf);

}  // namespace vfs

#endif  // INFO_KERNEL_VFS_H_
