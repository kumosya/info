#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "internal.h"
#include "internal.h"

#define SPAWN_ACTION_CLOSE 1
#define SPAWN_ACTION_DUP2 2
#define SPAWN_ACTION_OPEN 3
#define SPAWN_ACTION_CHDIR 4
#define SPAWN_ACTION_FCHDIR 5
#define SPAWN_ARGUMENT_CAPACITY 64

static int append_action(posix_spawn_file_actions_t* actions, int type,
                         int file_descriptor, int new_file_descriptor,
                         const char* path, int flags, mode_t mode) {
    posix_spawn_file_action_t* action;
    if (actions == NULL) return EINVAL;
    if (actions->count >= POSIX_SPAWN_FILE_ACTION_CAPACITY) return ENOMEM;
    char* copied = NULL;
    if (path) { size_t length = strnlen(path, LIBC_PATH_CAPACITY); if (length == LIBC_PATH_CAPACITY) return ENAMETOOLONG; copied = malloc(length + 1); if (!copied) return ENOMEM; memcpy(copied, path, length + 1); }
    action = &actions->actions[actions->count++];
    action->type = type;
    action->file_descriptor = file_descriptor;
    action->new_file_descriptor = new_file_descriptor;
    action->path = copied;
    action->flags = flags;
    action->mode = mode;
    return 0;
}
static _Noreturn void spawn_failure(int descriptor, int error) {
    const unsigned char* bytes = (const unsigned char*)&error; size_t written = 0;
    while (written < sizeof(error)) { ssize_t count = write(descriptor, bytes + written, sizeof(error) - written); if (count < 0 && errno == EINTR) continue; if (count <= 0) break; written += (size_t)count; }
    _exit(127);
}
static int mentioned(const posix_spawn_file_actions_t* actions, int fd) {
    if (!actions) return 0;
    for (unsigned i = 0; i < actions->count; ++i) {
        const posix_spawn_file_action_t* action = &actions->actions[i];
        if (action->file_descriptor == fd || (action->type == SPAWN_ACTION_DUP2 && action->new_file_descriptor == fd)) return 1;
    }
    return 0;
}
static int protect_status_fd(int fd, const posix_spawn_file_actions_t* actions) {
    if (!mentioned(actions, fd)) return fd;
    for (int i = 3; i < LIBC_DESCRIPTOR_CAPACITY; ++i) {
        if (mentioned(actions, i) || libcDescriptor(i)) continue;
        int moved = fcntl(fd, F_DUPFD_CLOEXEC, i);
        if (moved < 0) continue;
        if (mentioned(actions, moved)) { close(moved); continue; }
        close(fd); return moved;
    }
    errno = EMFILE; return -1;
}

