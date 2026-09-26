#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "os/c_api.h"
#include "internal.h"

#define LIBC_CHILD_CAPACITY 32
#define LIBC_SHARED_IMAGE_COUNT 3

static const char* const shared_image_paths[LIBC_SHARED_IMAGE_COUNT] = {
    "/lib/x86-64-osinfo/libos-c++.so",
    "/lib/x86-64-osinfo/libc.so",
    "/lib/x86-64-osinfo/libc++.so"};

typedef struct ChildProcess {
    uint64_t handle;
    pid_t id;
} ChildProcess;

static ChildProcess children[LIBC_CHILD_CAPACITY];
static volatile uint8_t child_lock;

static void lockChildren(void) {
    while (__atomic_test_and_set(&child_lock, __ATOMIC_ACQUIRE)) os_yield();
}

static void unlockChildren(void) {
    __atomic_clear(&child_lock, __ATOMIC_RELEASE);
}

static void resetChildrenAfterFork(void) {
    int index;
    child_lock = 0;
    for (index = 0; index < LIBC_CHILD_CAPACITY; ++index) {
        if (children[index].handle != 0) {
            os_close_handle(children[index].handle);
        }
        children[index].handle = 0;
        children[index].id = 0;
    }
}

static void forgetChild(pid_t processId) {
    int index;
    lockChildren();
    for (index = 0; index < LIBC_CHILD_CAPACITY; ++index) {
        if (children[index].handle == 0 ||
            children[index].id != processId) {
            continue;
        }
        os_close_handle(children[index].handle);
        children[index].handle = 0;
        children[index].id = 0;
        break;
    }
    unlockChildren();
}

pid_t libcRegisterChild(uint64_t handle) {
    OsProcessInfo info;
    int index;
    if (handle == 0 || os_get_process_info(handle, &info) != 0) return -1;
    lockChildren();
    for (index = 0; index < LIBC_CHILD_CAPACITY; ++index) {
        if (children[index].handle == 0) {
            children[index].handle = handle;
            children[index].id = (pid_t)info.id;
            unlockChildren();
            return (pid_t)info.id;
        }
    }
    unlockChildren();
    os_close_handle(handle);
    return -1;
}

pid_t getpid(void) { return (pid_t)os_get_process_id(); }

pid_t getppid(void) {
    OsProcessIdentifiers identifiers;
    if (os_get_process_identifiers(&identifiers) != 0) return 0;
    return (pid_t)identifiers.parentId;
}

pid_t getpgrp(void) {
    OsProcessIdentifiers identifiers;
    if (os_get_process_identifiers(&identifiers) != 0) return -1;
    return (pid_t)identifiers.processGroupId;
}

pid_t getsid(pid_t processId) {
    OsProcessIdentifiers identifiers;
    if (processId != 0 && processId != getpid()) {
        errno = ENOSYS;
        return -1;
    }
    if (os_get_process_identifiers(&identifiers) != 0) {
        errno = ESRCH;
        return -1;
    }
    return (pid_t)identifiers.sessionId;
}

pid_t setsid(void) {
    int64_t result = os_create_session();
    if (result < 0) {
        errno = EPERM;
        return -1;
    }
    return (pid_t)result;
}

int kill(pid_t processId, int signal) {
    if (processId == -1) {
        errno = ENOSYS;
        return -1;
    }
    if (os_send_signal((uint64_t)(int64_t)processId,
                       (uint32_t)signal) != 0) {
        errno = ESRCH;
        return -1;
    }
    return 0;
}

pid_t fork(void) {
    int64_t result = os_fork_process();
    if (result < 0) {
        errno = result == -5 ? ENOMEM : EAGAIN;
        return -1;
    }
    if (result == 0) {
        const pid_t parent = getppid();
        resetChildrenAfterFork();
        if (libcAfterForkChild(parent) != 0) {
            os_exit_process(127);
        }
    }
    return (pid_t)result;
}

