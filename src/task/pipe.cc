/**
 * @file task/pipe.cc
 * @brief Pipe implementation
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

static std::int64_t fd_counter = 1000;
static SpinLock fd_lock;

static int AllocFd() {
    fd_lock.lock();
    int fd = fd_counter++;
    fd_lock.unlock();
    return fd;
}

PipeManager *pipe_manager = nullptr;

void InitPipeManager() {
    pipe_manager = new PipeManager();
}

int Pipe::Create() {
    read_fd = AllocFd();
    write_fd = AllocFd();
    read_open = true;
    write_open = true;
    return 0;
}

std::int64_t Pipe::Read(void *buf, std::uint64_t size) {
    if (!read_open) {
        return -1;
    }

    lock.lock();

    while (buffer_size == 0 && write_open) {
        lock.unlock();
        readable->wait();
        lock.lock();
    }

    if (buffer_size == 0) {
        lock.unlock();
        return 0;
    }

    std::uint64_t read_size = size < buffer_size ? size : buffer_size;

    for (std::uint64_t i = 0; i < read_size; i++) {
        *((char *)buf + i) = buffer[read_pos];
        read_pos = (read_pos + 1) % 4096;
    }

    buffer_size -= read_size;

    lock.unlock();

    writable->signal();

    return read_size;
}

std::int64_t Pipe::Write(const void *buf, std::uint64_t size) {
    if (!write_open) {
        return -1;
    }

    lock.lock();

    while (buffer_size == PIPE_BUF_SIZE && read_open) {
        lock.unlock();
        writable->wait();
        lock.lock();
    }

    if (!read_open) {
        lock.unlock();
        return -1;
    }

    std::uint64_t write_size = size < (PIPE_BUF_SIZE - buffer_size) ? size : (PIPE_BUF_SIZE - buffer_size);

    for (std::uint64_t i = 0; i < write_size; i++) {
        buffer[write_pos] = *((const char *)buf + i);
        write_pos = (write_pos + 1) % PIPE_BUF_SIZE;
    }

    buffer_size += write_size;

    lock.unlock();

    readable->signal();

    return write_size;
}

void Pipe::CloseRead() {
    lock.lock();
    read_open = false;
    lock.unlock();
    writable->signal();
}

void Pipe::CloseWrite() {
    lock.lock();
    write_open = false;
    lock.unlock();
    readable->signal();
}

PipeManager::~PipeManager() {
    lock.lock();
    Pipe *current = head;
    while (current) {
        Pipe *next = current->next;
        delete current;
        current = next;
    }
    head = nullptr;
    count = 0;
    lock.unlock();
}

Pipe *PipeManager::CreatePipe() {
    lock.lock();

    Pipe *pipe = new Pipe();
    if (!pipe) {
        lock.unlock();
        return nullptr;
    }

    pipe->Create();

    pipe->next = head;
    head = pipe;
    count++;

    lock.unlock();

    return pipe;
}

void PipeManager::DestroyPipe(Pipe *pipe) {
    if (!pipe) {
        return;
    }

    lock.lock();

    Pipe *current = head;
    Pipe *prev = nullptr;

    while (current) {
        if (current == pipe) {
            if (prev) {
                prev->next = current->next;
            } else {
                head = current->next;
            }
            count--;
            delete current;
            break;
        }
        prev = current;
        current = current->next;
    }

    lock.unlock();
}

Pipe *PipeManager::FindByReadFd(int fd) {
    lock.lock();

    Pipe *current = head;
    while (current) {
        if (current->GetReadFd() == fd) {
            lock.unlock();
            return current;
        }
        current = current->next;
    }

    lock.unlock();
    return nullptr;
}

Pipe *PipeManager::FindByWriteFd(int fd) {
    lock.lock();

    Pipe *current = head;
    while (current) {
        if (current->GetWriteFd() == fd) {
            lock.unlock();
            return current;
        }
        current = current->next;
    }

    lock.unlock();
    return nullptr;
}

}  // namespace task
