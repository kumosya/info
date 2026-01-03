
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
    tty::printf("Thread %d exit with code: 0x%lx\n", current_proc->pid, code);
    current_proc->exit_code = code;
    current_proc->stat      = Dead;

    // 释放argv内存
    if (current_proc->argv != 0) {
        char **argv = reinterpret_cast<char **>(current_proc->argv);
        // 遍历并释放每个参数字符串
        for (std::uint64_t i = 0; argv[i] != nullptr; i++) {
            mm::page::Free(argv[i]);
        }
        // 释放argv数组本身
        mm::page::Free(argv);
    }

    if (!(current_proc->stat & THREAD_KERNEL)) {
        // TODO
        // 在这之前其实要把用户层的各级页表umap的
        mm::page::Free(current_proc->mm.pml4);
    }

    // 将当前进程从任务队列中移除
    queue::Remove(current_proc);

    // 切换到下一个线程
    task::schedule();

    return 0;
}

}  // namespace task::thread