#pragma once

#include <cstdint>

#include "kernel/spinlock.h"

namespace kernel {

class Thread;

class WaitQueue {
   public:
    void init();
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
    void init();
    void lock();
    bool tryLock();
    void unlock();

   private:
    SpinLock lock_;
    Thread* owner_       = nullptr;
    Thread* waiters_     = nullptr;
    Thread* waitersTail_ = nullptr;
};

class Semaphore {
   public:
    void init(uint32_t count);
    void wait();
    bool tryWait();
    void signal();

   private:
    SpinLock lock_;
    uint32_t count_      = 0;
    Thread* waiters_     = nullptr;
    Thread* waitersTail_ = nullptr;
};

class Once {
   public:
    using Initializer = void (*)(void* argument);

    void init();
    void run(Initializer initializer, void* argument = nullptr);

   private:
    SpinLock lock_;
    uint8_t state_       = 0;
    Thread* waiters_     = nullptr;
    Thread* waitersTail_ = nullptr;
};

namespace sync {
void runSynchronizationSelfTest();

int waitOnUserAddress(volatile uint32_t* address, uint32_t expected);
uint32_t wakeUserAddress(volatile uint32_t* address, uint32_t count);

}  // namespace sync
}  // namespace kernel
