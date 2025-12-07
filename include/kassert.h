#ifndef KASSERT_H
#define KASSERT_H

#include <stdint.h>

void kassert_fail(const char *expr, const char *file, int line, const char *msg);

#define KASSERT(expr) \
    do { if (!(expr)) kassert_fail(#expr, __FILE__, __LINE__, NULL); } while (false)

#define KASSERT_MSG(expr, msg) \
    do { if (!(expr)) kassert_fail(#expr, __FILE__, __LINE__, (msg)); } while (false)

#endif // KASSERT_H
