#include <cstdint>

#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/mm.h"

namespace task::queue {
// 将任务添加到队列末尾
void Add(Pcb *pcb) {
    if (pcb == nullptr) return;
    
    // 初始化链表指针
    pcb->next = nullptr;
    
    if (task_queue_head == nullptr) {
        // 队列为空，直接作为头节点
        task_queue_head = pcb;
        task_queue_tail = pcb;
    } else {
        // 添加到队列末尾
        pcb->next = task_queue_tail;
        task_queue_tail = pcb;
    }
}

// 将任务从队列中移除
void Remove(Pcb *pcb) {
    if (pcb == nullptr || task_queue_head == nullptr) return;
    
    // 如果是唯一节点
    if (pcb->next == pcb) {
        task_queue_head = nullptr;
        task_queue_tail = nullptr;
    } else {
        // 如果移除的是尾节点，更新尾指针
        if (pcb == task_queue_tail) {
            task_queue_tail = pcb->next;
        } else {
            // 调整链表指针
            Pcb *p = task_queue_tail;
            while (p->next != pcb) {
                p = p->next;
            }
            // 移除节点
            p->next = pcb->next;
            if (pcb == task_queue_head) {
                task_queue_head = p;
            }
        }
        pcb->next = nullptr;
    }
    
    mm::page::Free(pcb);
    pcb = nullptr;
}


}  // namespace task::proc