/**
 * @file task/syscall.cc
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
#include "kernel/kassert.h"

std::uint64_t SysSend(std::uint64_t dst_pid, std::uint64_t type, MESSAGE *data) {
    task::Message ipc_msg;
    ipc_msg.dst_pid = dst_pid;
    ipc_msg.type    = type;
    //tty::printk("Syscall: Send to PID %d, type %d, arg3=0x%lx arg4=0x%lx ", ipc_msg.dst_pid, ipc_msg.type, arg3, arg4);
    //tty::printk("data: %c(%Xh)\n", reinterpret_cast<char *>(arg3)[0], reinterpret_cast<char *>(arg3)[0]);
    memcpy(ipc_msg.data, data,
            sizeof(ipc_msg.data));
    return ipc_msg.Send();
}

std::uint64_t SysRecv(std::uint64_t *dst_pid, std::uint64_t type, MESSAGE *data) {
    task::Message ipc_msg;
    ipc_msg.type = type;
    //tty::printk("Syscall: type %d, arg1=0x%lx arg3=0x%lx\n", ipc_msg.type, arg1, arg3);
    int ret      = ipc_msg.Recv();
    memcpy(data, ipc_msg.data,
            sizeof(ipc_msg.data));
    if (dst_pid && reinterpret_cast<pid_t *>(dst_pid) != nullptr) {
        *dst_pid = static_cast<std::uint64_t>(ipc_msg.sender->GetPid());
    }
    return ret;
}

std::uint64_t SysExecve(const char *filename, const char **argv, const char **envp) {
    return task::current_proc->Execve(filename, argv, envp);
}

extern "C" std::uint64_t SyscallMain(std::uint64_t arg1,
                                     std::uint64_t arg2, std::uint64_t arg3, std::uint64_t arg4, std::uint64_t arg5, std::uint64_t arg6, std::uint64_t num) {
    KASSERT(rdmsr(MSR_GS_BASE) != 0);
    //tty::printk("Syscall: rax=%d\n", num);
    if (num == SYS_SEND) {
        return SysSend(arg1, arg2, reinterpret_cast<MESSAGE *>(arg3));
    } else if (num == SYS_RECEIVE) {
        return SysRecv(reinterpret_cast<std::uint64_t *>(arg1), arg2, reinterpret_cast<MESSAGE *>(arg3));
    } else if (num == SYS_TASK_EXECVE) {
        return SysExecve(reinterpret_cast<const char *>(arg1), reinterpret_cast<const char **>(arg2), reinterpret_cast<const char **>(arg3));
    }
    return -1;
}
