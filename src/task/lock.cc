#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"


namespace task {

SpinLock::SpinLock() : state(0) {}

SpinLock::~SpinLock() {}

void SpinLock::lock() {
    while (true) {
        std::uint32_t expected = 0;
        std::uint32_t new_value = 1;
        
        __asm__ __volatile__(
            "lock xchgl %1, %0\n\t"
            : "=m"(state), "=r"(new_value)
            : "m"(state), "r"(new_value)
            : "memory", "cc");
        
        if (expected == 0) {
            break;
        }
        
        __asm__ __volatile__("pause");
    }
}

void SpinLock::unlock() {
    __asm__ __volatile__(
        "movl $0, %0\n\t"
        : "=m"(state)
        : "m"(state)
        : "memory");
}

bool SpinLock::try_lock() {
    std::uint32_t expected = 0;
    std::uint32_t new_value = 1;
    
    __asm__ __volatile__(
        "lock xchgl %1, %0\n\t"
        : "=m"(state), "=r"(new_value)
        : "m"(state), "r"(new_value)
        : "memory", "cc");
    
    return (expected == 0);
}

}