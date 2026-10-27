#include "internal.h"
#include <errno.h>

int rename(const char* existing, const char* newPath) {
    char source[LIBC_PATH_CAPACITY], destination[LIBC_PATH_CAPACITY];
    OsFileStat first, second;
    libcCopyPath(source, existing); libcCopyPath(destination, newPath);
    if (!__libc_os_stat_file(source, &first)) { errno = ENOENT; return -1; }
    // Compare the destination parent, which exists even when its leaf does not.
    char parent[LIBC_PATH_CAPACITY];
    size_t length = 0;
    while (destination[length]) ++length;
    while (length > 1 && destination[length - 1] == '/') --length;
    while (length > 1 && destination[length - 1] != '/') --length;
    for (size_t i = 0; i < length; ++i) parent[i] = destination[i];
    parent[length > 1 ? length - 1 : 1] = 0;
    if (!__libc_os_stat_file(parent, &second)) { errno = ENOENT; return -1; }
    if (first.device != second.device) { errno = EXDEV; return -1; }
    if (!__libc_os_rename_file(libcFsService(), source, destination)) { errno = EIO; return -1; }
    return 0;
}

#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static mode_t creation_mask=0022;
mode_t umask(mode_t mode) { return __atomic_exchange_n(&creation_mask,mode&0777,__ATOMIC_RELAXED); }
mode_t libcCreationMode(mode_t mode) { return mode & ~__atomic_load_n(&creation_mask,__ATOMIC_RELAXED); }

int stat(const char* path, struct stat* result) {
    char absolute[LIBC_PATH_CAPACITY];
    OsFileStat status;
    if (path == NULL || result == NULL) {
        errno = EFAULT;
        return -1;
    }
    libcCopyPath(absolute, path);
    if (!__libc_os_stat_file(absolute, &status)) {
        errno = ENOENT;
        return -1;
    }
    result->st_mode = status.mode;
    if ((result->st_mode & S_IFMT) == 0) {
        result->st_mode |= status.type == OS_FILE_TYPE_DIRECTORY ? S_IFDIR : S_IFREG;
    }
    result->st_size = (off_t)status.size;
    result->st_dev = status.device;
    result->st_ino = status.inode;
    result->st_nlink = status.links;
    result->st_uid = status.uid;
    result->st_gid = status.gid;
    result->st_rdev = status.specialDevice;
    result->st_blksize = 4096;
    result->st_blocks = (blkcnt_t)((status.size + 511) / 512);
    result->st_atim.tv_sec = (time_t)status.accessTime;
    result->st_atim.tv_nsec = 0;
    result->st_mtim.tv_sec = (time_t)status.modificationTime;
    result->st_mtim.tv_nsec = 0;
    result->st_ctim.tv_sec = (time_t)status.changeTime;
    result->st_ctim.tv_nsec = 0;
    return 0;
}

int fstat(int fileDescriptor, struct stat* result) {
    OpenDescription* value = libcDescriptor(fileDescriptor);
    if (value == NULL) {
        errno = EBADF;
        return -1;
    }
    int status = __libc_file_stat(value->fileObject, result);
    if (status < 0) { errno = -status; return -1; }
    return 0;
}

