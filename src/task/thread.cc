
#include "task.h"
#include "tty.h"
#include "mm.h"
#include "page.h"
#include "cpu.h"
#include "io.h"
#include "vfs.h"
#include "block.h"
#include <cstdint>
#include <cstring>
#include <stddef.h>

namespace task {
    
Pcb *current_proc = nullptr;
// 任务队列头指针
Pcb *task_queue_head = nullptr;

}

extern "C" std::int64_t do_exit(std::int64_t code) {
    return task::thread::Exit(code);
}

namespace task::thread {

pid_t pid_counter = 0;

std::int64_t Exec(task::pt_regs *regs) {
    /* TODO */
    
    return 0;
}

std::int64_t Exit(std::int64_t code) {
    tty::printf("Thread %d exit with code: 0x%lx\n", current_proc->pid, code);
    current_proc->exit_code = code;
    current_proc->stat = Dead;

    // 正确释放argv内存
    if (current_proc->argv != 0) {
        char **argv = reinterpret_cast<char **>(current_proc->argv);
        // 遍历并释放每个参数字符串
        for (std::uint64_t i = 0; argv[i] != nullptr; i++) {
            mm::page::Free(argv[i]);
        }
        // 释放argv数组本身
        mm::page::Free(argv);
    }
    
    // 将当前进程从任务队列中移除
    queue::Remove(current_proc);
    
    // 切换到下一个线程
    task::schedule();
    
    return 0;
}

static pid_t NewPid() {
    return pid_counter++;
}

pid_t Fork(task::pt_regs *regs, std::uint64_t flags, std::uint64_t stack_start, std::uint64_t stack_size) {
    // 分配新的进程控制块
    Pcb *child = reinterpret_cast<Pcb*>(mm::page::Alloc(sizeof(Pcb)));
    if (child == nullptr) {
        return -1;
    }
    
    std::memset(child, 0, sizeof(Pcb));
    
    // 如果current_proc为nullptr（创建第一个进程时），直接初始化
    if (current_proc != nullptr) {
        *child = *current_proc;
        child->parent = current_proc;
    } else {
        // 初始化第一个进程
        child->parent = nullptr;
    }
    
    child->pid = NewPid();
    child->stat = task::Blocked;
    child->flags = flags;
    
    // 创建线程控制块
    Tcb *thread = reinterpret_cast<Tcb*>(mm::page::Alloc(sizeof(Tcb)));
    if (thread == nullptr) {
        mm::page::Free(child);
        return -1;
    }
    
    std::memset(thread, 0, sizeof(Tcb));
    child->thread = thread;
    
    // 设置内核栈指针
    thread->rsp0 = reinterpret_cast<std::uint64_t>(child) + STACK_SIZE;
    
    // 如果有regs参数，复制寄存器状态到新进程的栈中
    if (regs != nullptr) {
        // 将寄存器状态复制到新进程的栈顶
        task::pt_regs *rsp = reinterpret_cast<task::pt_regs*>(thread->rsp0 - sizeof(task::pt_regs));
        std::memcpy(rsp, regs, sizeof(task::pt_regs));
        
        thread->rsp = reinterpret_cast<std::uint64_t>(rsp);
        thread->rip = regs->rip;
        child->argv = regs->rcx;
    } else {
        // 如果没有regs参数，使用默认的寄存器状态
        thread->rip = reinterpret_cast<std::uint64_t>(kernel_thread_entry);
        thread->rsp = thread->rsp0;
    }
    
    child->stat = task::Ready;
    
    // 将新创建的进程添加到任务队列
    queue::Add(child);
    
    return child->pid;
}

// 内核线程创建函数，使用pt_regs来设置线程上下文
pid_t KernelThread(std::int64_t *fn, char *argv[], std::uint64_t flags) {
    task::pt_regs regs;
    std::memset(&regs, 0, sizeof(regs));
    
    
    std::uint64_t argc = 0;
    char **new_argv = nullptr;
    
    // 计算argc并分配新的argv内存
    if (argv != nullptr) {
        while (argv[argc]) {
            argc++;
        }
        
        // 分配argv数组内存 (argc+1个指针)
        new_argv = (char **)mm::page::Alloc((argc + 1) * sizeof(char *));
        
        // 分配并复制每个参数字符串
        for (std::uint64_t i = 0; i < argc; i++) {
            if (argv[i] != nullptr) {
                size_t len = strlen(argv[i]);
                new_argv[i] = (char *)mm::page::Alloc(len + 1);  // +1 for null terminator
                strcpy(new_argv[i], argv[i]);
            } else {
                new_argv[i] = nullptr;
            }
        }
        new_argv[argc] = nullptr;  // 最后一个元素设置为null
    }
    
    // 设置线程函数指针和参数
    regs.rbx = reinterpret_cast<std::uint64_t>(fn);
    regs.rdx = argc;
    regs.rcx = reinterpret_cast<std::uint64_t>(new_argv);
    
    // 设置段选择器
    regs.ds = KERNEL_DS;
    regs.es = KERNEL_DS;
    regs.cs = KERNEL_CS;
    regs.ss = KERNEL_DS;
    
    // 设置EFLAGS，确保中断使能
    regs.rflags = (1 << 9);  // IF位
    
    // 设置线程入口点为kernel_thread_entry包装器
    regs.rip = reinterpret_cast<std::uint64_t>(kernel_thread_entry);
    
    pid_t pid = Fork(&regs, flags | THREAD_KERNEL, 0, 0);
    
    if (pid >= 0) {
        tty::printf("Kernel thread created. PID: %d\n", pid);
    }
    
    return pid;
}

void Init() {
    tty::printf("Thread init.\n"); 
    wrmsr(0x174, KERNEL_CS);
    // 初始化系统调用表
    
    // 分配新的进程控制块
    Pcb *idle = reinterpret_cast<Pcb*>(mm::page::Alloc(sizeof(Pcb)));
    if (!idle) {
        tty::Panic("Failed to allocate memory for idle process.\n");
    }
    
    std::memset(idle, 0, sizeof(Pcb));
    
    // 初始化第一个进程
    idle->parent = nullptr;
    
    idle->pid = pid_counter++;
    idle->stat = task::Blocked;
    
    // 创建线程控制块
    Tcb *thread = reinterpret_cast<Tcb*>(mm::page::Alloc(sizeof(Tcb)));
    if (thread == nullptr) {
        mm::page::Free(idle);
        tty::Panic("Failed to allocate memory for idle thread.\n");
    }
    
    std::memset(thread, 0, sizeof(Tcb));
    idle->thread = thread;
    
    // 设置内核栈指针
    thread->rsp0 = reinterpret_cast<std::uint64_t>(idle) + STACK_SIZE;
    // 设置idle线程的指令指针和栈指针
    thread->rip = reinterpret_cast<std::uint64_t>(kernel_thread_entry);
    thread->rsp = thread->rsp0;
    
    idle->stat = task::Ready;
    
    // 将新创建的进程添加到任务队列
    queue::Add(idle);
    
    current_proc = idle;
    tty::printf("Set first process (PID: %d) as current_proc\n", idle->pid);
    
    // 创建init线程
    char *init_argv[] = {"init", nullptr};
    KernelThread(reinterpret_cast<std::int64_t *>(SysInit), init_argv, 0);
    
    // 调用schedule函数进行第一次任务调度
    task::schedule();
}

}  // namespace task::thread