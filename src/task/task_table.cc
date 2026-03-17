/**
 * @file task/task_table.cc
 * @brief Task hash table for pid -> task mapping
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>

#include "kernel/mm.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

TaskTable task_table;

void TaskTable::Insert(Task *task) {
    if (task == nullptr) return;
    pid_t pid = task->GetPid();
    std::uint64_t idx = Hash(pid);
    
    lock.lock();
    
    Node *node = (Node *)mm::page::Alloc(sizeof(Node));
    if (node == nullptr) {
        lock.unlock();
        return;
    }
    node->task = task;
    node->next = buckets[idx];
    buckets[idx] = node;
    
    lock.unlock();
}

void TaskTable::Remove(pid_t pid) {
    std::uint64_t idx = Hash(pid);
    
    lock.lock();
    
    Node *prev = nullptr;
    Node *curr = buckets[idx];
    
    while (curr != nullptr) {
        if (curr->task && curr->task->GetPid() == pid) {
            if (prev == nullptr) {
                buckets[idx] = curr->next;
            } else {
                prev->next = curr->next;
            }
            mm::page::Free(curr);
            lock.unlock();
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    
    lock.unlock();
}

Task *TaskTable::Find(pid_t pid) {
    std::uint64_t idx = Hash(pid);
    
    lock.lock();
    
    Node *curr = buckets[idx];
    while (curr != nullptr) {
        if (curr->task && curr->task->GetPid() == pid) {
            Task *result = curr->task;
            lock.unlock();
            return result;
        }
        curr = curr->next;
    }
    
    lock.unlock();
    return nullptr;
}

}  // namespace task
