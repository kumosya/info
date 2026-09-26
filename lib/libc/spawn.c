#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "internal.h"

#define SPAWN_ACTION_CLOSE 1
#define SPAWN_ACTION_DUP2 2
#define SPAWN_ACTION_OPEN 3
#define SPAWN_ARGUMENT_CAPACITY 8

static int append_action(posix_spawn_file_actions_t* actions, int type,
                         int file_descriptor, int new_file_descriptor,
                         const char* path, int flags, mode_t mode) {
    posix_spawn_file_action_t* action;
    if (actions == NULL ||
        actions->count >= POSIX_SPAWN_FILE_ACTION_CAPACITY) return ENOMEM;
    action = &actions->actions[actions->count++];
    action->type = type;
    action->file_descriptor = file_descriptor;
    action->new_file_descriptor = new_file_descriptor;
    action->path = path;
    action->flags = flags;
    action->mode = mode;
    return 0;
}

static int spawn_impl(pid_t* process_id, const char* path,
                      const posix_spawn_file_actions_t* file_actions,
                      const posix_spawnattr_t* attributes,
                      char* const arguments[], char* const environment[]) {
    int argument_count = 0;
    unsigned int index;
    pid_t process;
    if (process_id == NULL || path == NULL || arguments == NULL) return EINVAL;
    if (attributes != NULL &&
        (attributes->flags & POSIX_SPAWN_SETPGROUP) != 0) return ENOTSUP;
    while (arguments[argument_count] != NULL) {
        if (argument_count == SPAWN_ARGUMENT_CAPACITY) return E2BIG;
        ++argument_count;
    }
    if (argument_count == 0) return EINVAL;
    if (file_actions != NULL) {
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
            } else {
                return EINVAL;
            }
        }
    }
    process = fork();
    if (process < 0) return errno;
    if (process == 0) {
        if (file_actions != NULL) {
            for (index = 0; index < file_actions->count; ++index) {
                const posix_spawn_file_action_t* action =
                    &file_actions->actions[index];
                if (action->type == SPAWN_ACTION_CLOSE) {
                    close(action->file_descriptor);
                } else if (action->type == SPAWN_ACTION_DUP2) {
                    if (dup2(action->file_descriptor,
                             action->new_file_descriptor) < 0) {
                        _exit(127);
                    }
                } else if (action->type == SPAWN_ACTION_OPEN) {
                    const int descriptor = open(action->path, action->flags,
                                                action->mode);
                    if (descriptor < 0 ||
                        (descriptor != action->file_descriptor &&
                         dup2(descriptor, action->file_descriptor) < 0)) {
                        _exit(127);
                    }
                    if (descriptor != action->file_descriptor) {
                        close(descriptor);
                    }
                }
            }
        }
        if (attributes != NULL) {
            if ((attributes->flags & POSIX_SPAWN_SETSIGMASK) != 0 &&
                sigprocmask(SIG_SETMASK, &attributes->signal_mask, NULL) != 0) {
                _exit(127);
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
        _exit(127);
    }
    *process_id = process;
    return 0;
}

int posix_spawn(pid_t* process_id, const char* path,
                const posix_spawn_file_actions_t* file_actions,
                const posix_spawnattr_t* attributes,
                char* const arguments[], char* const environment[]) {
    return spawn_impl(process_id, path, file_actions, attributes, arguments,
                      environment);
}

int posix_spawnp(pid_t* process_id, const char* file,
                 const posix_spawn_file_actions_t* file_actions,
                 const posix_spawnattr_t* attributes,
                 char* const arguments[], char* const environment[]) {
    char path[96];
    size_t length;
    if (file == NULL) return EINVAL;
    if (file[0] == '/') {
        return spawn_impl(process_id, file, file_actions, attributes, arguments,
                          environment);
    }
    length = strlen(file);
    if (length + 6 >= sizeof(path)) return ENAMETOOLONG;
    memcpy(path, "/bin/", 5);
    memcpy(path + 5, file, length + 1);
    return spawn_impl(process_id, path, file_actions, attributes, arguments,
                      environment);
}

int posix_spawn_file_actions_init(posix_spawn_file_actions_t* actions) {
    if (actions == NULL) return EINVAL;
    memset(actions, 0, sizeof(*actions));
    return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* actions) {
    if (actions == NULL) return EINVAL;
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
    if (file_descriptor < 0 || path == NULL) return EINVAL;
    return append_action(actions, SPAWN_ACTION_OPEN, file_descriptor, -1, path,
                         flags, mode);
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
                              pid_t* process_group) {
    if (attributes == NULL || process_group == NULL) return EINVAL;
    *process_group = attributes->process_group;
    return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t* attributes,
                              pid_t process_group) {
    if (attributes == NULL || process_group < 0) return EINVAL;
    attributes->process_group = process_group;
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
