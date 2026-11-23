/* 常用的位宽、路径长度和整数大小比较宏。 */
#ifndef _POSIX_SYS_PARAM_H
#define _POSIX_SYS_PARAM_H

#include <limits.h>
#include <endian.h>

#define NBBY CHAR_BIT
#define MAXPATHLEN PATH_MAX

#ifndef MIN
#define MIN(left, right) ((left) < (right) ? (left) : (right))
#endif

#ifndef MAX
#define MAX(left, right) ((left) > (right) ? (left) : (right))
#endif

#endif
