#include <os/syscall.h>

namespace os {

int64_t invokeSyscall(SyscallNumber number, uint64_t arg1, uint64_t arg2,
                      uint64_t arg3, uint64_t arg4, uint64_t arg5,
                      uint64_t arg6) {
    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8 asm("r8")   = arg5;
    register uint64_t r9 asm("r9")   = arg6;
    uint64_t result;
    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(static_cast<uint64_t>(number)), "D"(arg1), "S"(arg2),
                   "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return static_cast<int64_t>(result);
}

int waitAddress(volatile uint32_t* address, uint32_t expected) {
    return static_cast<int>(invokeSyscall(SyscallNumber::WaitAddress,
                                          reinterpret_cast<uint64_t>(address),
                                          expected));
}

int wakeAddress(volatile uint32_t* address, uint32_t count) {
    return static_cast<int>(invokeSyscall(SyscallNumber::WakeAddress,
                                          reinterpret_cast<uint64_t>(address),
                                          count));
}

}  // namespace os
