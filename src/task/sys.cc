/**
 * @file task/sys.cc
 * @brief API handler
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstring>

#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

int Service(void *arg) {
    bool reply;
    uint64_t exit_code;
    while (true) {
        task::Message msg;
        reply = true;
        if (msg.Recv()) {
            Task *task;
            tty::printk("Service received message from PID %d, type %d\n", msg.sender->GetPid(), msg.type);
            switch (msg.type) {
                case SYS_TASK_CLONE:
                    tty::printk("msg.sender %d\n", msg.sender->GetPid());
                    task = msg.sender->Clone(reinterpret_cast<int (*)(void *)>(msg.num[0]), reinterpret_cast<void *>(msg.num[1]), 
                        static_cast<int>(msg.num[2]), reinterpret_cast<void *>(msg.num[3]), 0);
                    msg.num[0] = task->GetPid();
                    break;
                case SYS_TASK_EXIT:
                    exit_code = msg.num[0];
                    msg.sender->Exit(exit_code);
                    reply = false;
                    break;
                case SYS_TASK_KILL:
                    msg.sender->Kill(task_table.Find(static_cast<pid_t>(msg.num[0])),
                                 msg.num[1]);
                    break;
                
                default:
                    tty::printk("Unknown message type: %d\n", msg.type);
                    break;
            }
            if (reply) {
                msg.dst_pid = msg.sender->GetPid();
                msg.Send();
            }
        }
    }
    return 0;
}

}  // namespace task