static int spawn_impl(pid_t* task_id, const char* path,
                      const posix_spawn_file_actions_t* file_actions,
                      const posix_spawnattr_t* attributes,
                      char* const arguments[], char* const environment[]) {
    int argument_count = 0;
    unsigned int index;
    pid_t task;
    if (task_id == NULL || path == NULL || arguments == NULL) return EINVAL;
    while (arguments[argument_count] != NULL) {
        if (argument_count == SPAWN_ARGUMENT_CAPACITY) return E2BIG;
        ++argument_count;
    }
    if (argument_count == 0) return EINVAL;
    if (attributes && (attributes->flags & ~(POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK))) return EINVAL;
    if (file_actions != NULL) {
        if (file_actions->count > POSIX_SPAWN_FILE_ACTION_CAPACITY) return EINVAL;
        for (index = 0; index < file_actions->count; ++index) {
            const posix_spawn_file_action_t* action =
                &file_actions->actions[index];
            if (action->type == SPAWN_ACTION_CLOSE) {
                if (action->file_descriptor < 0) return EBADF;
            } else if (action->type == SPAWN_ACTION_DUP2) {
                if (action->file_descriptor < 0 ||
                    action->new_file_descriptor < 0) return EBADF;
            } else if (action->type == SPAWN_ACTION_OPEN) {
                if (action->file_descriptor < 0 || action->path == NULL) {
                    return EINVAL;
                }
            } else if (action->type == SPAWN_ACTION_CHDIR) {
                if (!action->path) return EINVAL;
            } else if (action->type == SPAWN_ACTION_FCHDIR) {
                if (action->file_descriptor < 0) return EBADF;
            } else {
                return EINVAL;
            }
        }
    }
    int status_pipe[2];
    if (pipe2(status_pipe, O_CLOEXEC)) return errno;
    int protected_fd = protect_status_fd(status_pipe[1], file_actions);
    if (protected_fd < 0) { int error = errno; close(status_pipe[0]); close(status_pipe[1]); return error; }
    status_pipe[1] = protected_fd;
    task = fork();
    if (task < 0) { int error = errno; close(status_pipe[0]); close(status_pipe[1]); return error; }
    if (task == 0) {
        close(status_pipe[0]);
        if (attributes != NULL &&
            (attributes->flags & POSIX_SPAWN_SETPGROUP) != 0 &&
            __libc_os_set_task_group((uint64_t)attributes->task_group, 0) != 0) {
            spawn_failure(status_pipe[1], EPERM);
        }
        if (file_actions != NULL) {
            for (index = 0; index < file_actions->count; ++index) {
                const posix_spawn_file_action_t* action =
                    &file_actions->actions[index];
                if (action->type == SPAWN_ACTION_CLOSE) {
                    close(action->file_descriptor);
                } else if (action->type == SPAWN_ACTION_DUP2) {
                    if (dup2(action->file_descriptor,
                             action->new_file_descriptor) < 0) {
                        spawn_failure(status_pipe[1], errno);
                    }
                } else if (action->type == SPAWN_ACTION_OPEN) {
                    const int descriptor = open(action->path, action->flags,
                                                action->mode);
                    if (descriptor < 0 ||
                        (descriptor != action->file_descriptor &&
                         dup2(descriptor, action->file_descriptor) < 0)) {
                        spawn_failure(status_pipe[1], errno);
                    }
                    if (descriptor != action->file_descriptor) {
                        close(descriptor);
                    }
                } else if (action->type == SPAWN_ACTION_CHDIR) {
                    if (chdir(action->path)) spawn_failure(status_pipe[1], errno);
                } else if (action->type == SPAWN_ACTION_FCHDIR) {
                    if (fchdir(action->file_descriptor)) spawn_failure(status_pipe[1], errno);
                }
            }
        }
        if (attributes != NULL) {
            if ((attributes->flags & POSIX_SPAWN_RESETIDS) && (setegid(getgid()) || seteuid(getuid()))) spawn_failure(status_pipe[1], errno);
            if ((attributes->flags & POSIX_SPAWN_SETSIGMASK) != 0 &&
                sigprocmask(SIG_SETMASK, &attributes->signal_mask, NULL) != 0) {
                spawn_failure(status_pipe[1], errno);
            }
            if ((attributes->flags & POSIX_SPAWN_SETSIGDEF) != 0) {
                int signal_number;
                for (signal_number = 1; signal_number <= 64;
                     ++signal_number) {
                    if ((attributes->signal_default &
                         (1ULL << (signal_number - 1))) != 0) {
                        signal(signal_number, SIG_DFL);
                    }
                }
            }
        }
        execve(path, arguments, environment);
        spawn_failure(status_pipe[1], errno);
    }
    close(status_pipe[1]);
    int error = 0, read_error = 0; size_t received = 0;
    while (received < sizeof(error)) {
        ssize_t count = read(status_pipe[0], (unsigned char*)&error + received, sizeof(error) - received);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { error = errno; read_error = 1; received = sizeof(error); break; }
        if (!count) break;
        received += (size_t)count;
    }
    close(status_pipe[0]);
    if (received) {
        if (received != sizeof(error) || !error) error = EIO;
        if (read_error) kill(task, SIGKILL);
        int status; while (waitpid(task, &status, 0) < 0 && errno == EINTR) {}
        return error;
    }
    *task_id = task;
    return 0;
}

int posix_spawn(pid_t* task_id, const char* path,
                const posix_spawn_file_actions_t* file_actions,
                const posix_spawnattr_t* attributes,
                char* const arguments[], char* const environment[]) {
    const int saved = errno;
    const int result = spawn_impl(task_id, path, file_actions, attributes, arguments, environment);
    errno = saved;
    return result;
}

