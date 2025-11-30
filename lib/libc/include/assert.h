#ifndef LIBC_ASSERT_H
#define LIBC_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Define NDEBUG to disable all assert macro expansions */
#ifdef NDEBUG
#define assert(condition) ((void)0)
#else
/* Simple assert implementation that halts execution on failure */
#define assert(condition) ((void)((condition) ? 0 : __assert_fail(#condition, __FILE__, __LINE__, __func__)))

/* Assert failure handler */
void __assert_fail(const char *condition, const char *file, int line, const char *function);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LIBC_ASSERT_H */
