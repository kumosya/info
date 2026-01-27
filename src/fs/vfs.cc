
#include "kernel/vfs.h"

#include <cstdint>
#include <cstring>
#include <dirent.h>

#include "kernel/block.h"
#include "kernel/fs/ext2.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace vfs {

FileSystem *registered_filesystems = nullptr;
MountFs *mount_points              = nullptr;

int FileDescriptorTable::Alloc(File *file, std::uint32_t flags) {
    for (std::uint32_t i = 5; i < MAX_FD; i++) {
        if (!fds[i].used) {
            fds[i].used  = true;
            fds[i].file  = file;
            fds[i].flags = flags;
            return i;
        }
    }
    return -1;
}

int FileDescriptorTable::Free(int fd) {
    if (fd < 0 || fd >= MAX_FD || !fds[fd].used) {
        return -1;
    }
    if (fds[fd].file) {
        vfs::Close(fds[fd].file);
    }
    fds[fd].used  = false;
    fds[fd].file  = nullptr;
    fds[fd].flags = 0;
    return 0;
}

File *FileDescriptorTable::Get(int fd) {
    if (fd < 0 || fd >= MAX_FD || !fds[fd].used) {
        return nullptr;
    }
    return fds[fd].file;
}

int FileDescriptorTable::SetFlags(int fd, std::uint32_t flags) {
    if (fd < 0 || fd >= MAX_FD || !fds[fd].used) {
        return -1;
    }
    fds[fd].flags = flags;
    return 0;
}

int FdAlloc(File *file, std::uint32_t flags) {
    return task::current_proc->files.Alloc(file, flags);
}

int FdFree(int fd) { return task::current_proc->files.Free(fd); }

File *FdGet(int fd) { return task::current_proc->files.Get(fd); }

int FdDup(int oldfd) {
    File *file = FdGet(oldfd);
    if (!file) {
        return -1;
    }
    return FdAlloc(file, task::current_proc->files.fds[oldfd].flags);
}

int FdDup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= MAX_FD ||
        !task::current_proc->files.fds[oldfd].used) {
        return -1;
    }
    if (newfd < 0 || newfd >= MAX_FD) {
        return -1;
    }
    if (oldfd == newfd) {
        return newfd;
    }
    task::current_proc->files.fds[newfd].used =
        task::current_proc->files.fds[oldfd].used;
    task::current_proc->files.fds[newfd].file =
        task::current_proc->files.fds[oldfd].file;
    task::current_proc->files.fds[newfd].flags =
        task::current_proc->files.fds[oldfd].flags;
    return newfd;
}

