#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "internal.h"
#include "internal.h"

#define LIBC_CHILD_CAPACITY 32
#define LIBC_SHARED_IMAGE_COUNT 1

typedef struct ChildTask {
    uint64_t handle;
    pid_t id;
} ChildTask;

static ChildTask children[LIBC_CHILD_CAPACITY];
static volatile uint8_t child_lock;

static void lockChildren(void) {
    while (__atomic_test_and_set(&child_lock, __ATOMIC_ACQUIRE)) __libc_os_yield();
}

static void unlockChildren(void) {
    __atomic_clear(&child_lock, __ATOMIC_RELEASE);
}

static void resetChildrenAfterFork(void) {
    int index;
    child_lock = 0;
    for (index = 0; index < LIBC_CHILD_CAPACITY; ++index) {
        if (children[index].handle != 0) {
            __libc_os_close_handle(children[index].handle);
        }
        children[index].handle = 0;
        children[index].id = 0;
    }
}

static void forgetChild(pid_t taskId) {
    int index;
    lockChildren();
    for (index = 0; index < LIBC_CHILD_CAPACITY; ++index) {
        if (children[index].handle == 0 ||
            children[index].id != taskId) {
            continue;
        }
        __libc_os_close_handle(children[index].handle);
        children[index].handle = 0;
        children[index].id = 0;
        break;
    }
    unlockChildren();
}

pid_t libcRegisterChild(uint64_t handle) {
    OsTaskInfo info;
    int index;
    if (handle == 0 || __libc_os_get_task_info(handle, &info) != 0) return -1;
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
    __libc_os_close_handle(handle);
    return -1;
}

pid_t getpid(void) { return (pid_t)__libc_os_get_task_id(); }

pid_t getppid(void) {
    OsTaskIdentifiers ids;
    return __libc_os_get_task_identifiers(&ids, 0) == 0 ? (pid_t)ids.parentId : 0;
}

pid_t getpgrp(void) {
    OsTaskIdentifiers ids;
    return __libc_os_get_task_identifiers(&ids, 0) == 0 ? (pid_t)ids.taskGroupId : 0;
}

pid_t getsid(pid_t taskId) {
    OsTaskIdentifiers ids;
    const long result = __libc_os_get_task_identifiers(&ids, taskId);
    if (result < 0) { errno = result == -4 ? ESRCH : EINVAL; return -1; }
    return (pid_t)ids.sessionId;
}

pid_t setsid(void) {
    int64_t result = __libc_os_create_session();
    if (result < 0) {
        errno = EPERM;
        return -1;
    }
    return (pid_t)result;
}

int kill(pid_t taskId, int signal) {
    if (taskId == -1) {
        errno = ENOSYS;
        return -1;
    }
    const long result = __libc_os_send_signal(taskId, signal);
    if (result < 0) {
        errno = result == -4 ? ESRCH : result == -3 ? EPERM : EINVAL;
        return -1;
    }
    return 0;
}

extern int64_t __libc_fork(void);
int __libc_fork_child_runtime(void) {
    const pid_t parent = getppid();
    resetChildrenAfterFork();
    return libcAfterForkChild(parent);
}
pid_t fork(void) {
    int64_t result = __libc_fork();
    if (result < 0) {
        errno = result == -5 ? ENOMEM : EAGAIN;
        return -1;
    }
    return (pid_t)result;
}

