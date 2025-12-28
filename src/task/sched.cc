#include <cstdint>

#include "task.h"
#include "tty.h"
#include "io.h"
#include "cpu.h"
#include "kassert.h"
using namespace std;

namespace task {

void schedule() {
    if (task_queue_head == nullptr) {
        return;  // 任务队列为空，无法调度
    }
    if (task_queue_head == current_proc) {
        if (task_queue_tail->next) {	
		    Pcb *p = task_queue_tail;
		    while (p->next != task_queue_head) {
		        p = p->next;
		    }
		
		    task_queue_head->next = task_queue_tail;
		    task_queue_tail = task_queue_head;
		    
		    p->next = nullptr;
		    task_queue_head = p;
		} else	return;
    }
    // 切换任务
    Pcb *prev = current_proc;
    current_proc = task_queue_head;
    current_proc->stat = task::Running;

    Pcb *p = task_queue_tail;
    while (p->next != task_queue_head) {
        p = p->next;
    }

    task_queue_head->next = task_queue_tail;
    task_queue_tail = task_queue_head;
    
    p->next = nullptr;
    task_queue_head = p;

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
