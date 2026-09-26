#pragma once

#include <signal.h>
#include <sys/types.h>

#define POSIX_SPAWN_RESETIDS 0x01
#define POSIX_SPAWN_SETPGROUP 0x02
#define POSIX_SPAWN_SETSIGDEF 0x04
#define POSIX_SPAWN_SETSIGMASK 0x08

#define POSIX_SPAWN_FILE_ACTION_CAPACITY 16

typedef struct {
    int type;
    int file_descriptor;
    int new_file_descriptor;
    int flags;
    mode_t mode;
    const char* path;
} posix_spawn_file_action_t;

typedef struct {
    unsigned int count;
    posix_spawn_file_action_t actions[POSIX_SPAWN_FILE_ACTION_CAPACITY];
} posix_spawn_file_actions_t;

typedef struct {
    short flags;
    pid_t process_group;
    sigset_t signal_default;
    sigset_t signal_mask;
} posix_spawnattr_t;

#ifdef __cplusplus
extern "C" {
#endif

int posix_spawn(pid_t* process_id, const char* path,
                const posix_spawn_file_actions_t* file_actions,
                const posix_spawnattr_t* attributes,
                char* const arguments[], char* const environment[]);
int posix_spawnp(pid_t* process_id, const char* file,
                 const posix_spawn_file_actions_t* file_actions,
                 const posix_spawnattr_t* attributes,
                 char* const arguments[], char* const environment[]);

int posix_spawn_file_actions_init(posix_spawn_file_actions_t* actions);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* actions);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* actions,
                                      int file_descriptor);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* actions,
                                     int file_descriptor,
                                     int new_file_descriptor);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* actions,
                                     int file_descriptor, const char* path,
                                     int flags, mode_t mode);

int posix_spawnattr_init(posix_spawnattr_t* attributes);
int posix_spawnattr_destroy(posix_spawnattr_t* attributes);
int posix_spawnattr_getflags(const posix_spawnattr_t* attributes,
                             short* flags);
int posix_spawnattr_setflags(posix_spawnattr_t* attributes, short flags);
int posix_spawnattr_getpgroup(const posix_spawnattr_t* attributes,
                              pid_t* process_group);
int posix_spawnattr_setpgroup(posix_spawnattr_t* attributes,
                              pid_t process_group);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t* attributes,
                                  sigset_t* signals);
int posix_spawnattr_setsigdefault(posix_spawnattr_t* attributes,
                                  const sigset_t* signals);
int posix_spawnattr_getsigmask(const posix_spawnattr_t* attributes,
                               sigset_t* signals);
int posix_spawnattr_setsigmask(posix_spawnattr_t* attributes,
                               const sigset_t* signals);

#ifdef __cplusplus
}
#endif
