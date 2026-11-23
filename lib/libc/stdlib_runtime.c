#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "internal.h"

typedef struct ExitHandler {
    void (*plain)(void);
    void (*destructor)(void*);
    void* object;
    void* module;
    struct ExitHandler* next;
} ExitHandler;

static ExitHandler* exitHandlers;
static unsigned char exitHandlerLock;
static uint32_t randomState = 1;

int atoi(const char* string) { return (int)strtol(string, NULL, 10); }

int rand(void) {
    randomState = randomState * 1103515245U + 12345U;
    return (int)((randomState >> 16) & RAND_MAX);
}

void srand(unsigned int seed) { randomState = seed; }

/* 登记和取出节点时加锁，执行用户回调时释放锁。 */
static void lockExitHandlers(void) {
    while (__atomic_test_and_set(&exitHandlerLock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile ("pause");
    }
}

static void unlockExitHandlers(void) {
    __atomic_clear(&exitHandlerLock, __ATOMIC_RELEASE);
}

/* fork 与登记表修改互斥，孩子继承回调内容并重新开始使用锁。 */
void __libc_exit_fork_prepare(void) {
    lockExitHandlers();
}

void __libc_exit_fork_parent(void) {
    unlockExitHandlers();
}

void __libc_exit_fork_child(void) {
    exitHandlerLock = 0;
}

/* C 和 C++ 回调共用一条链表，保持两类登记之间的逆序关系。 */
static int registerExitHandler(void (*plain)(void), void (*destructor)(void*),
                               void* object, void* module) {
    if (plain == NULL && destructor == NULL) {
        return -1;
    }

    ExitHandler* entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        return -1;
    }

    entry->plain = plain;
    entry->destructor = destructor;
    entry->object = object;
    entry->module = module;

    lockExitHandlers();
    entry->next = exitHandlers;
    exitHandlers = entry;
    unlockExitHandlers();
    return 0;
}

/* 登记普通 C 退出回调。 */
int atexit(void (*function)(void)) {
    return registerExitHandler(function, NULL, NULL, NULL);
}

/* C++ 编译器在构造全局对象后登记析构函数及所属模块。 */
int __cxa_atexit(void (*destructor)(void*), void* object, void* module) {
    return registerExitHandler(NULL, destructor, object, module);
}

/* 指定模块时只处理它的回调；空模块参数按逆序处理整个登记表。 */
static void finalizeExitHandlers(void* module) {
    for (;;) {
        lockExitHandlers();
        ExitHandler** position = &exitHandlers;
        while (*position != NULL && module != NULL && (*position)->module != module) {
            position = &(*position)->next;
        }

        ExitHandler* entry = *position;
        if (entry != NULL) {
            *position = entry->next;
        }
        unlockExitHandlers();

        if (entry == NULL) {
            return;
        }

        /* 先移出登记表，回调里再次 finalize 或继续登记时也能正常工作。 */
        ExitHandler callback = *entry;
        free(entry);
        if (callback.plain != NULL) {
            callback.plain();
        } else {
            callback.destructor(callback.object);
        }
    }
}

/* C++ 模块清理入口。 */
void __cxa_finalize(void* module) {
    finalizeExitHandlers(module);
}

/* exit 的公共清理入口。 */
void __libc_run_exit_handlers(void) {
    finalizeExitHandlers(NULL);
}

void exit(int status) {
    __libc_run_exit_handlers();
    fflush(NULL);
    __libc_os_runtime_fini();
    libcCloseAllDescriptors();
    __libc_os_exit_task(status);
}

void _exit(int status) {
    libcCloseAllDescriptors();
    __libc_os_exit_task(status);
}
void abort(void) {
    libcCloseAllDescriptors();
    __libc_os_exit_task(134);
}
