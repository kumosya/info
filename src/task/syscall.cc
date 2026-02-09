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

extern "C" std::uint64_t SyscallMain(std::uint64_t arg1,
                                     std::uint64_t arg2, std::uint64_t arg3, std::uint64_t arg4, std::uint64_t arg5, std::uint64_t num) {
    KASSERT(rdmsr(MSR_GS_BASE) != 0);
    //tty::printk("Syscall: rax=%d\n", num);
    if (num == SYS_SEND) {
        task::ipc::Message ipc_msg;
        ipc_msg.dst_pid = arg1;
        ipc_msg.type    = arg2;
        //tty::printk("Syscall: Send to PID %d, type %d, arg3=0x%lx arg4=0x%lx ", ipc_msg.dst_pid, ipc_msg.type, arg3, arg4);
        //tty::printk("data: %c(%Xh)\n", reinterpret_cast<char *>(arg3)[0], reinterpret_cast<char *>(arg3)[0]);
        memcpy(ipc_msg.data, reinterpret_cast<void *>(arg3),
               sizeof(ipc_msg.data));
        int ret = task::ipc::Send(&ipc_msg);
        //tty::printk("Syscall: Send return %d\n", ret);
        return static_cast<std::uint64_t>(ret);
    } else if (num == SYS_RECEIVE) {
        task::ipc::Message ipc_msg;
        ipc_msg.type = arg2;
        //tty::printk("Syscall: type %d, arg1=0x%lx arg3=0x%lx\n", ipc_msg.type, arg1, arg3);
        int ret      = task::ipc::Receive(&ipc_msg);
        memcpy(reinterpret_cast<void *>(arg3), ipc_msg.data,
               sizeof(ipc_msg.data));
        if (arg1 && reinterpret_cast<pid_t *>(arg1) != nullptr) {
            *reinterpret_cast<pid_t *>(arg1) = ipc_msg.sender->pid;
        }
        return static_cast<std::uint64_t>(ret);
    }
    return -1;
}