int execve(const char* path, char* const arguments[],
           char* const environment[]) {
    enum { maximum_arguments = 8, maximum_environment = 16 };
    const uint64_t maximum_image_size = 8ULL * 1024 * 1024;
    char absolute[LIBC_PATH_CAPACITY];
    OsFileStat status;
    OsExecImageInfo info;
    OsSharedImageInfo sharedImages[LIBC_SHARED_IMAGE_COUNT] = {{0}};
    void* sharedBuffers[LIBC_SHARED_IMAGE_COUNT] = {0};
    LibcExecState runtimeState;
    uint64_t descriptor;
    uint64_t offset = 0;
    void* image;
    int argumentCount = 0;
    int environmentCount = 0;
    int result;
    int sharedIndex;
    if (path == NULL || arguments == NULL) {
        errno = EFAULT;
        return -1;
    }
    while (arguments[argumentCount] != NULL) {
        if (argumentCount == maximum_arguments) {
            errno = E2BIG;
            return -1;
        }
        ++argumentCount;
    }
    if (environment != NULL) {
        while (environment[environmentCount] != NULL) {
            if (environmentCount == maximum_environment) {
                errno = E2BIG;
                return -1;
            }
            ++environmentCount;
        }
    }
    libcCopyPath(absolute, path);
    if (absolute[0] == '\0' ||
        !os_stat_file(libcFsService(), absolute, &status) ||
        status.type != OS_FILE_TYPE_FILE) {
        errno = ENOENT;
        return -1;
    }
    if (status.size == 0 || status.size > maximum_image_size) {
        errno = ENOEXEC;
        return -1;
    }
    image = malloc((size_t)status.size);
    if (image == NULL) {
        errno = ENOMEM;
        return -1;
    }
    descriptor = os_open_file(libcFsService(), absolute);
    if (descriptor == 0) {
        free(image);
        errno = EIO;
        return -1;
    }
    while (offset < status.size) {
        const int64_t count = os_read_file(
            libcFsService(), descriptor, (uint8_t*)image + offset,
            (size_t)(status.size - offset));
        if (count <= 0) break;
        offset += (uint64_t)count;
    }
    os_close_file(libcFsService(), descriptor);
    if (offset != status.size) {
        free(image);
        errno = EIO;
        return -1;
    }
    for (sharedIndex = 0; sharedIndex < LIBC_SHARED_IMAGE_COUNT;
         ++sharedIndex) {
        OsFileStat sharedStatus;
        uint64_t sharedDescriptor;
        uint64_t sharedOffset = 0;
        if (!os_stat_file(libcFsService(), shared_image_paths[sharedIndex],
                          &sharedStatus) ||
            sharedStatus.type != OS_FILE_TYPE_FILE ||
            sharedStatus.size == 0 ||
            sharedStatus.size > maximum_image_size) {
            break;
        }
        sharedBuffers[sharedIndex] = malloc((size_t)sharedStatus.size);
        sharedDescriptor = os_open_file(
            libcFsService(), shared_image_paths[sharedIndex]);
        if (sharedBuffers[sharedIndex] == NULL || sharedDescriptor == 0) {
            if (sharedDescriptor != 0) {
                os_close_file(libcFsService(), sharedDescriptor);
            }
            break;
        }
        while (sharedOffset < sharedStatus.size) {
            const int64_t count = os_read_file(
                libcFsService(), sharedDescriptor,
                (uint8_t*)sharedBuffers[sharedIndex] + sharedOffset,
                (size_t)(sharedStatus.size - sharedOffset));
            if (count <= 0) break;
            sharedOffset += (uint64_t)count;
        }
        os_close_file(libcFsService(), sharedDescriptor);
        if (sharedOffset != sharedStatus.size) break;
        sharedImages[sharedIndex].image = sharedBuffers[sharedIndex];
        sharedImages[sharedIndex].imageSize = sharedStatus.size;
        sharedImages[sharedIndex].name = shared_image_paths[sharedIndex];
    }
    if (sharedIndex != LIBC_SHARED_IMAGE_COUNT) {
        int cleanup;
        for (cleanup = 0; cleanup < LIBC_SHARED_IMAGE_COUNT; ++cleanup) {
            free(sharedBuffers[cleanup]);
        }
        free(image);
        errno = ENOENT;
        return -1;
    }
    libcExportExecState(&runtimeState);
    info.image = image;
    info.imageSize = status.size;
    info.name = absolute;
    info.argumentCount = (uint32_t)argumentCount;
    info.environmentCount = (uint32_t)environmentCount;
    info.arguments = (const char* const*)arguments;
    info.environment = (const char* const*)environment;
    info.runtimeState = &runtimeState;
    info.runtimeStateSize = sizeof(runtimeState);
    info.sharedImageCount = LIBC_SHARED_IMAGE_COUNT;
    info.sharedReserved = 0;
    info.sharedImages = sharedImages;
    result = os_replace_process_image(&info);
    for (sharedIndex = 0; sharedIndex < LIBC_SHARED_IMAGE_COUNT;
         ++sharedIndex) {
        free(sharedBuffers[sharedIndex]);
    }
    free(image);
    errno = result == -5 ? ENOMEM :
            (result == -7 ? ENOEXEC : EINVAL);
    return -1;
}

pid_t wait(int* status) { return waitpid(-1, status, 0); }

pid_t waitpid(pid_t processId, int* status, int options) {
    if ((options & ~(WNOHANG | WUNTRACED | WCONTINUED)) != 0) {
        errno = EINVAL;
        return -1;
    }
    {
        int exitCode = 0;
        const int64_t result = os_wait_child(
            processId, &exitCode, options);
        if (result < 0) {
            errno = ECHILD;
            return -1;
        }
        if (result == 0) return 0;
        forgetChild((pid_t)result);
        if (status != NULL) *status = exitCode;
        return (pid_t)result;
    }
}

int isatty(int fileDescriptor) {
    OpenDescription* description = libcDescriptor(fileDescriptor);
    if (description != NULL &&
        (description->kind == DESCRIPTOR_TERMINAL_READ ||
         description->kind == DESCRIPTOR_TERMINAL_WRITE)) {
        return 1;
    }
    errno = ENOTTY;
    return 0;
}