int Service(int argc, char *argv[]) {
    RegisterFileSystems();

    // Wait for block devices to initialize
    task::ipc::Message msg;
    msg.dst_pid = 2;
    msg.type    = SYS_BLOCK_GET;
    strcpy(msg.data, "hda");
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);

    if (msg.num[0] == 0) {
        tty::printk("VFS: Failed to get device info from IPC response\n");
        return -1;
    }
    block::BlockDevice *dev =
        reinterpret_cast<block::BlockDevice *>(msg.num[0]);

    if (!dev) {
        tty::printk("VFS: Device 'hda' not found.\n");
        return -1;
    }

    FileSystem *fs = GetFileSystem("ext2");
    if (!fs) {
        tty::printk("VFS: ext2 filesystem not registered\n");
        return -2;
    }

    MountFs *root = fs->mount(fs, "hda1", "/", 0);
    if (!root) {
        tty::printk("VFS: Failed to mount root filesystem\n");
        return -3;
    }

    mount_points = root;

    tty::printk("VFS: Root filesystem mounted successfully\n");

    msg.dst_pid = 1;
    msg.type    = 0xa00;
    task::ipc::Send(&msg);

    bool reply;
    while (true) {
        reply = true;
        if (task::ipc::Receive(&msg)) {
            switch (msg.type) {
                case SYS_FS_OPEN: {
                    File *file = Open(msg.s.str, msg.s.arg);
                    int fd     = -1;
                    if (file) {
                        fd = msg.sender->files.Alloc(file, msg.s.arg);
                        if (fd < 0) {
                            Close(file);
                            file = nullptr;
                        }
                    }
                    tty::printk("VFS: Open %s fd %d\n", msg.s.str, fd);
                    msg.num[0] = fd;
                    break;
                }
                case SYS_FS_READ: {
                    int fd      = static_cast<int>(msg.num[0]);
                    File *f     = msg.sender->files.Get(fd);
                    ssize_t ret = -1;
                    if (f) {
                        ret = Read(f, reinterpret_cast<void *>(msg.num[1]),
                                   static_cast<std::uint64_t>(msg.num[2]));
                    }
                    msg.num[0] = ret;
                    break;
                }
                case SYS_FS_WRITE: {
                    int fd      = static_cast<int>(msg.num[0]);
                    File *f     = msg.sender->files.Get(fd);
                    ssize_t ret = -1;
                    if (f) {
                        ret =
                            Write(f, reinterpret_cast<const void *>(msg.num[1]),
                                  static_cast<std::uint64_t>(msg.num[2]));
                    }
                    msg.num[0] = ret;
                    break;
                }
                case SYS_FS_CLOSE: {
                    int fd     = static_cast<int>(msg.num[0]);
                    msg.num[0] = msg.sender->files.Free(fd);
                    break;
                }
                case SYS_FS_LSEEK: {
                    int fd      = static_cast<int>(msg.num[0]);
                    File *f     = msg.sender->files.Get(fd);
                    ssize_t ret = -1;
                    if (f) {
                        ret = Seek(f, static_cast<off_t>(msg.num[1]),
                                   static_cast<int>(msg.num[2]));
                    }
                    msg.num[0] = ret;
                    break;
                }
                case SYS_FS_DUP: {
                    int oldfd = static_cast<int>(msg.num[0]);
                    int newfd = -1;
                    File *f   = msg.sender->files.Get(oldfd);
                    if (f) {
                        newfd = msg.sender->files.Alloc(
                            f, msg.sender->files.fds[oldfd].flags);
                    }
                    msg.num[0] = newfd;
                    break;
                }
                case SYS_FS_DUP2: {
                    int oldfd  = static_cast<int>(msg.num[0]);
                    int newfd  = static_cast<int>(msg.num[1]);
                    msg.num[0] = FdDup2(oldfd, newfd);
                    break;
                }
                case SYS_FS_OPENDIR:
                    // msg.num[0] = reinterpret_cast<uint64_t>(
                    //     Opendir(msg.s.str, msg.s.arg));
                    break;
                case SYS_FS_READDIR:
                    msg.num[0] = reinterpret_cast<uint64_t>(
                        Readdir(msg.s.str, msg.s.arg));
                    break;
                case SYS_FS_CLOSEDIR:
                    // msg.num[0] = reinterpret_cast<uint64_t>(
                    //     Closedir(msg.s.str, msg.s.arg));
                    break;
                default:
                    tty::printk("VFS: Unknown message type: %d\n", msg.type);
                    reply = false;
                    break;
            }
            if (reply) {
                msg.dst_pid = msg.sender->pid;
                msg.sender  = task::current_proc;
                task::ipc::Send(&msg);
            }
        }
    }
    return 0;
}

int RegisterFileSystem(FileSystem *fs) {
    if (fs == nullptr || strlen(fs->name) == 0) {
        return -1;
    }

    FileSystem *current = registered_filesystems;

    while (registered_filesystems != nullptr) {
        if (strcmp(current->name, fs->name) == 0) {
            return -2;
        }
        current = current->next;
    }

    fs->next               = registered_filesystems;
    registered_filesystems = fs;

    return 0;
}

FileSystem *GetFileSystem(const char *name) {
    if (!name) {
        return nullptr;
    }

    FileSystem *current = registered_filesystems;
    while (current != nullptr) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }

    return nullptr;
}

int Mount(const char *device, const char *path, const char *fs_type,
          std::uint32_t flags) {
    if (!device || !path || !fs_type) {
        return -1;
    }

    FileSystem *fs = GetFileSystem(fs_type);
    if (!fs || !fs->mount) {
        return -2;
    }

    MountFs *current = mount_points;
    while (current != nullptr) {
        if (strcmp(current->GetPath(), path) == 0) {
            return -3;
        }
        current = current->next;
    }

    MountFs *mount = fs->mount(fs, device, path, flags);
    if (!mount) {
        return -4;
    }

    mount->next  = mount_points;
    mount_points = mount;

    return 0;
}

