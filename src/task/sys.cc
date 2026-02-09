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

int Service(int argc, char *argv[]) {
    while (true) {
        task::ipc::Message msg;
        if (ipc::Receive(&msg)) {
            //tty::printk("Service received message from PID %d, type %d\n", msg.sender->pid, msg.type);
            switch (msg.type) {
                case SYS_TASK_EXIT:
                    thread::Kill(msg.sender, msg.num[0]);
                    break;
                case SYS_TASK_KILL:

                    // TODO：用pid找到对应的Pcb，再调用Kill
                    // 运行队列是一个二叉树，通过遍历树找pcb效率太低
                    // 还需要支持一个全局的pid到pcb的映射表，来快速找到目标pcb

                    thread::Kill(reinterpret_cast<Pcb *>(msg.num[0]),
                                 msg.num[1]);
                    break;
                case SYS_TASK_EXECVE:
                    tty::printk("Execve request from PID %d: %x %x %s\n",
                                msg.sender->pid, msg.num[0], msg.num[1],
                                reinterpret_cast<char *>(msg.num[0]));

                                // TODO：argv、envp的内存映射
                                // 从用户态传递过来的指针只存在于用户态地址空间中，
                                // 需要先将其转化为物理地址，再映射到内核态地址空间中，才能访问
                                // 而目前Vir2Phy只支持0xfffff000`00000000以上那一段地址的转换，还需要进一步改进

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