
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
    if (regs->rax == SYS_SEND_NO) {
        task::ipc::Message *msg = (task::ipc::Message *)regs->rdi;
        std::int64_t ret = task::ipc::Send(msg);
        return ret;
    } else if (regs->rax == SYS_RECEIVE_NO) {
        task::ipc::Message *msg = (task::ipc::Message *)regs->rdi;
        bool ret = task::ipc::Receive(msg);
        return static_cast<std::uint64_t>(ret);
    }
    return -1;
}
