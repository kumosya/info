#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <unistd.h>

int open(const char* path, int flags, ...) {
    char absolute[LIBC_PATH_CAPACITY];
    OsFileStat status;
    uint64_t service;
    uint64_t handle;
    int exists;
    int result;
    mode_t createMode = 0666;
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        createMode = va_arg(arguments, mode_t);
        va_end(arguments);
    }
    libcCopyPath(absolute, path);
    service = libcFsService();
    if (service == 0 || absolute[0] == '\0') {
        errno = ENOENT;
        return -1;
    }
    exists = os_stat_file(service, absolute, &status);
    if (!exists && (flags & O_CREAT) != 0) {
        if (!os_create_file(service, absolute)) {
            errno = EIO;
            return -1;
        }
        if (!os_change_file_mode(service, absolute, createMode)) {
            errno = EIO;
            return -1;
        }
        exists = os_stat_file(service, absolute, &status);
    }
    if (!exists || status.type == OS_FILE_TYPE_DIRECTORY) {
        errno = exists ? EISDIR : ENOENT;
        return -1;
    }
    if ((flags & O_TRUNC) != 0 && (flags & (O_WRONLY | O_RDWR)) != 0 &&
        !os_truncate_file(service, absolute)) {
        errno = EIO;
        return -1;
    }
    handle = os_open_file(service, absolute);
    if (handle == 0) {
        errno = EIO;
        return -1;
    }
    result = libcAllocateDescriptor(handle, absolute, flags, DESCRIPTOR_FILE);
    if (result < 0) {
        os_close_file(service, handle);
        return -1;
    }
    if ((flags & O_APPEND) != 0 && os_stat_file(service, absolute, &status)) {
        os_seek_file(service, handle, status.size);
        libcDescriptor(result)->offset = status.size;
    }
    return result;
}

