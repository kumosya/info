/**
 * @file mm/alloc.cc
 * @brief System service of memory management
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kernel/io.h"
#include "kernel/keyboard.h"
#include "kernel/mm.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace mm {

int Service(void *arg) {
    /*tty::printk(
        "task is running, argc: %d, argv[0]: %s, pcb addr: 0x%lx, pid: %d\n",
        argc, argv[0], (std::uint64_t)task::current_proc,
        task::current_proc->pid);*/

    task::current_proc->SetTTY(1);  // 绑定到第一个TTY
    task::Message msg;
    msg.dst_pid = SYS_CHAR;
    msg.type    = SYS_CHAR_PUTS;
    std::sprintf(msg.data, "[ %08d ] Memory management service started.\n",
                 timer::GetTicks());
    msg.Send();
    while (true) {
        bool need_reply = false;
        task::Message msg;
        if (msg.Recv()) {
            need_reply = true;
            // tty::printk("Service received message from PID %d, type %d\n", msg.sender->GetPid(), msg.type);
            std::uint32_t gs = 0;
            // tty::printk("GS: 0x%x\n", rdmsr(MSR_GS_BASE));
            switch (msg.type) {
                case SYS_MM_BRK: {
                    // brk(addr): if addr==0 return current break, else set break
                    void *addr = msg.sender->Brk(msg.num[0]);
                    if (addr == nullptr) {
                        msg.num[0] = 0; // failure
                    } else {
                        msg.num[0] = reinterpret_cast<std::uint64_t>(addr); // success
                    }
                    
                    break;
                }
                case SYS_MM_SBRK: {
                    // sbrk(increment): return previous break, expand if needed
                    void *addr = msg.sender->Sbrk(msg.num[0]);
                    if (addr == nullptr) {
                        msg.num[0] = 0; // failure
                    } else {
                        msg.num[0] = reinterpret_cast<std::uint64_t>(addr); // success
                    }
                    break;
                }
                case SYS_MM_MMAP: {
                    /* Not implemented: return failure (0) */
                    msg.num[0] = 0;
                    break;
                }
                case SYS_MM_MUNMAP: {
                    /* Not implemented: return failure (0) */
                    msg.num[0] = 0;
                    break;
                }
                default:
                    tty::printk("Unknown message type: %d\n", msg.type);
                    need_reply = false;
                    break;
            }
            if (need_reply) {
                msg.dst_pid = msg.sender->GetPid();
                msg.Send();
            }
        }
    }
    return 0;
}

}  // namespace 