#include <cstdint>

#include "task.h"
#include "tty.h"
#include "mm.h"
using namespace std;

namespace task::queue {

// 将任务添加到队列末尾
void Add(Pcb *pcb) {
    if (pcb == nullptr) return;
    
    // 初始化链表指针
    pcb->prev = nullptr;
    pcb->next = nullptr;
    
    if (task_queue_head == nullptr) {
        // 队列为空，直接作为头节点
        task_queue_head = pcb;
        task_queue_head->next = task_queue_head;
        task_queue_head->prev = task_queue_head;
    } else {
        // 添加到队列末尾（头节点的前一个位置）
        Pcb *tail = task_queue_head->prev;
        tail->next = pcb;
        pcb->prev = tail;
        pcb->next = task_queue_head;
        task_queue_head->prev = pcb;
    }
}

// 将任务从队列中移除
void Remove(Pcb *pcb) {
    if (pcb == nullptr || task_queue_head == nullptr) return;
    
    // 如果是唯一节点
    if (pcb->next == pcb && pcb->prev == pcb) {
        task_queue_head = nullptr;
    } else {
        // 调整链表指针
        pcb->prev->next = pcb->next;
        pcb->next->prev = pcb->prev;
        
        // 如果移除的是头节点，更新头指针
        if (pcb == task_queue_head) {
            task_queue_head = pcb->next;
        }
    }
    mm::page::Free(pcb);
}


}  // namespace task::proc