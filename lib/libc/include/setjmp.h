/* Public domain.  */
#ifndef _SETJMP_H
#define _SETJMP_H

#include <stddef.h>

/* C++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/* Define jmp_buf type - implementation specific */
#define _JBLEN 16

typedef struct {
    int buf[_JBLEN];
} jmp_buf;

typedef jmp_buf sigjmp_buf;

/* Function declarations */
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
int sigsetjmp(sigjmp_buf env, int savesigs);
void siglongjmp(sigjmp_buf env, int val);

#ifdef __cplusplus
}
#endif

#endif /* _SETJMP_H */