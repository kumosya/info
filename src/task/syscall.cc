
#include <cstdint>
#include <cstring>
#include <stddef.h>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/syscall.h"

extern "C" std::uint64_t SyscallMain(task::Registers *regs) {
    tty::printf("num:%d arg1:%x arg2:%x arg3:%x arg4:%x arg5:%x\n", regs->rax,
                regs->rdi, regs->rsi, regs->r8, regs->r9, regs->r10);
    if (regs->rax == SYS_EXIT_NO) {
        int ret = task::thread::Exit(regs->rdi);
        return ret;
    }
    // while (true);
    return 1;
}