int creat(const char* path, mode_t mode) {
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

ssize_t read(int fileDescriptor, void* buffer, size_t count) {
    OpenDescription* value = libcDescriptor(fileDescriptor);
    int64_t result;
    if (value == NULL || buffer == NULL) {
        errno = value == NULL ? EBADF : EFAULT;
        return -1;
    }
    if (value->kind == DESCRIPTOR_PIPE_WRITE ||
        value->kind == DESCRIPTOR_TERMINAL_WRITE) {
        errno = EBADF;
        return -1;
    }
    if (value->kind == DESCRIPTOR_FILE &&
        (value->flags & O_ACCMODE) == O_WRONLY) {
        errno = EBADF;
        return -1;
    }
    if (value->kind == DESCRIPTOR_FILE) libcLockFileIo();
    if (value->kind == DESCRIPTOR_PIPE_READ) {
        result = os_read_pipe(value->handle, buffer, count);
    } else if (value->kind == DESCRIPTOR_TERMINAL_READ) {
        result = os_read_terminal(buffer, count);
    } else {
        result = os_read_file(libcFsService(), value->handle, buffer, count);
    }
    if (result >= 0) value->offset += (uint64_t)result;
    else errno = EIO;
    if (value->kind == DESCRIPTOR_FILE) libcUnlockFileIo();
    return result;
}

ssize_t write(int fileDescriptor, const void* buffer, size_t count) {
    OpenDescription* value = libcDescriptor(fileDescriptor);
    int64_t result;
    if (value == NULL || buffer == NULL) {
        errno = value == NULL ? EBADF : EFAULT;
        return -1;
    }
    if (value->kind == DESCRIPTOR_PIPE_READ ||
        value->kind == DESCRIPTOR_TERMINAL_READ) {
        errno = EBADF;
        return -1;
    }
    if (value->kind == DESCRIPTOR_FILE &&
        (value->flags & O_ACCMODE) == O_RDONLY) {
        errno = EBADF;
        return -1;
    }
    if (value->kind == DESCRIPTOR_FILE) {
        OsFileStat status;
        libcLockFileIo();
        if ((value->flags & O_APPEND) != 0 &&
            (!os_stat_file(libcFsService(), value->path, &status) ||
             !os_seek_file(libcFsService(), value->handle, status.size))) {
            libcUnlockFileIo();
            errno = EIO;
            return -1;
        }
    }
    if (value->kind == DESCRIPTOR_PIPE_WRITE) {
        result = os_write_pipe(value->handle, buffer, count);
    } else if (value->kind == DESCRIPTOR_TERMINAL_WRITE) {
        result = os_write_terminal(buffer, count);
    } else {
        result = os_write_file(libcFsService(), value->handle, buffer, count);
    }
    if (result >= 0) value->offset += (uint64_t)result;
    else errno = EIO;
    if (value->kind == DESCRIPTOR_FILE) libcUnlockFileIo();
    return result;
}

int close(int fileDescriptor) {
    if (libcDescriptor(fileDescriptor) != NULL) {
        if (libcReleaseDescriptor(fileDescriptor)) return 0;
        errno = EIO;
        return -1;
    }
    errno = EBADF;
    return -1;
}

int dup(int fileDescriptor) {
    return libcDuplicateDescriptor(fileDescriptor, 0, 0);
}

int dup2(int fileDescriptor, int newFileDescriptor) {
    OpenDescription* description = libcDescriptor(fileDescriptor);
    if (description == NULL || newFileDescriptor < 0 ||
        newFileDescriptor >= LIBC_DESCRIPTOR_CAPACITY) {
        errno = EBADF;
        return -1;
    }
    if (fileDescriptor == newFileDescriptor) return newFileDescriptor;
    if (libcDescriptor(newFileDescriptor) != NULL &&
        !libcReleaseDescriptor(newFileDescriptor)) {
        errno = EIO;
        return -1;
    }
    libcDescriptors[newFileDescriptor].description = description;
    libcDescriptors[newFileDescriptor].closeOnExec = 0;
    ++description->referenceCount;
    return newFileDescriptor;
}

int dup3(int fileDescriptor, int newFileDescriptor, int flags) {
    int result;
    if (fileDescriptor == newFileDescriptor || (flags & ~O_CLOEXEC) != 0) {
        errno = EINVAL;
        return -1;
    }
    result = dup2(fileDescriptor, newFileDescriptor);
    if (result >= 0) {
        libcDescriptors[result].closeOnExec = (flags & O_CLOEXEC) != 0;
    }
    return result;
}

int fcntl(int fileDescriptor, int command, ...) {
    FileDescriptor* slot;
    va_list arguments;
    int result = 0;
    if (libcDescriptor(fileDescriptor) == NULL) {
        errno = EBADF;
        return -1;
    }
    slot = libcDescriptorSlot(fileDescriptor);
    va_start(arguments, command);
    if (command == F_DUPFD || command == F_DUPFD_CLOEXEC) {
        result = libcDuplicateDescriptor(fileDescriptor, va_arg(arguments, int),
                                         command == F_DUPFD_CLOEXEC);
    } else if (command == F_GETFD) {
        result = slot->closeOnExec ? FD_CLOEXEC : 0;
    } else if (command == F_SETFD) {
        slot->closeOnExec = (va_arg(arguments, int) & FD_CLOEXEC) != 0;
    } else if (command == F_GETFL) {
        result = slot->description->flags;
    } else if (command == F_SETFL) {
        int flags = va_arg(arguments, int);
        if ((flags & O_NONBLOCK) != 0) {
            errno = ENOSYS;
            result = -1;
        } else {
            slot->description->flags =
                (slot->description->flags & ~O_APPEND) | (flags & O_APPEND);
        }
    } else {
        errno = EINVAL;
        result = -1;
    }
    va_end(arguments);
    return result;
}

int pipe(int fileDescriptors[2]) { return pipe2(fileDescriptors, 0); }

int pipe2(int fileDescriptors[2], int flags) {
    OsPipeHandles handles;
    int readDescriptor;
    int writeDescriptor;
    if (fileDescriptors == NULL || (flags & ~O_CLOEXEC) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!os_create_pipe(&handles)) {
        errno = ENOMEM;
        return -1;
    }
    readDescriptor = libcAllocateDescriptor(handles.readHandle, "pipe",
                                             O_RDONLY, DESCRIPTOR_PIPE_READ);
    writeDescriptor = libcAllocateDescriptor(handles.writeHandle, "pipe",
                                              O_WRONLY, DESCRIPTOR_PIPE_WRITE);
    if (readDescriptor < 0 || writeDescriptor < 0) {
        if (readDescriptor >= 0) close(readDescriptor);
        else os_close_handle(handles.readHandle);
        if (writeDescriptor >= 0) close(writeDescriptor);
        else os_close_handle(handles.writeHandle);
        return -1;
    }
    libcDescriptors[readDescriptor].closeOnExec = (flags & O_CLOEXEC) != 0;
    libcDescriptors[writeDescriptor].closeOnExec = (flags & O_CLOEXEC) != 0;
    fileDescriptors[0] = readDescriptor;
    fileDescriptors[1] = writeDescriptor;
    return 0;
}

off_t lseek(int fileDescriptor, off_t offset, int whence) {
    OpenDescription* value = libcDescriptor(fileDescriptor);
    int64_t target;
    if (value == NULL) {
        errno = EBADF;
        return -1;
    }
    if (value->kind != DESCRIPTOR_FILE) {
        errno = ESPIPE;
        return -1;
    }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }
    libcLockFileIo();
    target = os_seek_file_position(libcFsService(), value->handle,
                                   (int64_t)offset, whence);
    if (target < 0) {
        libcUnlockFileIo();
        errno = EINVAL;
        return -1;
    }
    value->offset = (uint64_t)target;
    libcUnlockFileIo();
    return target;
}