int posix_spawnp(pid_t* task_id, const char* file,
                 const posix_spawn_file_actions_t* file_actions,
                 const posix_spawnattr_t* attributes,
                 char* const arguments[], char* const environment[]) {
    char path[96];
    size_t length;
    if (file == NULL) return EINVAL;
    if (file[0] == '/') {
        return spawn_impl(task_id, file, file_actions, attributes, arguments,
                          environment);
    }
    length = strlen(file);
    if (length + 6 >= sizeof(path)) return ENAMETOOLONG;
    memcpy(path, "/bin/", 5);
    memcpy(path + 5, file, length + 1);
    return spawn_impl(task_id, path, file_actions, attributes, arguments,
                      environment);
}

int posix_spawn_file_actions_init(posix_spawn_file_actions_t* actions) {
    if (actions == NULL) return EINVAL;
    memset(actions, 0, sizeof(*actions));
    return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* actions) {
    if (actions == NULL || actions->count > POSIX_SPAWN_FILE_ACTION_CAPACITY) return EINVAL;
    for (unsigned i = 0; i < actions->count; ++i) free((void*)actions->actions[i].path);
    memset(actions, 0, sizeof(*actions));
    return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* actions,
                                      int file_descriptor) {
    if (file_descriptor < 0) return EBADF;
    return append_action(actions, SPAWN_ACTION_CLOSE, file_descriptor, -1,
                         NULL, 0, 0);
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* actions,
                                     int file_descriptor,
                                     int new_file_descriptor) {
    if (file_descriptor < 0 || new_file_descriptor < 0) return EBADF;
    return append_action(actions, SPAWN_ACTION_DUP2, file_descriptor,
                         new_file_descriptor, NULL, 0, 0);
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* actions,
                                     int file_descriptor, const char* path,
                                     int flags, mode_t mode) {
    if (file_descriptor < 0) return EBADF;
    if (path == NULL) return EINVAL;
    return append_action(actions, SPAWN_ACTION_OPEN, file_descriptor, -1, path,
                         flags, mode);
}
int posix_spawn_file_actions_addchdir(posix_spawn_file_actions_t* restrict actions, const char* restrict path) {
    if (!path) return EINVAL;
    return append_action(actions, SPAWN_ACTION_CHDIR, -1, -1, path, 0, 0);
}
int posix_spawn_file_actions_addfchdir(posix_spawn_file_actions_t* actions, int descriptor) {
    if (descriptor < 0) return EBADF;
    return append_action(actions, SPAWN_ACTION_FCHDIR, descriptor, -1, NULL, 0, 0);
}

int posix_spawnattr_init(posix_spawnattr_t* attributes) {
    if (attributes == NULL) return EINVAL;
    memset(attributes, 0, sizeof(*attributes));
    return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t* attributes) {
    if (attributes == NULL) return EINVAL;
    memset(attributes, 0, sizeof(*attributes));
    return 0;
}

int posix_spawnattr_getflags(const posix_spawnattr_t* attributes,
                             short* flags) {
    if (attributes == NULL || flags == NULL) return EINVAL;
    *flags = attributes->flags;
    return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t* attributes, short flags) {
    const short supported = POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP |
                            POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
    if (attributes == NULL || (flags & ~supported) != 0) return EINVAL;
    attributes->flags = flags;
    return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t* attributes,
                              pid_t* task_group) {
    if (attributes == NULL || task_group == NULL) return EINVAL;
    *task_group = attributes->task_group;
    return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t* attributes,
                              pid_t task_group) {
    if (attributes == NULL || task_group < 0) return EINVAL;
    attributes->task_group = task_group;
    return 0;
}

int posix_spawnattr_getsigdefault(const posix_spawnattr_t* attributes,
                                  sigset_t* signals) {
    if (attributes == NULL || signals == NULL) return EINVAL;
    *signals = attributes->signal_default;
    return 0;
}

int posix_spawnattr_setsigdefault(posix_spawnattr_t* attributes,
                                  const sigset_t* signals) {
    if (attributes == NULL || signals == NULL) return EINVAL;
    attributes->signal_default = *signals;
    return 0;
}

int posix_spawnattr_getsigmask(const posix_spawnattr_t* attributes,
                               sigset_t* signals) {
    if (attributes == NULL || signals == NULL) return EINVAL;
    *signals = attributes->signal_mask;
    return 0;
}

int posix_spawnattr_setsigmask(posix_spawnattr_t* attributes,
                               const sigset_t* signals) {
    if (attributes == NULL || signals == NULL) return EINVAL;
    attributes->signal_mask = *signals;
    return 0;
}
