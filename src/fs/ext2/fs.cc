
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "kernel/block.h"
#include "kernel/fs/ext2.h"
#include "kernel/mm.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

namespace vfs {

struct Ext2MountData {
    block::BlockDevice *device;
    Ext2SuperBlock super;
    std::uint32_t block_size;
    std::uint32_t ext2_lba;
};

struct Ext2FileData {
    std::uint32_t inode_num;
    Ext2Inode inode;
};

MountFs *Ext2Mount(FileSystem *fs, const char *device, const char *path,
                   std::uint32_t flags) {
    std::uint32_t partition_num = 0;
    const char *p               = device;
    while (*p && (*p < '0' || *p > '9')) {
        p++;
    }
    if (*p) {
        partition_num = atoi(p) - 1;
    }
    char device_name[4];
    strcpy(device_name, device);
    device_name[3] = '\0';

    block::BlockDevice *dev = block::FindDevice(device_name);
    if (!dev) {
        tty::printk("EXT2: Device '%s' not found\n", device_name);
        return nullptr;
    }

    Ext2MountData *mount_data = new Ext2MountData();
    if (!mount_data) {
        tty::printk("EXT2: Failed to allocate mount data\n");
        return nullptr;
    }

    mount_data->device = dev;

    std::uint8_t *mbr_buf = (std::uint8_t *)mm::page::Alloc(512);
    if (!mbr_buf) {
        tty::printk("EXT2: Failed to allocate MBR buffer\n");
        delete mount_data;
        return nullptr;
    }

    mount_data->ext2_lba = 0;

    if (dev->Read(0, 1, mbr_buf) == 0) {
        MBR *mbr = (MBR *)mbr_buf;
        if (mbr->signature == 0xAA55) {
            int partition_count = 0;
            for (int i = 0; i < 4; i++) {
                if (mbr->partitions[i].partition_type == 0x83) {
                    if (partition_count == partition_num) {
                        mount_data->ext2_lba = mbr->partitions[i].starting_lba;
                        // tty::printk("EXT2: Partition %u LBA offset: %u\n",
                        // partition_num, mount_data->ext2_lba);
                        break;
                    }
                    partition_count++;
                }
            }
            if (mount_data->ext2_lba == 0) {
                tty::printk("EXT2: Partition %u not found, using LBA 0\n",
                            partition_num);
            }
        }
    }
    mm::page::Free(mbr_buf);

    if (ext2::ReadSuperBlock(dev, &mount_data->super, mount_data->ext2_lba) !=
        0) {
        tty::printk("EXT2: Failed to read superblock\n");
        delete mount_data;
        return nullptr;
    }

    mount_data->block_size = 1024 << mount_data->super.s_log_block_size;

    MountFs *mount = new MountFs();
    if (!mount) {
        tty::printk("EXT2: Failed to allocate mount point\n");
        delete mount_data;
        return nullptr;
    }

    strncpy(mount->path, path, sizeof(mount->path) - 1);
    mount->path[sizeof(mount->path) - 1] = '\0';
    mount->private_data                  = mount_data;
    mount->mount                         = Ext2Mount;
    mount->umount                        = Ext2Umount;
    mount->open                          = Ext2Open;
    mount->close                         = Ext2Close;
    mount->read                          = Ext2Read;
    mount->write                         = Ext2Write;
    mount->mkdir                         = Ext2Mkdir;
    mount->rmdir                         = Ext2Rmdir;
    mount->readdir                       = Ext2Readdir;
    mount->stat                          = Ext2Stat;

    // tty::printk("EXT2: Mounted '%s' on '%s'\n", device, path);
    return mount;
}

int Ext2Umount(MountFs *mount) {
    if (!mount || !mount->private_data) {
        return -1;
    }

    Ext2MountData *mount_data = (Ext2MountData *)mount->private_data;
    delete mount_data;
    mount->private_data = nullptr;

    // tty::printk("EXT2: Unmounted '%s'\n", mount->path);
    return 0;
}

File *Ext2Open(MountFs *mount, const char *path, std::uint32_t flags) {
    if (!mount || !path) {
        return nullptr;
    }

    Ext2MountData *mount_data = (Ext2MountData *)mount->private_data;
    if (!mount_data) {
        return nullptr;
    }

    std::uint32_t inode_num;

    char abs_path[256];
    const char *lookup_path = path;
    if (path[0] != '/') {
        abs_path[0] = '/';
        size_t len  = strlen(path);
        if (len >= sizeof(abs_path) - 1) {
            return nullptr;
        }
        memcpy(abs_path + 1, path, len);
        abs_path[len + 1] = '\0';
        lookup_path       = abs_path;
    }

    int ret_path =
        ext2::LookupPath(mount_data->device, &mount_data->super, lookup_path,
                         &inode_num, mount_data->ext2_lba);
    if (ret_path != 0) {
        if (flags & 0x02) {
            int ret = ext2::CreateFile(mount_data->device, &mount_data->super,
                                       lookup_path, EXT2_S_IFREG | 0644,
                                       mount_data->ext2_lba);
            if (ret <= 0) {
                tty::printk(
                    "EXT2: Failed to create file '%s', error code: -%d\n",
                    lookup_path, -ret);
                return nullptr;
            }
            inode_num = ret;
        } else {
            tty::printk("EXT2: File '%s' not found, error code: -%d\n",
                        lookup_path, -ret_path);
            return nullptr;
        }
    }

    Ext2Inode inode;
    if (ext2::ReadInode(mount_data->device, &mount_data->super, inode_num,
                        &inode, mount_data->ext2_lba) != 0) {
        tty::printk("EXT2: Failed to read inode %u\n", inode_num);
        return nullptr;
    }
    // tty::printk("EXT2: Opened file '%s', inode %u\n", path, inode_num);

    Ext2FileData *file_data = new Ext2FileData();
    if (!file_data) {
        return nullptr;
    }

    file_data->inode_num = inode_num;
    file_data->inode     = inode;

    File *file = new File();
    if (!file) {
        delete file_data;
        return nullptr;
    }

    file->flags        = flags;
    file->position     = 0;
    file->mount        = mount;
    file->private_data = file_data;

    return file;
}

int Ext2Close(File *file) {
    if (!file) {
        return -1;
    }

    Ext2FileData *file_data = (Ext2FileData *)file->private_data;
    if (file_data) {
        delete file_data;
        file->private_data = nullptr;
    }

    delete file;
    return 0;
}

ssize_t Ext2Read(File *file, void *buf, std::size_t count,
                 std::uint64_t offset) {
    if (!file || !buf) {
        return -1;
    }

    Ext2FileData *file_data = (Ext2FileData *)file->private_data;
    if (!file_data) {
        return -2;
    }

    Ext2MountData *mount_data = (Ext2MountData *)file->mount->private_data;
    if (!mount_data) {
        return -3;
    }

    return ext2::ReadFile(mount_data->device, &mount_data->super,
                          &file_data->inode, buf, count, offset,
                          mount_data->ext2_lba);
}

ssize_t Ext2Write(File *file, const void *buf, std::size_t count,
                  std::uint64_t offset) {
    if (!file || !buf) {
        return -1;
    }

    Ext2FileData *file_data = (Ext2FileData *)file->private_data;
    if (!file_data) {
        return -2;
    }

    Ext2MountData *mount_data = (Ext2MountData *)file->mount->private_data;
    if (!mount_data) {
        return -3;
    }

    ssize_t ret = ext2::WriteFile(mount_data->device, &mount_data->super,
                                  &file_data->inode, buf, count, offset,
                                  mount_data->ext2_lba);

    if (ret > 0) {
        ext2::WriteInode(mount_data->device, &mount_data->super,
                         file_data->inode_num, &file_data->inode,
                         mount_data->ext2_lba);
    }

    return ret;
}

int Ext2Mkdir(MountFs *mount, const char *path) {
    if (!mount || !path) {
        return -1;
    }

    Ext2MountData *mount_data = (Ext2MountData *)mount->private_data;
    if (!mount_data) {
        return -2;
    }

    int ret = ext2::CreateFile(mount_data->device, &mount_data->super, path,
                               EXT2_S_IFDIR | 0755, mount_data->ext2_lba);
    if (ret <= 0) {
        return -3;
    }

    std::uint32_t dir_inode_num = ret;
    Ext2Inode dir_inode;
    if (ext2::ReadInode(mount_data->device, &mount_data->super, dir_inode_num,
                        &dir_inode, mount_data->ext2_lba) != 0) {
        return -4;
    }

    ext2::AddEntry(mount_data->device, &mount_data->super, &dir_inode,
                   dir_inode_num, ".", 2, mount_data->ext2_lba);
    ext2::AddEntry(mount_data->device, &mount_data->super, &dir_inode,
                   dir_inode_num, "..", 2, mount_data->ext2_lba);

    dir_inode.i_links_count = 2;
    ext2::WriteInode(mount_data->device, &mount_data->super, dir_inode_num,
                     &dir_inode, mount_data->ext2_lba);

    return 0;
}

int Ext2Rmdir(MountFs *mount, const char *path) {
    if (!mount || !path) {
        return -1;
    }

    Ext2MountData *mount_data = (Ext2MountData *)mount->private_data;
    if (!mount_data) {
        return -2;
    }

    char abs_path[256];
    const char *lookup_path = path;
    if (path[0] != '/') {
        abs_path[0] = '/';
        size_t len  = strlen(path);
        if (len >= sizeof(abs_path) - 1) {
            return -7;
        }
        memcpy(abs_path + 1, path, len);
        abs_path[len + 1] = '\0';
        lookup_path       = abs_path;
    }

    std::uint32_t inode_num;
    if (ext2::LookupPath(mount_data->device, &mount_data->super, lookup_path,
                         &inode_num, mount_data->ext2_lba) != 0) {
        return -3;
    }

    Ext2Inode inode;
    if (ext2::ReadInode(mount_data->device, &mount_data->super, inode_num,
                        &inode, mount_data->ext2_lba) != 0) {
        return -4;
    }

    if (!(inode.i_mode & EXT2_S_IFDIR)) {
        return -5;
    }

    if (inode.i_links_count > 2) {
        return -6;
    }

    return ext2::DeleteFile(mount_data->device, &mount_data->super, lookup_path,
                            mount_data->ext2_lba);
}

DirEntry *Ext2Readdir(MountFs *mount, const char *path, std::uint32_t index) {
    if (!mount || !path) {
        return nullptr;
    }

    Ext2MountData *mount_data = (Ext2MountData *)mount->private_data;
    if (!mount_data) {
        return nullptr;
    }

    char abs_path[256];
    const char *lookup_path = path;
    if (path[0] != '/') {
        abs_path[0] = '/';
        size_t len  = strlen(path);
        if (len >= sizeof(abs_path) - 1) {
            return nullptr;
        }
        memcpy(abs_path + 1, path, len);
        abs_path[len + 1] = '\0';
        lookup_path       = abs_path;
    }

    std::uint32_t inode_num;
    if (ext2::LookupPath(mount_data->device, &mount_data->super, lookup_path,
                         &inode_num, mount_data->ext2_lba) != 0) {
        return nullptr;
    }

    Ext2Inode inode;
    if (ext2::ReadInode(mount_data->device, &mount_data->super, inode_num,
                        &inode, mount_data->ext2_lba) != 0) {
        return nullptr;
    }

    if (!(inode.i_mode & EXT2_S_IFDIR)) {
        return nullptr;
    }

    return ext2::Readdir(mount_data->device, &mount_data->super, inode_num,
                         index, mount_data->ext2_lba);
}

int Ext2Stat(MountFs *mount, const char *path, void *statbuf) {
    if (!mount || !path || !statbuf) {
        return -1;
    }

    Ext2MountData *mount_data = (Ext2MountData *)mount->private_data;
    if (!mount_data) {
        return -2;
    }

    char abs_path[256];
    const char *lookup_path = path;
    if (path[0] != '/') {
        abs_path[0] = '/';
        size_t len  = strlen(path);
        if (len >= sizeof(abs_path) - 1) {
            return -5;
        }
        memcpy(abs_path + 1, path, len);
        abs_path[len + 1] = '\0';
        lookup_path       = abs_path;
    }

    std::uint32_t inode_num;
    if (ext2::LookupPath(mount_data->device, &mount_data->super, lookup_path,
                         &inode_num, mount_data->ext2_lba) != 0) {
        return -3;
    }

    Ext2Inode inode;
    if (ext2::ReadInode(mount_data->device, &mount_data->super, inode_num,
                        &inode, mount_data->ext2_lba) != 0) {
        return -4;
    }

    struct {
        std::uint32_t st_ino;
        std::uint32_t st_mode;
        std::uint32_t st_nlink;
        std::uint32_t st_uid;
        std::uint32_t st_gid;
        std::uint32_t st_rdev;
        std::uint64_t st_size;
        std::uint32_t st_blksize;
        std::uint64_t st_blocks;
        std::uint32_t st_atime;
        std::uint32_t st_mtime;
        std::uint32_t st_ctime;
    } *stat = (decltype(stat))statbuf;

    stat->st_ino     = inode_num;
    stat->st_mode    = inode.i_mode;
    stat->st_nlink   = inode.i_links_count;
    stat->st_uid     = inode.i_uid;
    stat->st_gid     = inode.i_gid;
    stat->st_rdev    = 0;
    stat->st_size    = inode.i_size;
    stat->st_blksize = mount_data->block_size;
    stat->st_blocks  = inode.i_blocks;
    stat->st_atime   = inode.i_atime;
    stat->st_mtime   = inode.i_mtime;
    stat->st_ctime   = inode.i_ctime;

    return 0;
}

void RegisterFileSystems() {
    FileSystem *ext2_fs =
        reinterpret_cast<FileSystem *>(mm::page::Alloc(sizeof(FileSystem)));
    if (ext2_fs) {
        strcpy(ext2_fs->name, "ext2");
        ext2_fs->mount   = Ext2Mount;
        ext2_fs->umount  = Ext2Umount;
        ext2_fs->open    = Ext2Open;
        ext2_fs->close   = Ext2Close;
        ext2_fs->read    = Ext2Read;
        ext2_fs->write   = Ext2Write;
        ext2_fs->mkdir   = Ext2Mkdir;
        ext2_fs->rmdir   = Ext2Rmdir;
        ext2_fs->readdir = Ext2Readdir;
        ext2_fs->stat    = Ext2Stat;
        vfs::RegisterFileSystem(ext2_fs);
    }
}

}  // namespace vfs
