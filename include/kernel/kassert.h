#ifndef INFO_KERNEL_KASSERT_H_
#define INFO_KERNEL_KASSERT_H_

void kassert_fail(const char *expr, const char *file, int line,
                  const char *msg);

#define KASSERT(expr)                                               \
    do {                                                            \
        if (!(expr)) kassert_fail(#expr, __FILE__, __LINE__, NULL); \
    } while (false)

#define KASSERT_MSG(expr, msg)                                       \
    do {                                                             \
        if (!(expr)) kassert_fail(#expr, __FILE__, __LINE__, (msg)); \
    } while (false)

#endif  // INFO_KERNEL_KASSERT_H_
