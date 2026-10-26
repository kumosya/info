#ifndef _OS_SYNC_H
#define _OS_SYNC_H

#include <cstdint>

namespace os {

class SpinLock {
   public:
    class Guard {
       public:
        explicit Guard(SpinLock& lock);
        ~Guard();

        Guard(const Guard&)            = delete;
        Guard& operator=(const Guard&) = delete;

       private:
        SpinLock& lock_;
    };

    void lock();

    bool tryLock();

    void unlock();

   private:
    uint8_t locked_ = 0;
};

class Mutex {
   public:
    class Guard {
       public:
        explicit Guard(Mutex& mutex);
        ~Guard();
        Guard(const Guard&)            = delete;
        Guard& operator=(const Guard&) = delete;

       private:
        Mutex& mutex_;
    };
    Mutex();
    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;
    void lock();
    bool tryLock();
    void unlock();

   private:
    uint32_t locked_{};
};
class AddressWait {
   public:
    explicit AddressWait(volatile uint32_t& address);
    int wait(uint32_t expected);
    int wakeOne();
    int wakeAll();

   private:
    volatile uint32_t& address_;
};
}  // namespace os

#endif
