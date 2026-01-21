#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task::ipc {

struct MessageQueue {
    Message messages[16];
    std::uint64_t head;
    std::uint64_t tail;
    SpinLock lock;
    Pcb *waiting_sender;
    Pcb *waiting_receiver;
    bool has_message;
};

MessageQueue msg_queues[256];
SpinLock msg_lock;

std::int64_t Send(Message *msg) {
    msg->sender = current_proc;

    // validate destination before indexing into the array
    if (msg->dst_pid < 0 || msg->dst_pid >= 256) {
        return -1;
    }

    MessageQueue *queue = &msg_queues[msg->dst_pid];
    queue->lock.lock();
    
    // If a receiver is already waiting, deliver immediately and wake it.
    if (queue->waiting_receiver != nullptr) {
        memcpy(reinterpret_cast<void *>(queue->waiting_receiver->msg), msg, sizeof(Message));
        Pcb *receiver = queue->waiting_receiver;
        queue->waiting_receiver = nullptr;
        queue->lock.unlock();
        receiver->stat = task::Ready;
        cfs::Enqueue(receiver);
        return 0;
    }

    // Rendezvous semantics: block the sender until a receiver consumes the message.
    //tty::printk("Send: pid=%d blocking for dst=%d msg=%p\n", current_proc->pid, (int)msg->dst_pid, msg);
    current_proc->msg = msg;
    queue->waiting_sender = current_proc;
    current_proc->stat = task::Blocked;
    queue->lock.unlock();
    Schedule();
    //tty::printk("Send: pid=%d woke up\n", current_proc->pid);
    
    return 0;
}

bool Receive(Message *msg) {
    task::Pcb *current = current_proc;
    
    MessageQueue *queue = &msg_queues[current->pid];
    
    queue->lock.lock();
    
    if (queue->head != queue->tail) {
        memcpy(msg, &queue->messages[queue->head], sizeof(Message));
        msg->sender = queue->messages[queue->head].sender;
        queue->head = (queue->head + 1) % 16;
        
        if (queue->head == queue->tail) {
            queue->has_message = false;
        }
        
        queue->lock.unlock();
    } else if (queue->waiting_sender != nullptr) {
        // copy message directly from sender's buffer
        //tty::printk("Receive: pid=%d consuming sender=%d msg=%p\n", current->pid, queue->waiting_sender->pid, queue->waiting_sender->msg);
        memcpy(msg, reinterpret_cast<void *>(queue->waiting_sender->msg), sizeof(Message));
        msg->sender = queue->waiting_sender;
        Pcb *sender = queue->waiting_sender;
        queue->waiting_sender = nullptr;
        queue->lock.unlock();
        //tty::printk("Receive: pid=%d waking sender=%d\n", current->pid, sender->pid);
        sender->stat = task::Ready;
        cfs::Enqueue(sender);
    } else {
        current_proc->msg = msg;
        current_proc->stat = task::Blocked;
        
        queue->waiting_receiver = current_proc;
        queue->lock.unlock();

        Schedule();
    }
    return true;
}

}  // namespace ipc
