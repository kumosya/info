/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <ndbm.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int dbm_clearerr(DBM * arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) void dbm_close(DBM * arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) int dbm_delete(DBM * arg0, datum arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int dbm_error(DBM * arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) datum dbm_fetch(DBM * arg0, datum arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (datum){0}; }
__attribute__((weak)) datum dbm_firstkey(DBM * arg0) { (void)arg0; errno = ENOSYS; return (datum){0}; }
__attribute__((weak)) datum dbm_nextkey(DBM * arg0) { (void)arg0; errno = ENOSYS; return (datum){0}; }
__attribute__((weak)) DBM * dbm_open(const char * arg0, int arg1, mode_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (DBM *)0; }
__attribute__((weak)) int dbm_store(DBM * arg0, datum arg1, datum arg2, int arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
