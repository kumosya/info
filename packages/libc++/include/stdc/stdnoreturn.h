/*
 * 不返回函数的兼容标记。
 */

#ifndef _STDNORETURN_H
#define _STDNORETURN_H
#ifndef __cplusplus
#include <features.h>
#define noreturn _Noreturn
#endif
#endif
