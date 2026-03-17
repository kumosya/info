/**
 * @file task/thread.cc
 * @brief Thread management functions
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/multiboot2.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

namespace task {

Task *current_proc = nullptr;
Task *idle         = nullptr;

void Task::Block() {
    stat = task::Blocked;
}

void Task::Unblock() {
    stat = task::Ready;
    Enqueue();
}

namespace thread {

void Init() {
    pid_counter = 0;
    curr_sched = Sched();  // 初始化调度器

    idle = new Task(true);
    idle = idle->Clone(nullptr, nullptr, 0, nullptr, IDLE_NICE);

    if (idle == nullptr) {
        tty::Panic("Failed to create idle process.\n");
    }
    
    // 初始化系统调用
    wrmsr(MSR_EFER,
         rdmsr(MSR_EFER) | (1 << 0));  // 启用syscall扩展
    wrmsr(MSR_STAR,
         (static_cast<std::uint64_t>(KERNEL_CS) << 32) | 
         (static_cast<std::uint64_t>(USER_CS - 0x10) << 48)); 
    wrmsr(MSR_LSTAR, reinterpret_cast<std::uint64_t>(enter_syscall));
    wrmsr(MSR_FMASK, 0x200); 
    
    current_proc = idle;
    // 创建线程
    Task *init = idle->Clone(SysInit, nullptr, 0, nullptr, 0);
    
    init->Clone(block::Service, nullptr, 0, nullptr, 3);
    init->Clone(vfs::Service, nullptr, 0, nullptr, 5);
    init->Clone(tty::Service, nullptr, 0, nullptr, 0);
    init->Clone(mm::Service, nullptr, 0, nullptr, -2);
    init->Clone(Service, nullptr, 0, nullptr, -2);

    sti();
    // 进行第一次任务调度
    Schedule();
}
}  // namespace thread
}  // namespace task
