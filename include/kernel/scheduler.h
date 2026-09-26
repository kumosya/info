#pragma once

#include <stdint.h>

#include "kernel/spinlock.h"
#include "kernel/thread.h"

namespace kernel {

struct RunQueue {
    Thread* root;
    Thread* leftmost;
    Thread* currentThread;
    Thread* idleThread;
    Thread* sleepingThreads;
    uint64_t runnableCount;
    uint64_t minVruntime;
    uint64_t clock;
    SpinLock lock;
};

void initScheduler();
bool isSchedulerInitialized();
void enqueueThread(Thread* thread);
void dequeueThread(Thread* thread);
Thread* pickNextThread();
void schedule();
void yieldThread();
void requestReschedule();
void schedulerTick();
void sleepCurrentThread(uint64_t milliseconds);
bool setThreadAffinity(Thread* thread, uint64_t cpuMask);
bool migrateThread(Thread* thread, uint32_t cpuId);
uint32_t getSchedulerWeight(int32_t nice);

}  // namespace kernel
