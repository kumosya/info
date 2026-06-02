/**
 * @file task/ipc.cc
 * @brief Inter-Process Communication
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

struct MessageQueue {
    Message messages[16];
    std::uint64_t head;
    std::uint64_t tail;
    SpinLock lock;
    Task *waiting_sender;
    Task *waiting_receiver;
    bool has_message;
};

MessageQueue msg_queues[256];
SpinLock msg_lock;

int Message::Send() {
    sender = current_proc;

    // validate destination before indexing into the array
    if (dst_pid < 0 || dst_pid >= 256 || current_proc->IsWaiting()) {
        return -1;
    }

    MessageQueue *queue = &msg_queues[dst_pid];
    queue->lock.lock();

    // If a receiver is already waiting, deliver immediately and wake it.
    if (queue->waiting_receiver != nullptr) {
        memcpy(reinterpret_cast<void *>(queue->waiting_receiver->GetMessage()), this,
               sizeof(Message));
        Task *receiver           = queue->waiting_receiver;
        queue->waiting_receiver = nullptr;
        queue->lock.unlock();
        receiver->Unblock();
        return 1;
    }

    // Rendezvous semantics: block the sender until a receiver consumes the
    // message.
    // tty::printk("Send: pid=%d blocking for dst=%d msg=%p\n",
    // current_proc->pid, (int)dst_pid, msg);
    current_proc->SetMessage(this);
    queue->waiting_sender = current_proc;
    current_proc->Block();
    queue->lock.unlock();
    // tty::printk("Send: pid=%d woke up\n", current_proc->pid);
    Schedule();

    return 1;
}

int Message::Recv() {
    task::Task *current = current_proc;

    MessageQueue *queue = &msg_queues[current->GetPid()];

    queue->lock.lock();

    if (queue->head != queue->tail) {
        memcpy(this, &queue->messages[queue->head], sizeof(Message));
        sender = queue->messages[queue->head].sender;
        queue->head = (queue->head + 1) % 16;

        if (queue->head == queue->tail) {
            queue->has_message = false;
        }

        queue->lock.unlock();
    } else if (queue->waiting_sender != nullptr) {
        // copy message directly from sender's buffer
        memcpy(this, reinterpret_cast<void *>(queue->waiting_sender->GetMessage()),
               sizeof(Message));
        sender           = queue->waiting_sender;
        Task *sender           = queue->waiting_sender;
        queue->waiting_sender = nullptr;
        queue->lock.unlock();
        // tty::printk("Receive: pid=%d waking sender=%d\n", current->pid,
        // sender->pid);
        sender->Unblock();
    } else {
        current_proc->SetMessage(this);

        queue->waiting_receiver = current_proc;
        current_proc->Block();
        queue->lock.unlock();

        Schedule();
    }
    return 1;
}

}  // namespace task::ipc