int Umount(const char *path) {
    if (!path) {
        return -1;
    }

    MountFs *mount = nullptr;
    MountFs *prev  = nullptr;

    if (mount_points != nullptr && strcmp(mount_points->GetPath(), path) == 0) {
        mount        = mount_points;
        mount_points = mount->next;
    } else {
        prev = mount_points;
        while (prev != nullptr && prev->next != nullptr) {
            if (strcmp(prev->next->GetPath(), path) == 0) {
                mount      = prev->next;
                prev->next = mount->next;
                break;
            }
            prev = prev->next;
        }
    }

    if (!mount) {
        tty::printk("VFS: Path '%s' is not mounted\n", path);
        return -2;
    }

    if (mount->umount) {
        mount->umount(mount);
    }

    delete mount;

    tty::printk("VFS: Unmounted '%s'\n", path);
    return 0;
}

File *Open(const char *path, std::uint32_t flags) {
    if (!path) {
        return nullptr;
    }

    MountFs *mount = FindMountPoint(path);
    if (!mount) {
        tty::printk("VFS: No mount point for path '%s'\n", path);
        return nullptr;
    }

    char rel_path[256];
    ExtractRelativePath(mount, path, rel_path, sizeof(rel_path));

    if (!mount->open) {
        tty::printk("VFS: open() not supported\n");
        return nullptr;
    }

    File *file = mount->open(mount, rel_path, flags);
    if (!file) {
        tty::printk("VFS: Failed to open file '%s'\n", path);
        return nullptr;
    }

    file->mount = mount;

    return file;
}

int Close(File *file) {
    if (!file) {
        return -1;
    }

    MountFs *mount = file->mount;
    if (!mount || !mount->close) {
        return -2;
    }

    return mount->close(file);
}

ssize_t Read(File *file, void *buf, std::size_t count) {
    if (!file || !buf || count == 0) {
        return -1;
    }

    MountFs *mount = file->mount;
    if (!mount || !mount->read) {
        return -2;
    }

    ssize_t ret = mount->read(file, buf, count, file->position);
    if (ret > 0) {
        file->position += ret;
    }

    return ret;
}

ssize_t Write(File *file, const void *buf, std::size_t count) {
    if (!file || !buf || count == 0) {
        return -1;
    }

    MountFs *mount = file->mount;
    if (!mount || !mount->write) {
        return -2;
    }

    ssize_t ret = mount->write(file, buf, count, file->position);
    if (ret > 0) {
        file->position += ret;
    }

    return ret;
}

ssize_t Seek(File *file, std::int64_t offset, int whence) {
    if (!file) {
        return -1;
    }

    std::int64_t new_pos;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = file->position + offset;
            break;
        case SEEK_END:
            new_pos = -1;
            break;
        default:
            return -3;
    }

    if (new_pos < 0) {
        return -4;
    }

    file->position = new_pos;
    return file->position;
}

DirEntry *Readdir(const char *path, std::uint32_t index) {
    if (!path) {
        return nullptr;
    }

    MountFs *mount = FindMountPoint(path);
    if (!mount) {
        return nullptr;
    }

    char rel_path[256];
    ExtractRelativePath(mount, path, rel_path, sizeof(rel_path));

    if (!mount->readdir) {
        return nullptr;
    }

    return mount->readdir(mount, rel_path, index);
}

MountFs *FindMountPoint(const char *path) {
    if (!path || path[0] != '/') {
        return nullptr;
    }

    MountFs *best_match = nullptr;
    size_t best_len     = 0;

    MountFs *current = mount_points;
    while (current) {
        size_t mount_len = strlen(current->GetPath());

        if (strncmp(path, current->GetPath(), mount_len) == 0) {
            if (mount_len > best_len) {
                best_len   = mount_len;
                best_match = current;
            }
        }

        current = current->next;
    }

    return best_match;
}
void ExtractRelativePath(MountFs *mount, const char *full_path, char *rel_path,
                         size_t rel_path_len) {
    if (!mount || !full_path || !rel_path) {
        return;
    }

    const char *mount_path = mount->GetPath();
    size_t mount_len       = strlen(mount_path);
    size_t full_len        = strlen(full_path);

    if (mount_len == 1 && mount_path[0] == '/' && full_len == 1) {
        strncpy(rel_path, "/", rel_path_len - 1);
        return;
    }

    const char *rel = full_path + mount_len;
    if (*rel == '/') {
        rel++;
    }

    if (*rel == '\0') {
        rel_path[0] = '/';
        rel_path[1] = '\0';
    } else {
        size_t copy_len = rel_path_len - 1;
        if (strlen(rel) < copy_len) {
            copy_len = strlen(rel);
        }
        memcpy(rel_path, rel, copy_len);
        rel_path[copy_len] = '\0';
    }
}

}  // namespace vfs
