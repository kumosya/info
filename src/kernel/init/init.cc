
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

int SysInit(int argc, char *argv[]) {
    tty::printk("boot cmdline:%s\n", cmdline);

    CpuId cpu_id;
    cpu_id.PrintInfo();

    task::current_proc->tty = 1;  // 绑定到第一个TTY
    task::ipc::Message msg;
    msg.dst_pid = 3;
    task::ipc::Receive(&msg);
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
    char buf[256];

    const char *args[] = {"/bin/sh", nullptr};
    task::thread::Execve("/bin/sh", args, nullptr);

    printf("WARNING: thread init exited.\n");
    return 1;
}