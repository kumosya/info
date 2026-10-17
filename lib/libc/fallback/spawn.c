/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <spawn.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int posix_spawn_file_actions_addchdir(posix_spawn_file_actions_t *restrict arg0, const char *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int posix_spawn_file_actions_addfchdir(posix_spawn_file_actions_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int posix_spawnattr_getschedparam(const posix_spawnattr_t *restrict arg0, struct sched_param *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int posix_spawnattr_setschedparam(posix_spawnattr_t *restrict arg0, const struct sched_param *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int posix_spawnattr_setschedpolicy(posix_spawnattr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
