/**
 * @file task/sem.cc
 * @brief Semaphore implementation
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

Sem::Sem(std::int32_t value) : value(value), wait_queue(nullptr) {}

Sem::~Sem() {
    while (wait_queue != nullptr) {
        Task *task = wait_queue;
        wait_queue = reinterpret_cast<Task*>(task->GetMessage());
        task->SetMessage(nullptr);
        task->Unblock();
    }
}

void Sem::wait() {
    lock.lock();

    if (value > 0) {
        value--;
        lock.unlock();
        return;
    }

    current_proc->SetMessage(reinterpret_cast<Message*>(wait_queue));
    wait_queue = current_proc;
    current_proc->Block();
    
    lock.unlock();
    Schedule();
}

void Sem::signal() {
    lock.lock();

    if (wait_queue != nullptr) {
        Task *task = wait_queue;
        wait_queue = reinterpret_cast<Task*>(task->GetMessage());
        task->SetMessage(nullptr);
        task->Unblock();
        lock.unlock();
        return;
    }

    value++;
    lock.unlock();
}

std::int32_t Sem::get_value() const { return value; }

}  // namespace task
