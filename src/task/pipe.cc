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

// TODO: 完善管道

namespace task::ipc {

Pipe pipe_table[64];
std::int64_t pipe_count = 0;

Pipe::Pipe() {
    if (pipe_count >= 64) {
        pipefd[0] = -1;
        pipefd[1] = -1;
        return;
    }

    int id     = pipe_count++;
    Pipe *pipe = &pipe_table[id];

    read_pos    = 0;
    write_pos   = 0;
    buffer_size = 0;
    reader      = nullptr;
    writer      = nullptr;
    is_closed   = false;
    readable    = new Sem(0);
    writable    = new Sem(4096);

    pipefd[0] = id * 2;
    pipefd[1] = id * 2 + 1;
}

std::int64_t Pipe::Read(int fd, void *buf, std::uint64_t size) {
    if (fd < 0 || fd >= 128) {
        return -1;
    }

    int pipe_id = fd / 2;
    if (pipe_id >= pipe_count) {
        return -1;
    }

    Pipe *pipe = &pipe_table[pipe_id];

    lock.lock();

    while (buffer_size == 0 && !is_closed) {
        lock.unlock();
        readable->wait();
        lock.lock();
    }

    if (buffer_size == 0) {
        lock.unlock();
        return 0;
    }

    std::uint64_t read_size =
        size < buffer_size ? size : buffer_size;

    for (std::uint64_t i = 0; i < read_size; i++) {
        *((char *)buf + i) = buffer[read_pos];
        read_pos     = (read_pos + 1) % 4096;
    }

    buffer_size -= read_size;

    lock.unlock();

    writable->signal();

    return read_size;
}

std::int64_t Pipe::Write(int fd, const void *buf, std::uint64_t size) {
    if (fd < 0 || fd >= 128) {
        return -1;
    }

    int pipe_id = fd / 2;
    if (pipe_id >= pipe_count) {
        return -1;
    }

    Pipe *pipe = &pipe_table[pipe_id];

    lock.lock();

    while (buffer_size == 4096 && !is_closed) {
        lock.unlock();
        writable->wait();
        lock.lock();
    }

    if (is_closed) {
        lock.unlock();
        return -1;
    }

    std::uint64_t write_size =
        size < (4096 - buffer_size) ? size : (4096 - buffer_size);

    for (std::uint64_t i = 0; i < write_size; i++) {
        buffer[write_pos] = *((const char *)buf + i);
        write_pos               = (write_pos + 1) % 4096;
    }

    buffer_size += write_size;

    lock.unlock();

    readable->signal();

    return write_size;
}

Pipe::~Pipe() {
    if (readable) {
        delete readable;
    }

    if (writable) {
        delete writable;
    }

    lock.lock();

    is_closed = true;

    lock.unlock();

    readable->signal();
    writable->signal();
}

}  // namespace task::ipc
