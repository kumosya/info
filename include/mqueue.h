#pragma once

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

typedef uint64_t mqd_t;

struct mq_attr {
    long mq_flags;
    long mq_maxmsg;
    long mq_msgsize;
    long mq_curmsgs;
};

#define MQ_PRIO_MAX 32

#ifdef __cplusplus
extern "C" {
#endif

mqd_t mq_open(const char* name, int flags, ...);
int mq_close(mqd_t descriptor);
int mq_unlink(const char* name);
int mq_send(mqd_t descriptor, const char* message, size_t length,
            unsigned int priority);
ssize_t mq_receive(mqd_t descriptor, char* message, size_t length,
                   unsigned int* priority);
int mq_timedsend(mqd_t descriptor, const char* message, size_t length,
                 unsigned int priority,
                 const struct timespec* absolute_time);
ssize_t mq_timedreceive(mqd_t descriptor, char* message, size_t length,
                        unsigned int* priority,
                        const struct timespec* absolute_time);
int mq_getattr(mqd_t descriptor, struct mq_attr* attributes);
int mq_setattr(mqd_t descriptor, const struct mq_attr* attributes,
               struct mq_attr* old_attributes);
int mq_notify(mqd_t descriptor, const struct sigevent* notification);

#ifdef __cplusplus
}
#endif
