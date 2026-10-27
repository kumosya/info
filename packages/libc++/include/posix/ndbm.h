#ifndef _POSIX_NDBM_H
#define _POSIX_NDBM_H

#include <sys/types.h>
typedef struct {
    char *dptr;
    int dsize;
} datum;
typedef struct OsDbm DBM;
#define DBM_INSERT 0
#define DBM_REPLACE 1

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_datum
#define __DEFINED_DBM

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int dbm_clearerr(DBM *);
void dbm_close(DBM *);
int dbm_delete(DBM *, datum);
int dbm_error(DBM *);
datum dbm_fetch(DBM *, datum);
datum dbm_firstkey(DBM *);
datum dbm_nextkey(DBM *);
DBM *dbm_open(const char *, int, mode_t);
int dbm_store(DBM *, datum, datum, int);
#ifdef __cplusplus
}
#endif

#endif