static int execString(const char* text) {
    if (!text) { errno = EFAULT; return -1; }
    size_t checked = 0;
    while (checked < LIBC_PATH_CAPACITY) {
        size_t count = 4096 - ((uintptr_t)(text + checked) & 4095);
        if (count > LIBC_PATH_CAPACITY - checked) count = LIBC_PATH_CAPACITY - checked;
        if (!__libc_os_accessible(text + checked, count, 0)) { errno = EFAULT; return -1; }
        if (memchr(text + checked, 0, count)) return 0;
        checked += count;
    }
    errno = E2BIG; return -1;
}
static int executePath(const char* path, char* const arguments[], char* const environment[], unsigned depth);
static int executeScript(int source, const char* path, int descriptorExecution, int closeOnExec,
                         const char* image, size_t size, char* const arguments[], char* const environment[], unsigned depth) {
    if (descriptorExecution && closeOnExec) { errno = ENOENT; return -1; }
    if (depth == 4) { errno = ELOOP; return -1; }
    size_t end = 2, limit = size < 256 ? size : 256;
    while (end < limit && image[end] && image[end] != '\n') ++end;
    if (end == 256) { errno = ENOEXEC; return -1; }
    while (end > 2 && (image[end - 1] == ' ' || image[end - 1] == '\t')) --end;
    size_t start = 2; while (start < end && (image[start] == ' ' || image[start] == '\t')) ++start;
    size_t split = start; while (split < end && image[split] != ' ' && image[split] != '\t') ++split;
    if (split == start) { errno = ENOEXEC; return -1; }
    char interpreter[256], option[256], objectPath[96];
    memcpy(interpreter, image + start, split - start); interpreter[split - start] = 0;
    while (split < end && (image[split] == ' ' || image[split] == '\t')) ++split;
    memcpy(option, image + split, end - split); option[end - split] = 0;
    if (descriptorExecution) {
        OpenDescription* description = libcDescriptor(source);
        if (!description || description->kind != DESCRIPTOR_FILE) { errno = EBADF; return -1; }
        snprintf(objectPath, sizeof(objectPath), "/proc/self/object/%llu", (unsigned long long)description->handle);
        path = objectPath;
    }
    char* scriptArguments[65]; unsigned count = 0;
    scriptArguments[count++] = interpreter;
    if (*option) scriptArguments[count++] = option;
    scriptArguments[count++] = (char*)path;
    for (unsigned i = 1; arguments[i]; ++i) {
        if (count == 64) { errno = E2BIG; return -1; }
        scriptArguments[count++] = arguments[i];
    }
    scriptArguments[count] = NULL;
    return executePath(interpreter, scriptArguments, environment, depth + 1);
}
static int executeDescriptor(int source, const char* path, int descriptorExecution, int closeOnExec,
                             char* const arguments[], char* const environment[], unsigned depth) {
    enum { maximum_arguments = 64, maximum_environment = 64 };
    const uint64_t maximum_image_size = 8ULL * 1024 * 1024;
    char absolute[LIBC_PATH_CAPACITY];
    struct stat status;
    OsExecImageInfo info;
    OsSharedImageInfo sharedImages[LIBC_SHARED_IMAGE_COUNT] = {{0}};
    void* sharedBuffers[LIBC_SHARED_IMAGE_COUNT] = {0};
    LibcExecState* runtimeState;
    uint64_t offset = 0;
    void* image;
    int argumentCount = 0;
    int environmentCount = 0;
    int result;
    int sharedIndex;
    char interpreter[256];
    int interpreterCount;
    if (path == NULL || arguments == NULL) {
        errno = EFAULT;
        return -1;
    }
    for (;;) {
        if (!__libc_os_accessible(arguments + argumentCount, sizeof(*arguments), 0)) { errno = EFAULT; return -1; }
        if (!arguments[argumentCount]) break;
        if (argumentCount == maximum_arguments) { errno = E2BIG; return -1; }
        if (execString(arguments[argumentCount])) return -1;
        ++argumentCount;
    }
    if (environment != NULL) {
        for (;;) {
            if (!__libc_os_accessible(environment + environmentCount, sizeof(*environment), 0)) { errno = EFAULT; return -1; }
            if (!environment[environmentCount]) break;
            if (environmentCount == maximum_environment) { errno = E2BIG; return -1; }
            if (execString(environment[environmentCount])) return -1;
            ++environmentCount;
        }
    }
    libcCopyPath(absolute, path);
    if (fstat(source, &status)) return -1;
    if (!S_ISREG(status.st_mode)) { errno = EACCES; return -1; }
    OsCredentialInfo credentials;
    int credentialError = __libc_os_credentials(0, 0, 0, 0, NULL, &credentials);
    if (credentialError) { errno = credentialError; return -1; }
    int member = credentials.effectiveGid == status.st_gid;
    for (uint32_t i = 0; i < credentials.groupCount; ++i) if (credentials.groups[i] == status.st_gid) member = 1;
    unsigned shift = credentials.effectiveUid == status.st_uid ? 6 : member ? 3 : 0;
    if ((!credentials.effectiveUid && !(status.st_mode & 0111)) ||
        (credentials.effectiveUid && !((status.st_mode >> shift) & 1))) { errno = EACCES; return -1; }
    if (status.st_size <= 0 || (uint64_t)status.st_size > maximum_image_size) {
        errno = ENOEXEC;
        return -1;
    }
    image = malloc((size_t)status.st_size);
    if (image == NULL) {
        errno = ENOMEM;
        return -1;
    }
    while (offset < (uint64_t)status.st_size) {
        const ssize_t count = pread(source, (uint8_t*)image + offset, (size_t)((uint64_t)status.st_size - offset), offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += (uint64_t)count;
    }
    if (offset != (uint64_t)status.st_size) {
        free(image);
        errno = EIO;
        return -1;
    }
    if (status.st_size >= 2 && ((char*)image)[0] == '#' && ((char*)image)[1] == '!') {
        int result = executeScript(source, absolute, descriptorExecution, closeOnExec, image, status.st_size, arguments, environment, depth);
        int error = errno; free(image); errno = error; return result;
    }
    /* Privilege-bearing ELF exec needs a trusted FS authorization transaction. */
    if (status.st_mode & (S_ISUID | S_ISGID)) { free(image); errno = ENOTSUP; return -1; }
    interpreterCount = __libc_os_image_interpreter(image, status.st_size, interpreter, sizeof(interpreter));
    if (interpreterCount < 0) { free(image); errno = ENOEXEC; return -1; }
    for (sharedIndex = 0; sharedIndex < interpreterCount;
         ++sharedIndex) {
        OsFileStat sharedStatus;
        uint64_t sharedDescriptor;
        uint64_t sharedOffset = 0;
        if (!__libc_os_stat_file(interpreter,
                          &sharedStatus) ||
            sharedStatus.type != OS_FILE_TYPE_FILE ||
            sharedStatus.size == 0 ||
            sharedStatus.size > maximum_image_size) {
            break;
        }
        sharedBuffers[sharedIndex] = malloc((size_t)sharedStatus.size);
        sharedDescriptor = __libc_os_open_file(
            libcFsService(), interpreter);
        if (sharedBuffers[sharedIndex] == NULL || sharedDescriptor == 0) {
            if (sharedDescriptor != 0) {
                __libc_os_close_file(libcFsService(), sharedDescriptor);
            }
            break;
        }
        while (sharedOffset < sharedStatus.size) {
            const int64_t count = __libc_os_read_file(
                libcFsService(), sharedDescriptor,
                (uint8_t*)sharedBuffers[sharedIndex] + sharedOffset,
                (size_t)(sharedStatus.size - sharedOffset));
            if (count <= 0) break;
            sharedOffset += (uint64_t)count;
        }
        __libc_os_close_file(libcFsService(), sharedDescriptor);
        if (sharedOffset != sharedStatus.size) break;
        sharedImages[sharedIndex].image = sharedBuffers[sharedIndex];
        sharedImages[sharedIndex].imageSize = sharedStatus.size;
        sharedImages[sharedIndex].name = interpreter;
    }
    if (sharedIndex != interpreterCount) {
        int cleanup;
        for (cleanup = 0; cleanup < LIBC_SHARED_IMAGE_COUNT; ++cleanup) {
            free(sharedBuffers[cleanup]);
        }
        free(image);
        errno = ENOENT;
        return -1;
    }
    runtimeState = malloc(sizeof(*runtimeState));
    if (!runtimeState) { for (sharedIndex = 0; sharedIndex < LIBC_SHARED_IMAGE_COUNT; ++sharedIndex) free(sharedBuffers[sharedIndex]); free(image); return -1; }
    libcExportExecState(runtimeState);
    info.image = image;
    info.imageSize = status.st_size;
    info.name = absolute;
    info.argumentCount = (uint32_t)argumentCount;
    info.environmentCount = (uint32_t)environmentCount;
    info.arguments = (const char* const*)arguments;
    info.environment = (const char* const*)environment;
    info.runtimeState = runtimeState;
    info.runtimeStateSize = sizeof(*runtimeState);
    info.sharedImageCount = interpreterCount;
    info.sharedReserved = 0;
    info.sharedImages = sharedImages;
    result = __libc_os_replace_task_image(&info);
    free(runtimeState);
    for (sharedIndex = 0; sharedIndex < LIBC_SHARED_IMAGE_COUNT;
         ++sharedIndex) {
        free(sharedBuffers[sharedIndex]);
    }
    free(image);
    errno = result == -5 ? ENOMEM :
            (result == -7 ? ENOEXEC : EINVAL);
    return -1;
}
static int executePath(const char* path, char* const arguments[], char* const environment[], unsigned depth) {
    if (!path || !arguments) { errno = EFAULT; return -1; }
    if (execString(path)) return -1;
    char absolute[LIBC_PATH_CAPACITY];
    if (libcPathAt(AT_FDCWD, path, absolute)) return -1;
    int descriptor = open(absolute, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return -1;
    int result = executeDescriptor(descriptor, absolute, 0, 1, arguments, environment, depth);
    int error = errno; close(descriptor); errno = error; return result;
}
int execve(const char* path, char* const arguments[], char* const environment[]) { return executePath(path, arguments, environment, 0); }
int fexecve(int descriptor, char* const arguments[], char* const environment[]) {
    if (!arguments) { errno = EFAULT; return -1; }
    const int flags = fcntl(descriptor, F_GETFD);
    if (flags < 0) return -1;
    int pinned = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if (pinned < 0) return -1;
    OpenDescription* description = libcDescriptor(pinned);
    const char* name = description && description->path[0] == '/' ? description->path : "/proc/self/exe";
    int result = executeDescriptor(pinned, name, 1, (flags & FD_CLOEXEC) != 0, arguments, environment, 0);
    int error = errno; close(pinned); errno = error; return result;
}

pid_t wait(int* status) { return waitpid(-1, status, 0); }

pid_t waitpid(pid_t taskId, int* status, int options) {
    if ((options & ~(WNOHANG | WUNTRACED | WCONTINUED)) != 0) {
        errno = EINVAL;
        return -1;
    }
    {
        int exitCode = 0;
        const int64_t result = __libc_os_wait_child(
            taskId, &exitCode, options);
        if (result < 0) {
            errno = result == -10 ? EINTR : ECHILD;
            return -1;
        }
        if (result == 0) return 0;
        if (WIFEXITED(exitCode) || WIFSIGNALED(exitCode)) forgetChild((pid_t)result);
        if (status != NULL) *status = exitCode;
        return (pid_t)result;
    }
}

int waitid(idtype_t type, id_t id, siginfo_t* info, int options) {
    if ((options & ~(WNOHANG | WSTOPPED | WEXITED | WCONTINUED | WNOWAIT)) ||
        !(options & (WSTOPPED | WEXITED | WCONTINUED)) ||
        (type != P_ALL && type != P_PID && type != P_PGID) || (type == P_PID && !id)) { errno = EINVAL; return -1; }
    if (!info || !__libc_os_accessible(info, sizeof(*info), 1)) { errno = EFAULT; return -1; }
    const int64_t selector = type == P_ALL ? -1 : type == P_PID ? (int64_t)id : id ? -(int64_t)id : 0;
    uint32_t uid = 0; int code = 0, status = 0;
    const int64_t result = __libc_os_wait_id(selector, options, &uid, &code, &status);
    if (result < 0) { errno = result == -10 ? EINTR : result == -1 ? EINVAL : ECHILD; return -1; }
    memset(info, 0, sizeof(*info));
    if (result) {
        info->si_signo = SIGCHLD; info->si_pid = result; info->si_uid = uid; info->si_code = code; info->si_status = status;
        if (!(options & WNOWAIT) && (code == CLD_EXITED || code == CLD_KILLED || code == CLD_DUMPED)) forgetChild((pid_t)result);
    }
    return 0;
}

int isatty(int fileDescriptor) {
    OpenDescription* description = libcDescriptor(fileDescriptor);
    if (description != NULL &&
        (description->kind == DESCRIPTOR_TERMINAL_READ ||
         description->kind == DESCRIPTOR_TERMINAL_WRITE ||
         ((description->kind == DESCRIPTOR_PIPE_READ ||
           description->kind == DESCRIPTOR_PIPE_WRITE) &&
          __libc_os_is_terminal_handle(description->handle)))) {
        return 1;
    }
    errno = ENOTTY;
    return 0;
}
