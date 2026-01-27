/**
 * @file sys.cc
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

int Service(int argc, char *argv[]) {
    while (true) {
        task::ipc::Message msg;

        if (ipc::Receive(&msg)) {
            switch (msg.type) {
                case SYS_TASK_EXIT:
                    thread::Kill(msg.sender, msg.num[0]);
                    break;
                case SYS_TASK_KILL:
                    thread::Kill(reinterpret_cast<Pcb *>(msg.num[0]),
                                 msg.num[1]);
                    break;
                case SYS_TASK_EXECVE:
                    tty::printk("Execve request from PID %d: %x %x %s\n",
                                msg.sender->pid, msg.num[0], msg.num[1],
                                reinterpret_cast<char *>(msg.num[0]));
                    thread::Execve(reinterpret_cast<const char *>(msg.num[0]),
                                   const_cast<const char **>(
                                       reinterpret_cast<char **>(msg.num[1])),
                                   const_cast<const char **>(
                                       reinterpret_cast<char **>(msg.num[2])));
                    break;
                default:
                    tty::printk("Unknown message type: %d\n", msg.type);
                    break;
            }
        }
    }
    return 0;
}

}  // namespace task