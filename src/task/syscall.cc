

#include <stddef.h>

#include <cstdint>
#include <cstring>

#include "block.h"
#include "cpu.h"
#include "io.h"
#include "mm.h"
#include "page.h"
#include "task.h"
#include "tty.h"
#include "vfs.h"
#include "syscall.h"

extern "C" std::uint64_t SyscallMain(task::pt_regs *regs) {
    tty::printf("num:%d arg1:%x arg2:%x arg3:%x arg4:%x arg5:%x\n",
                regs->rax, regs->rdi, regs->rsi, regs->r8, regs->r9, regs->r10);
    if (regs->rax == SYS_EXIT_NO) {
        int ret = task::thread::Exit(regs->rdi);
        return ret;
    }
    // while (true);
    return 1;
}
