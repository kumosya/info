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

int Service(int argc, char *argv[]) {
    /*tty::printk(
        "mm task is running, argc: %d, argv[0]: %s, pcb addr: 0x%lx, pid: %d\n",
        argc, argv[0], (std::uint64_t)task::current_proc,
        task::current_proc->pid);*/

    task::current_proc->tty = 1;  // 绑定到第一个TTY
    task::ipc::Message msg;
    msg.dst_pid = SYS_CHAR;
    msg.type    = SYS_CHAR_PUTS;
    std::sprintf(msg.data, "[ %08d ] Memory management service started.\n",
                 timer::GetTicks());
    task::ipc::Send(&msg);

    while (true) {
    }
    return 0;
}

}  // namespace mm
