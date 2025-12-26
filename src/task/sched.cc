#include <cstdint>

#include "task.h"
#include "tty.h"
#include "io.h"
#include "cpu.h"
using namespace std;

namespace task {

extern Pcb *current_proc;
extern Pcb *task_queue_head;

// 简单的轮询调度算法
void schedule() {
    if (task_queue_head == nullptr) {
        return;  // 任务队列为空，无法调度
    }

    Pcb *next_proc = nullptr;
    Pcb *current = current_proc;
    
    // 如果当前任务存在且状态为Ready，将其标记为Running
    if (current != nullptr && current->stat == task::Ready) {
        current->stat = task::Running;
    }
    
    // 遍历任务队列，找到下一个Ready状态的任务
    Pcb *start = (current && current->stat == task::Ready) ? current : task_queue_head;
    Pcb *end = start;
    
    do {
        //tty::printf("PID: %d pcb addr: 0x%lx, Next: 0x%lx\n", start->pid, reinterpret_cast<uintptr_t>(start), reinterpret_cast<uintptr_t>(start->next));
        start = start->next;
        if (start->stat == task::Ready) {
            next_proc = start;
            break;
        }
    } while (start != end);
    
    // 如果没有找到Ready状态的任务，使用init线程
    if (next_proc == nullptr) {
        Pcb *init = task_queue_head;
        do {
            if (init->pid == 1) {  // init线程的PID为1
                next_proc = init;
                break;
            }
            init = init->next;
        } while (init != task_queue_head);
        
        if (next_proc == nullptr) {
            return;  // 没有找到init线程，无法调度
        }
    }
    
    // 如果找到的任务是当前任务，不需要切换
    if (next_proc == current_proc) {
        return;
    }
    
    // 更新当前任务状态
    if (current_proc != nullptr && current_proc->stat == task::Running) {
        current_proc->stat = task::Ready;
    }
    
    // 切换任务
    Pcb *prev = current_proc;
    current_proc = next_proc;
    current_proc->stat = task::Running;
    
    // 更新TSS中的内核栈指针
    gdt::tss.rsp0 = current_proc->thread->rsp0;
    //tty::printf("Switch from process (PID: %d) to process (PID: %d)\n", prev->pid, current_proc->pid);
    if (prev != nullptr) {
        if (!(prev->flags & THREAD_KERNEL) && !(current_proc->flags & THREAD_KERNEL)) {
            SwitchTable(prev, current_proc);
        }
        SwitchContext(prev, current_proc);
    }
}

}  // namespace task

extern "C" void __switch_to(task::Pcb *prev, task::Pcb *next) {
    gdt::tss.rsp0 = next->thread->rsp0;

    __asm__ __volatile__("movw	%%fs,	%0 \n\t" : "=a"(prev->thread->fs));
    __asm__ __volatile__("movw	%%gs,	%0 \n\t" : "=a"(prev->thread->gs));

    __asm__ __volatile__("movw	%0,	%%fs \n\t" ::"a"(next->thread->fs));
    __asm__ __volatile__("movw	%0,	%%gs \n\t" ::"a"(next->thread->gs));
    
    //wrmsr(0x175, next->thread->rsp0);
}