ssize_t pread(int fd, void* buffer, size_t count, off_t offset) {
    OpenDescription* value = libcDescriptor(fd);
    uint64_t saved;
    ssize_t result;
    if (value == NULL || value->kind != DESCRIPTOR_FILE || offset < 0) {
        errno = value == NULL ? EBADF : ESPIPE;
        return -1;
    }
    libcLockFileIo();
    saved = value->offset;
    if (!os_seek_file(libcFsService(), value->handle, (uint64_t)offset)) {
        libcUnlockFileIo();
        errno = EINVAL;
        return -1;
    }
    result = os_read_file(libcFsService(), value->handle, buffer, count);
    os_seek_file(libcFsService(), value->handle, saved);
    libcUnlockFileIo();
    if (result < 0) errno = EIO;
    return result;
}

ssize_t pwrite(int fd, const void* buffer, size_t count, off_t offset) {
    OpenDescription* value = libcDescriptor(fd);
    uint64_t saved;
    ssize_t result;
    if (value == NULL || value->kind != DESCRIPTOR_FILE || offset < 0) {
        errno = value == NULL ? EBADF : ESPIPE;
        return -1;
    }
    libcLockFileIo();
    saved = value->offset;
    if (!os_seek_file(libcFsService(), value->handle, (uint64_t)offset)) {
        libcUnlockFileIo();
        errno = EINVAL;
        return -1;
    }
    result = os_write_file(libcFsService(), value->handle, buffer, count);
    os_seek_file(libcFsService(), value->handle, saved);
    libcUnlockFileIo();
    if (result < 0) errno = EIO;
    return result;
}

ssize_t readv(int fd, const struct iovec* vectors, int vectorCount) {
    ssize_t total = 0;
    int index;
    if (vectors == NULL || vectorCount <= 0) {
        errno = EINVAL;
        return -1;
    }
    for (index = 0; index < vectorCount; ++index) {
        ssize_t count = read(fd, vectors[index].iov_base, vectors[index].iov_len);
        if (count < 0) return total == 0 ? -1 : total;
        total += count;
        if ((size_t)count != vectors[index].iov_len) break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec* vectors, int vectorCount) {
    ssize_t total = 0;
    int index;
    if (vectors == NULL || vectorCount <= 0) {
        errno = EINVAL;
        return -1;
    }
    for (index = 0; index < vectorCount; ++index) {
        ssize_t count = write(fd, vectors[index].iov_base, vectors[index].iov_len);
        if (count < 0) return total == 0 ? -1 : total;
        total += count;
        if ((size_t)count != vectors[index].iov_len) break;
    }
    return total;
}
