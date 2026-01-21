
#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"

extern "C" std::int64_t do_exit(std::int64_t code) {
    return task::thread::Exit(code);
}

namespace task::thread {
std::int64_t Exit(std::int64_t code) {
    Kill(current_proc, code);

    // 切换到下一个线程（Schedule 中会 Dequeue）
    Schedule();

    return 0;
}

std::int64_t Kill(Pcb *proc, std::int64_t code) {
    tty::printk("Thread %d exit with code: 0x%lx\n", proc->pid, code);
    proc->exit_code = code;
    proc->stat      = Dead;

    // 释放argv内存
    if (proc->argv != 0) {
        char **argv = reinterpret_cast<char **>(proc->argv);
        // 遍历并释放每个参数字符串
        for (std::uint64_t i = 0; argv[i] != nullptr; i++) {
            mm::page::Free(argv[i]);
        }
        // 释放argv数组本身
        mm::page::Free(argv);
    }

    // 释放用户态页表
    if (!(proc->flags & THREAD_KERNEL)) {
        mm::page::Free(proc->mm.pml4);
    }

    return 0;
}

}  // namespace task::thread