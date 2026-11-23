/*
 * 锁、等待和同步原语。
 */

#ifndef _OS_SYNC_H
#define _OS_SYNC_H

#include <cstdint>

namespace os {

    /* 使用原子状态忙等互斥，适合不能睡眠的短临界区。 */
    class SpinLock {
        public:
            /* 在作用域内持有对应资源或锁，退出作用域时按拥有关系归还。 */
            class Guard {
                public:
                    explicit Guard(SpinLock& lock);
                    ~Guard();

                    Guard(const Guard&) = delete;
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

    /* 互斥访问一份资源，竞争失败的线程进入睡眠队列。 */
    class Mutex {
        public:
            /* 在作用域内持有对应资源或锁，退出作用域时按拥有关系归还。 */
            class Guard {
                public:
                    explicit Guard(Mutex& mutex);
                    ~Guard();
                    Guard(const Guard&) = delete;
                    Guard& operator=(const Guard&) = delete;

                private:
                    Mutex& mutex_;
            };

            Mutex();
            Mutex(const Mutex&) = delete;
            Mutex& operator=(const Mutex&) = delete;
            void lock();
            bool tryLock();
            void unlock();

        private:
            uint32_t locked_{};
    };

    /* 把用户地址上的值检查与线程等待、唤醒组合起来。 */
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
