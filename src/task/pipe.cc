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

Pipe pipe_table[64];
std::int64_t pipe_count = 0;

std::int64_t PipeCreate(int pipefd[2]) {
    if (pipe_count >= 64) {
        return -1;
    }
    
    int id = pipe_count++;
    Pipe *pipe = &pipe_table[id];
    
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->buffer_size = 0;
    pipe->reader = nullptr;
    pipe->writer = nullptr;
    pipe->is_closed = false;
    pipe->readable = new Sem(0);
    pipe->writable = new Sem(4096);
    
    pipefd[0] = id * 2;
    pipefd[1] = id * 2 + 1;
    
    return 0;
}

std::int64_t PipeRead(int fd, void *buf, std::uint64_t size) {
    if (fd < 0 || fd >= 128) {
        return -1;
    }
    
    int pipe_id = fd / 2;
    if (pipe_id >= pipe_count) {
        return -1;
    }
    
    Pipe *pipe = &pipe_table[pipe_id];
    
    pipe->lock.lock();
    
    while (pipe->buffer_size == 0 && !pipe->is_closed) {
        pipe->lock.unlock();
        pipe->readable->wait();
        pipe->lock.lock();
    }
    
    if (pipe->buffer_size == 0) {
        pipe->lock.unlock();
        return 0;
    }
    
    std::uint64_t read_size = size < pipe->buffer_size ? size : pipe->buffer_size;
    
    for (std::uint64_t i = 0; i < read_size; i++) {
        *((char *)buf + i) = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % 4096;
    }
    
    pipe->buffer_size -= read_size;
    
    pipe->lock.unlock();
    
    pipe->writable->signal();
    
    return read_size;
}

std::int64_t PipeWrite(int fd, const void *buf, std::uint64_t size) {
    if (fd < 0 || fd >= 128) {
        return -1;
    }
    
    int pipe_id = fd / 2;
    if (pipe_id >= pipe_count) {
        return -1;
    }
    
    Pipe *pipe = &pipe_table[pipe_id];
    
    pipe->lock.lock();
    
    while (pipe->buffer_size == 4096 && !pipe->is_closed) {
        pipe->lock.unlock();
        pipe->writable->wait();
        pipe->lock.lock();
    }
    
    if (pipe->is_closed) {
        pipe->lock.unlock();
        return -1;
    }
    
    std::uint64_t write_size = size < (4096 - pipe->buffer_size) ? size : (4096 - pipe->buffer_size);
    
    for (std::uint64_t i = 0; i < write_size; i++) {
        pipe->buffer[pipe->write_pos] = *((const char *)buf + i);
        pipe->write_pos = (pipe->write_pos + 1) % 4096;
    }
    
    pipe->buffer_size += write_size;
    
    pipe->lock.unlock();
    
    pipe->readable->signal();
    
    return write_size;
}

void PipeClose(int fd) {
    if (fd < 0 || fd >= 128) {
        return;
    }
    
    int pipe_id = fd / 2;
    if (pipe_id >= pipe_count) {
        return;
    }
    
    Pipe *pipe = &pipe_table[pipe_id];
    
    pipe->lock.lock();
    
    pipe->is_closed = true;
    
    pipe->lock.unlock();
    
    pipe->readable->signal();
    pipe->writable->signal();
}

}  // namespace task::ipc
