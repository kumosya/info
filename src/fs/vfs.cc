

#include <cstdint>
#include <cstring>


#include "kernel/vfs.h"
#include "kernel/block.h"
#include "kernel/fs/ext2.h"
#include "kernel/tty.h"

namespace vfs {

// Global file systems list
FileSystem *registered_filesystems = nullptr;

// Global mount points list
MountFs *mount_points = nullptr;

// VFS initialization
int Proc(int argc, char *argv[]) {
    // 查找第一个Block设备
    /*block::Device *dev = block::FindDevice("hda");
    if (dev) {
        registered_filesystems = nullptr;
        mount_points           = nullptr;

        // Register built-in file systems
        RegisterFileSystems();
        int i = 0;
        while (++i) {
            // Mount root filesystem (ext2) on /
            int ret = Mount("hda", "/", "ext2", 0);
            if (ret != 0 && i > 10) {
                // Failed to mount root filesystem
                tty::Panic("Failed to mount root filesystem on /");
                return -2;
            }
            if (ret == 0) {
                break;
            }
        }
    } else {
        tty::Panic("hda not found.");
        return -1;
    }*/

    while (true) {
    }

    return 1;
}

// List directory entries
DirEntry *Readdir(const char *path, std::uint32_t index) {
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
    if (!mount->readdir) {
        return nullptr;
    }

    return mount->readdir(mount, rel_path, index);
}

// Helper function to find mount point for a path
MountFs *FindMountPoint(const char *path) {
    if (!path || path[0] != '/') {
        return nullptr;
    }

    MountFs *best_match = nullptr;
    size_t best_len     = 0;

    MountFs *current = mount_points;
    while (current) {
        size_t mount_len = strlen(current->GetPath());

        // Check if the path starts with the mount point path
        if (strncmp(path, current->GetPath(), mount_len) == 0) {
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

    const char *mount_path = mount->GetPath();
    size_t mount_len       = strlen(mount_path);
    size_t full_len        = strlen(full_path);

    // If the mount path is '/' and the full path is also '/', the relative path
    // is '/' or empty
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

}  // namespace vfs

// Register a new file system
int FileSystem::RegisterFileSystem() {
    if (strlen(name) == 0) {
        return -1;
    }

    // Check if already registered
    FileSystem *current = vfs::registered_filesystems;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return -2;  // Already registered
        }
        current = current->next;
    }

    // Add to list
    next                        = vfs::registered_filesystems;
    vfs::registered_filesystems = this;

    return 0;
}

// Find a registered file system by name
FileSystem *FileSystem::GetFileSystem(const char *name) {
    if (!name) {
        return nullptr;
    }

    FileSystem *current = vfs::registered_filesystems;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }

    return nullptr;
}

// Mount a file system
int MountFs::Mount(const char *device, const char *path, const char *fs_type,
                   std::uint32_t flags) {
    if (!device || !path || !fs_type) {
        return -1;
    }

    // Find file system
    FileSystem *fs = GetFileSystem(fs_type);
    if (!fs || !fs->mount) {
        return -2;
    }

    // Check if path is already mounted
    MountFs *current = vfs::mount_points;
    while (current) {
        if (strcmp(current->GetPath(), path) == 0) {
            return -3;
        }
        current = current->next;
    }

    // Call file system-specific mount
    MountFs *mount = fs->mount(fs, device, path, flags);
    if (!mount) {
        return -4;
    }

    // Add to mount points list
    // mount->fs    = fs;
    mount->next       = vfs::mount_points;
    vfs::mount_points = mount;

    return 0;
}

// Unmount a file system
int MountFs::Umount(const char *path) {
    if (!path) {
        return -1;
    }

    // Find mount point
    MountFs *mount = nullptr;
    MountFs *prev  = nullptr;

    if (vfs::mount_points && strcmp(vfs::mount_points->path, path) == 0) {
        mount             = vfs::mount_points;
        vfs::mount_points = mount->next;
    } else {
        prev = vfs::mount_points;
        while (prev && prev->next) {
            if (strcmp(prev->next->GetPath(), path) == 0) {
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
    if (mount->umount) {
        mount->umount(mount);
    }

    // Free mount point
    delete mount;

    tty::printf("VFS: Unmounted '%s'\n", path);
    return 0;
}

// Open a file
File *File::Open(const char *path, std::uint32_t flags) {
    if (!path) {
        return nullptr;
    }

    // Find the mount point for this path
    MountFs *mount = vfs::FindMountPoint(path);
    if (!mount) {
        tty::printf("VFS: No mount point for path '%s'\n", path);
        return nullptr;
    }

    // Extract the relative path (after mount point)
    char rel_path[256];
    vfs::ExtractRelativePath(mount, path, rel_path, sizeof(rel_path));

    // Call file system-specific open
    if (!mount->open) {
        tty::printf("VFS: open() not supported by file system\n");
        return nullptr;
    }

    File *file = mount->open(mount, rel_path, flags);
    if (!file) {
        tty::printf("VFS: Failed to open file '%s'\n", path);
        return nullptr;
    }

    // Set file mount point
    file->mount = mount;

    return file;
}

// Close a file
int File::Close() {
    MountFs *mount = mount;
    if (!mount || !mount->close) {
        return -2;
    }

    return mount->close(this);
}

// Read from a file
ssize_t File::Read(void *buf, size_t count) {
    if (!buf || count == 0) {
        return -1;
    }

    MountFs *mount = mount;
    if (!mount || !mount->read) {
        return -2;
    }

    return mount->read(this, buf, count, position);
}

// Write to a file
ssize_t File::Write(const void *buf, size_t count) {
    if (!buf || count == 0) {
        return -1;
    }

    MountFs *mount = mount;
    if (!mount || !mount->write) {
        return -2;
    }

    return mount->write(this, buf, count, position);
}

// Seek in a file
ssize_t File::Seek(std::int64_t offset, int whence) {
    if (!mount) {
        return -1;
    }

    /* TODO */
    return -1;
}
