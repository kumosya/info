
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "kernel/block.h"
#include "kernel/cpu.h"
#include "kernel/elf.h"
#include "kernel/io.h"
#include "kernel/keyboard.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

extern char *cmdline;

int a1(void *arg) {
    tty::printk("hello");
    task::Pipe *pipe = (task::Pipe *)arg;
    char buf[256];
    pipe->Read(buf, sizeof(buf));
    tty::printk("read from pipe: %s\n", buf);
    pipe->CloseRead();
    return 100;
}

int SysInit(void *arg) {
    tty::printk("boot cmdline:%s\n", cmdline);

    task::current_proc->SetTTY(1);
    task::Message msg;
    msg.dst_pid = 3;
    msg.Recv();
    if (msg.type != 0xa00) {
        tty::Panic("Failed to mount root filesystem!");
    }

    int fstab = open("/etc/fstab", O_RDONLY, 0);
    if (fstab) {
        char read_buf[256];
        ssize_t read_count = read(fstab, read_buf, sizeof(read_buf));
        if (read_count > 0) {
            read_buf[read_count] = '\0';
            printf("Read from /etc/fstab: %s", read_buf);
        }
        close(fstab);
    }
    tty::printk("%d's pid is %d\n", task::current_proc->GetPid(), task::task_table.Find(task::current_proc->GetPid())->GetPid());
    tty::printk("\n=== Pipe Test ===\n");
    
    task::InitPipeManager();
    task::Pipe *pipe = task::pipe_manager->CreatePipe();
    if (pipe) {
        tty::printk("Pipe created: read_fd=%d, write_fd=%d\n", 
                    pipe->GetReadFd(), pipe->GetWriteFd());
        task::Task *a = task::current_proc->Clone(a1, nullptr, 0, pipe, 0);
        pipe->Write("hello", 5);
        pipe->CloseWrite();
        task::pipe_manager->DestroyPipe(pipe);
        task::current_proc->Wait(a);
        tty::printk("a exit with status %d\n", task::current_proc->GetChildExitCode());
    } else {
        tty::Panic("Failed to create pipe!");
    }
    
    char buf[256];
    const char *args[] = {"/bin/sh", nullptr};
    task::current_proc->Execve("/bin/sh", args, nullptr);

    printf("WARNING: thread init exited.\n");
    return 1;
}
