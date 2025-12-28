#include "task.h"
#include "tty.h"
#include "mm.h"
#include "page.h"
#include "cpu.h"
#include <cstdint>
#include <cstring>
#include <stddef.h>

namespace task::thread {

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

}   // namespace task::thread