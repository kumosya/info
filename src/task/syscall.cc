/**
 * @file syscall.cc
 * @brief System call handler
 * @author Kumosya, 2025-2026
 **/
#include "kernel/syscall.h"

#include <cstdint>
#include <cstring>
#include <stddef.h>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"

extern "C" std::uint64_t SyscallMain(task::Registers *regs) {
    if (regs->rax == SYS_SEND) {
        task::ipc::Message ipc_msg;
        ipc_msg.dst_pid = regs->rdi;
        ipc_msg.type    = regs->rsi;
        memcpy(ipc_msg.data, reinterpret_cast<void *>(regs->r8),
               sizeof(ipc_msg.data));
        int ret = task::ipc::Send(&ipc_msg);
        return static_cast<std::uint64_t>(ret);
    } else if (regs->rax == SYS_RECEIVE) {
        task::ipc::Message ipc_msg;
        ipc_msg.type = regs->rdi;
        int ret      = task::ipc::Receive(&ipc_msg);
        memcpy(reinterpret_cast<void *>(regs->r8), ipc_msg.data,
               sizeof(ipc_msg.data));
        if (regs->rdi && reinterpret_cast<pid_t *>(regs->rdi) != nullptr) {
            *reinterpret_cast<pid_t *>(regs->rdi) = ipc_msg.sender->pid;
        }
        return static_cast<std::uint64_t>(ret);
    }
    return -1;
}