int mkdir(const char* path, mode_t mode) {
    char absolute[LIBC_PATH_CAPACITY];
    OsFileStat existing;
    libcCopyPath(absolute, path);
    if (__libc_os_stat_file(absolute, &existing)) {
        errno = EEXIST;
        return -1;
    }
    if (!__libc_os_make_directory(libcFsService(), absolute)) {
        errno = __libc_os_stat_file(absolute, &existing) ? EEXIST : EIO;
        return -1;
    }
    if (!__libc_os_change_file_mode(libcFsService(), absolute, libcCreationMode(mode))) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int unlink(const char* path) {
    char absolute[LIBC_PATH_CAPACITY];
    libcCopyPath(absolute, path);
    struct stat value;
    if (stat(absolute,&value)) return -1;
    if (S_ISDIR(value.st_mode)) { errno=EISDIR; return -1; }
    if (!__libc_os_remove_path(libcFsService(), absolute)) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int rmdir(const char* path) {
    char absolute[LIBC_PATH_CAPACITY]; libcCopyPath(absolute,path);
    struct stat value; if(stat(absolute,&value)) return -1;
    if(!S_ISDIR(value.st_mode)) { errno=ENOTDIR; return -1; }
    if(!__libc_os_remove_path(libcFsService(),absolute)) { errno=ENOTEMPTY; return -1; }
    return 0;
}

int link(const char* existing, const char* newPath) {
    char oldAbsolute[LIBC_PATH_CAPACITY];
    char newAbsolute[LIBC_PATH_CAPACITY];
    if (existing == NULL || newPath == NULL) {
        errno = EFAULT;
        return -1;
    }
    libcCopyPath(oldAbsolute, existing);
    libcCopyPath(newAbsolute, newPath);
    if (!__libc_os_link_file(libcFsService(), oldAbsolute, newAbsolute)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int linkat(int oldDirectory, const char* existing, int newDirectory,
           const char* newPath, int flags) {
    if (oldDirectory != AT_FDCWD || newDirectory != AT_FDCWD || flags != 0) {
        errno = ENOTSUP;
        return -1;
    }
    return link(existing, newPath);
}

int symlink(const char* target, const char* linkPath) {
    (void)target;
    (void)linkPath;
    errno = ENOSYS;
    return -1;
}

ssize_t readlink(const char* path, char* buffer, size_t size) {
    (void)path;
    (void)buffer;
    (void)size;
    errno = ENOSYS;
    return -1;
}

int chdir(const char* path) {
    char absolute[LIBC_PATH_CAPACITY];
    struct stat status;
    if (libcPathAt(AT_FDCWD, path, absolute)) return -1;
    if (stat(absolute, &status) != 0) return -1;
    if (!S_ISDIR(status.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    OsCredentialInfo credentials;
    int error = __libc_os_credentials(0, 0, 0, 0, NULL, &credentials);
    if (error) { errno = error; return -1; }
    int member = credentials.effectiveGid == status.st_gid;
    for (uint32_t i = 0; i < credentials.groupCount; ++i) if (credentials.groups[i] == status.st_gid) member = 1;
    unsigned shift = credentials.effectiveUid == status.st_uid ? 6 : member ? 3 : 0;
    if (credentials.effectiveUid && !((status.st_mode >> shift) & 1)) { errno = EACCES; return -1; }
    libcSetCurrentDirectory(absolute);
    return 0;
}

char* getcwd(char* buffer, size_t size) {
    const char* current = libcGetCurrentDirectory();
    size_t required = strlen(current) + 1;
    if (buffer == NULL || size == 0) {
        errno = EINVAL;
        return NULL;
    }
    if (required > size) {
        errno = ERANGE;
        return NULL;
    }
    memcpy(buffer, current, required);
    return buffer;
}

int access(const char* path, int mode) {
    struct stat status;
    if ((mode & ~(R_OK | W_OK | X_OK)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (stat(path, &status) != 0) return -1;
    if ((mode & R_OK) != 0 && (status.st_mode & 0444) == 0) goto denied;
    if ((mode & W_OK) != 0 && (status.st_mode & 0222) == 0) goto denied;
    if ((mode & X_OK) != 0 && (status.st_mode & 0111) == 0) goto denied;
    return 0;
denied:
    errno = EACCES;
    return -1;
}

int truncate(const char* path, off_t length) {
    char absolute[LIBC_PATH_CAPACITY];
    if (length != 0) {
        errno = ENOSYS;
        return -1;
    }
    libcCopyPath(absolute, path);
    if (!__libc_os_truncate_file(libcFsService(), absolute)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int ftruncate(int fileDescriptor, off_t length) {
    OpenDescription* value = libcDescriptor(fileDescriptor);
    if (value == NULL || length < 0) {
        errno = value == NULL ? EBADF : EINVAL;
        return -1;
    }
    if (value->kind == DESCRIPTOR_SHARED_MEMORY) {
        if ((value->flags & O_ACCMODE) != O_RDWR) {
            errno = EACCES;
            return -1;
        }
        if (__libc_os_resize_shared_memory(value->handle, (uint64_t)length) != 0) {
            errno = EINVAL;
            return -1;
        }
        value->offset = (uint64_t)length;
        return 0;
    }
    if (value->kind != DESCRIPTOR_FILE) {
        errno = EINVAL;
        return -1;
    }
    return truncate(value->path, length);
}

int fsync(int fileDescriptor) {
    OpenDescription* description = libcDescriptor(fileDescriptor);
    if (description == NULL) {
        errno = EBADF;
        return -1;
    }
    const int result = __libc_file_fsync(description->fileObject);
    if (result < 0) { errno = -result; return -1; }
    return 0;
}

int fdatasync(int fileDescriptor) { return fsync(fileDescriptor); }

int chmod(const char* path, mode_t mode) {
    char absolute[LIBC_PATH_CAPACITY];
    if (path == NULL || (mode & ~07777U) != 0) {
        errno = EINVAL;
        return -1;
    }
    libcCopyPath(absolute, path);
    if (!__libc_os_change_file_mode(libcFsService(), absolute, mode)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int fchmod(int fileDescriptor, mode_t mode) {
    OpenDescription* value = libcDescriptor(fileDescriptor);
    if (value == NULL) {
        errno = EBADF;
        return -1;
    }
    return chmod(value->path, mode);
}

int chown(const char* path, uid_t owner, gid_t group) {
    char absolute[LIBC_PATH_CAPACITY];
    OsFileStat status;
    uint32_t uid;
    uint32_t gid;
    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }
    libcCopyPath(absolute, path);
    if (!__libc_os_stat_file(absolute, &status)) {
        errno = ENOENT;
        return -1;
    }
    uid = owner == (uid_t)-1 ? status.uid : owner;
    gid = group == (gid_t)-1 ? status.gid : group;
    if (uid > 0xffffU || gid > 0xffffU ||
        !__libc_os_change_file_owner(libcFsService(), absolute, uid, gid)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int fchown(int fileDescriptor, uid_t owner, gid_t group) {
    OpenDescription* value = libcDescriptor(fileDescriptor);
    if (value == NULL) {
        errno = EBADF;
        return -1;
    }
    return chown(value->path, owner, group);
}

int lchown(const char* path, uid_t owner, gid_t group) {
    return chown(path, owner, group);
}

int fstatat(int directory, const char* path, struct stat* status, int flags) {
    if (flags & ~AT_SYMLINK_NOFOLLOW) {
        errno = ENOTSUP;
        return -1;
    }
    char absolute[LIBC_PATH_CAPACITY];
    if(libcPathAt(directory,path,absolute)) return -1;
    return stat(absolute, status);
}

int utimensat(int directory, const char* path,
              const struct timespec times[2], int flags) {
    char absolute[LIBC_PATH_CAPACITY];
    OsFileStat status;
    struct timespec now;
    uint64_t accessTime;
    uint64_t modificationTime;
    if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0 || path == NULL) {
        errno = flags != 0 ? ENOTSUP : EINVAL;
        return -1;
    }
    if(libcPathAt(directory,path,absolute)) return -1;
    if (!__libc_os_stat_file(absolute, &status)) {
        errno = ENOENT;
        return -1;
    }
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return -1;
    accessTime = times == NULL || times[0].tv_nsec == UTIME_NOW
                     ? (uint64_t)now.tv_sec
                     : times[0].tv_nsec == UTIME_OMIT
                           ? status.accessTime
                           : (uint64_t)times[0].tv_sec;
    modificationTime = times == NULL || times[1].tv_nsec == UTIME_NOW
                           ? (uint64_t)now.tv_sec
                           : times[1].tv_nsec == UTIME_OMIT
                                 ? status.modificationTime
                                 : (uint64_t)times[1].tv_sec;
    if ((times != NULL &&
         ((times[0].tv_nsec < 0 && times[0].tv_nsec != UTIME_OMIT) ||
          (times[1].tv_nsec < 0 && times[1].tv_nsec != UTIME_OMIT))) ||
        accessTime > UINT32_MAX || modificationTime > UINT32_MAX ||
        !__libc_os_change_file_times(libcFsService(), absolute, accessTime,
                              modificationTime)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

long sysconf(int name) {
    switch (name) {
        case _SC_VERSION: return _POSIX_VERSION;
        case _SC_2_VERSION: return _POSIX2_VERSION;
        case _SC_OPEN_MAX: return OPEN_MAX;
        case _SC_NGROUPS_MAX: return NGROUPS_MAX;
        case _SC_PAGESIZE: return 4096;
        case _SC_CLK_TCK: return 100;
        case _SC_NPROCESSORS_CONF:
        case _SC_NPROCESSORS_ONLN: errno = ENOSYS; return -1;
        default: errno = EINVAL; return -1;
    }
}

long pathconf(const char* path, int name) {
    struct stat status;
    if (path == NULL || stat(path, &status) != 0) return -1;
    switch (name) {
        case _PC_LINK_MAX: return 1;
        case _PC_MAX_CANON: return 255;
        case _PC_MAX_INPUT: return 255;
        case _PC_NAME_MAX: return NAME_MAX;
        case _PC_PATH_MAX: return PATH_MAX;
        case _PC_PIPE_BUF: return PIPE_BUF;
        case _PC_CHOWN_RESTRICTED: return 1;
        case _PC_NO_TRUNC: return 1;
        case _PC_VDISABLE: return 0;
        default: errno = EINVAL; return -1;
    }
}

size_t confstr(int name, char* buffer, size_t length) {
    static const char path[] = "/bin:/usr/bin";
    size_t required;
    size_t copied;
    if (name != _CS_PATH) {
        errno = EINVAL;
        return 0;
    }
    required = sizeof(path);
    if (buffer != NULL && length != 0) {
        copied = length < required ? length : required;
        memcpy(buffer, path, copied);
        buffer[copied - 1] = '\0';
    }
    return required;
}
