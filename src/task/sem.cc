/**
 * @file sem.cc
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

Sem::~Sem() {}

void Sem::wait() {
    lock.lock();

    if (value > 0) {
        value--;
        lock.unlock();
        return;
    }

    Pcb *current  = current_proc;
    current->stat = Blocked;
    cfs::sched.Dequeue(current);

    Pcb *tail = wait_queue;
    /*if (tail == nullptr) {
        wait_queue = current;
        current->next = nullptr;
    } else {
        while (tail->next != nullptr) {
            tail = tail->next;
        }
        tail->next = current;
        current->next = nullptr;
    }*/

    lock.unlock();
    Schedule();
}

void Sem::signal() {
    lock.lock();

    /*if (wait_queue != nullptr) {
        Pcb *pcb = wait_queue;
        wait_queue = pcb->next;
        pcb->stat = Ready;
        cfs::Enqueue(pcb);
    } else {
        value++;
    }*/

    lock.unlock();
}

std::int32_t Sem::get_value() const { return value; }

}  // namespace task
