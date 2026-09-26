#pragma once

#include <stdint.h>

#include "kernel/spinlock.h"

namespace kernel {

struct Thread;

class WaitQueue {
   public:
    void initialize();
    void wait();
    bool wakeOne();
    uint32_t wakeAll();

   private:
    SpinLock lock_;
    Thread* head_ = nullptr;
    Thread* tail_ = nullptr;
};

class Mutex {
   public:
    void initialize();
    void lock();
    bool tryLock();
    void unlock();

   private:
    SpinLock lock_;
    Thread* owner_ = nullptr;
    Thread* waiters_ = nullptr;
    Thread* waitersTail_ = nullptr;
};

class Semaphore {
   public:
    void initialize(uint32_t count);
    void wait();
    bool tryWait();
    void signal();

   private:
    SpinLock lock_;
    uint32_t count_ = 0;
    Thread* waiters_ = nullptr;
    Thread* waitersTail_ = nullptr;
};

class Once {
   public:
    using Initializer = void (*)(void* argument);

    void initialize();
    void run(Initializer initializer, void* argument = nullptr);

   private:
    SpinLock lock_;
    uint8_t state_ = 0;
    Thread* waiters_ = nullptr;
    Thread* waitersTail_ = nullptr;
};

void runSynchronizationSelfTest();

}  // namespace kernel
