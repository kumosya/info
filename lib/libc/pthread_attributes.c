#include <errno.h>
#include <pthread.h>
#include <stdint.h>

/* 所有线程由内核调度，当前只有 SYSTEM 竞争域。 */
int pthread_attr_getscope(const pthread_attr_t* restrict attr, int* restrict scope)
{
    if (!attr || !scope) return EINVAL;
    *scope = PTHREAD_SCOPE_SYSTEM;
    return 0;
}

int pthread_attr_setscope(pthread_attr_t* attr, int scope)
{
    if (!attr) return EINVAL;
    if (scope == PTHREAD_SCOPE_PROCESS) return ENOTSUP;
    return scope == PTHREAD_SCOPE_SYSTEM ? 0 : EINVAL;
}

/* 回读请求值；创建时才把保护区大小向上对齐到页面。 */
int pthread_attr_getguardsize(const pthread_attr_t* restrict attr, size_t* restrict size)
{
    if (!attr || !size) return EINVAL;
    *size = attr->guard_size;
    return 0;
}

int pthread_attr_setguardsize(pthread_attr_t* attr, size_t size)
{
    if (!attr || size > SIZE_MAX - 4095) return EINVAL;
    attr->guard_size = size;
    return 0;
}

/* 地址是可用区间的起点，不能误用为栈顶；自供栈不会由库释放。 */
int pthread_attr_getstack(const pthread_attr_t* restrict attr,
                           void** restrict address, size_t* restrict size)
{
    if (!attr || !address || !size) return EINVAL;
    *address = attr->stack_address;
    *size = attr->stack_size;
    return 0;
}

int pthread_attr_setstack(pthread_attr_t* attr, void* address, size_t size)
{
    if (!attr || !address || size < PTHREAD_STACK_MIN ||
        ((uintptr_t)address & 15) || (size & 15) ||
        (uintptr_t)address > UINTPTR_MAX - size) return EINVAL;
    attr->stack_address = address;
    attr->stack_size = size;
    return 0;
}

/* 等待按实际共享页和页内偏移定位，因此不同虚拟地址映射可相互唤醒。 */
static int valid_shared(int shared)
{
    return shared == PTHREAD_PROCESS_PRIVATE || shared == PTHREAD_PROCESS_SHARED;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t* restrict attr, int* restrict shared)
{
    if (!attr || !shared) return EINVAL;
    *shared = attr->shared;
    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t* attr, int shared)
{
    if (!attr || !valid_shared(shared)) return EINVAL;
    attr->shared = shared;
    return 0;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t* restrict attr, int* restrict robust)
{
    if (!attr || !robust) return EINVAL;
    *robust = attr->robust;
    return 0;
}

/* 退出持锁者的内核回收尚未接线时，不允许声称已支持 robust。 */
int pthread_mutexattr_setrobust(pthread_mutexattr_t* attr, int robust)
{
    if (!attr || (robust != PTHREAD_MUTEX_STALLED && robust != PTHREAD_MUTEX_ROBUST)) return EINVAL;
    if (robust == PTHREAD_MUTEX_ROBUST) return ENOTSUP;
    attr->robust = robust;
    return 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t* restrict attr, int* restrict shared)
{
    if (!attr || !shared) return EINVAL;
    *shared = attr->shared;
    return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t* attr, int shared)
{
    if (!attr || !valid_shared(shared)) return EINVAL;
    attr->shared = shared;
    return 0;
}

/* 读写锁和屏障属性已有独立整数存储，沿用布局，销毁时标记无效。 */
#define SHARED_ATTRIBUTES(prefix, type) \
int prefix##_init(type* attr) \
{ \
    if (!attr) return EINVAL; \
    attr->reserved = PTHREAD_PROCESS_PRIVATE; \
    return 0; \
} \
int prefix##_destroy(type* attr) \
{ \
    if (!attr || !valid_shared(attr->reserved)) return EINVAL; \
    attr->reserved = -1; \
    return 0; \
} \
int prefix##_getpshared(const type* restrict attr, int* restrict shared) \
{ \
    if (!attr || !shared || !valid_shared(attr->reserved)) return EINVAL; \
    *shared = attr->reserved; \
    return 0; \
} \
int prefix##_setpshared(type* attr, int shared) \
{ \
    if (!attr || !valid_shared(attr->reserved) || !valid_shared(shared)) return EINVAL; \
    attr->reserved = shared; \
    return 0; \
}

SHARED_ATTRIBUTES(pthread_rwlockattr, pthread_rwlockattr_t)
SHARED_ATTRIBUTES(pthread_barrierattr, pthread_barrierattr_t)
