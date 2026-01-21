
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>

#include "kernel/block.h"
#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/keyboard.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"
#include "kernel/syscall.h"

extern char *cmdline;

static void *user_ptr;

void user() {
    int i = 10, ret;
    i++;
    
    task::ipc::Message msg;
    msg.dst_pid = 4;
    msg.type = 0;
    for (i = 0; i < 9; i++) {
        msg.data[i] = '0' + i;
    }
    msg.data[9] = '\0';

    __asm__ __volatile__ (	"leaq	__send_ret(%%rip),	%%rdx	\n"
					"movq	%%rsp,	%%rcx		\n"
					"sysenter			\n"
					"__send_ret:	\n"
					:"=a"(ret):"a"(SYS_SEND_NO), "D"(&msg):"memory");
                    
    msg.dst_pid = 6;
    msg.type = 0;
    msg.num[0] = 0;

    __asm__ __volatile__ (	"leaq	__exit_ret(%%rip),	%%rdx	\n"
					"movq	%%rsp,	%%rcx		\n"
					"sysenter			\n"
					"__exit_ret:	\n"
					:"=a"(ret):"a"(SYS_SEND_NO), "D"(&msg):"memory");
    while (true);
}

extern "C" std::uint64_t do_execve(task::Registers *regs) {
    void *start_addr = mm::page::Alloc(0x2000);
    mm::page::Map(task::current_proc->mm.pml4, mm::Vir2Phy((std::uint64_t)start_addr), mm::Vir2Phy((std::uint64_t)start_addr), PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    mm::page::Map(task::current_proc->mm.pml4, mm::Vir2Phy((std::uint64_t)start_addr) + 0x1000, mm::Vir2Phy((std::uint64_t)start_addr) + 0x1000, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    regs->rdx = reinterpret_cast<std::uint64_t>(mm::Vir2Phy((std::uint64_t)user_ptr));
    regs->rcx = reinterpret_cast<std::uint64_t>(mm::Vir2Phy((std::uint64_t)start_addr)) + 0x2000;
    regs->rax = 1;
    regs->ds = regs->es = 0;

    task::SwitchTable(task::current_proc);

    tty::printk("Exec\n");
    return 1;
}

void test_exec() {
    if (task::current_proc != nullptr && task::current_proc->thread != nullptr) {
        gdt::tss->rsp0 = task::current_proc->thread->rsp0;
        wrmsr(0x175, task::current_proc->thread->rsp0);
    }
    
    task::current_proc->thread->rip =
        reinterpret_cast<std::uint64_t>(ret_syscall);
    task::current_proc->thread->rsp = reinterpret_cast<std::uint64_t>(
        reinterpret_cast<char *>(task::current_proc) + sizeof(task::Pcb) + STACK_SIZE - sizeof(task::Registers));

    task::Registers *regs = (task::Registers *)task::current_proc->thread->rsp;

    PTE *user_pml4 = (PTE *)mm::page::Alloc(512*sizeof(PTE));
    memset(user_pml4, 0, 512*sizeof(PTE));
    
    memcpy(&user_pml4[256], &mm::page::kernel_pml4[256], 256*sizeof(PTE));
    
    user_ptr = mm::page::Alloc(0x3000);
    memcpy(user_ptr, (void *)user, 0x3000);
    
    mm::page::Map(user_pml4, mm::Vir2Phy((std::uint64_t)user_ptr), 
        mm::Vir2Phy((std::uint64_t)user_ptr), 
        PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    mm::page::Map(user_pml4, 0x11000, mm::Vir2Phy((std::uint64_t)user_ptr) + 0x1000, 
        PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    

    task::current_proc->mm.pml4 = user_pml4;

    task::current_proc->flags ^= THREAD_KERNEL;

    __asm__ __volatile__(
        "movq %1, %%rsp \n"
        "pushq %2\n"
        "jmp do_execve\n" ::"D"(regs),
        "m"(task::current_proc->thread->rsp),
        "m"(task::current_proc->thread->rip)
        : "memory");
}

void Putchar(char c) {
    task::ipc::Message msg;
    msg.dst_pid = 4;
    msg.type = 0;
    msg.data[0] = c;
    msg.data[1] = '\0';
    task::ipc::Send(&msg);
}

int Printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    task::ipc::Message msg;
    int ret = vsprintf(msg.data, fmt, args);
    va_end(args);
    msg.dst_pid = 4;
    msg.type = 0;
    task::ipc::Send(&msg);
    return ret;
}

char Getchar() {
    task::ipc::Message msg;
    msg.dst_pid = 4;
    msg.type = 1;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    Putchar(msg.num[0]);

    return msg.num[0];
}

char *Gets(char *buf, std::uint64_t size) {
    std::uint64_t i = 0;
    while (i < size - 1) {
        char c = Getchar();
        if (c == '\n') {
            break;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return buf;
}

File *Open(const char *path, int flags) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type = SYS_FS_OPEN;
    strcpy(msg.s.str, path);
    msg.s.arg = flags;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return reinterpret_cast<File *>(msg.num[0]);
}

ssize_t Read(File *file, void *buf, std::uint64_t size) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type = SYS_FS_READ;
    msg.num[0] = reinterpret_cast<std::uint64_t>(file);
    msg.num[1] = reinterpret_cast<std::uint64_t>(buf);
    msg.num[2] = size;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return msg.num[0];
}

ssize_t Write(File *file, const void *buf, std::uint64_t size) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type = SYS_FS_WRITE;
    msg.num[0] = reinterpret_cast<std::uint64_t>(file);
    msg.num[1] = reinterpret_cast<std::uint64_t>(buf);
    msg.num[2] = size;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return msg.num[0];
}

int Close(File *file) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type = SYS_FS_CLOSE;
    msg.num[0] = reinterpret_cast<std::uint64_t>(file);
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return msg.num[0];
}

DirEntry *Readdir(const char *path, std::uint64_t index) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type = SYS_FS_READDIR;
    strcpy(msg.s.str, path);
    msg.s.arg = index;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return reinterpret_cast<DirEntry *>(msg.num[0]);
}

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

    File *fstab = Open("/etc/fstab", 0x01);
    if (fstab) {
        char read_buf[256];
        ssize_t read_count = Read(fstab, read_buf, sizeof(read_buf));
        if (read_count > 0) {
            read_buf[read_count] = '\0';
            Printf("Read from /etc/fstab: %s", read_buf);
        }
        Close(fstab);
    }
    char buf[256];
    //test_exec();
    while (true) {
        Printf("init> ");
        Gets(buf, sizeof(buf));
        if (strcmp(buf, "exit") == 0) {
            break;
        } else if (strcmp(buf, "version") == 0) {
            Printf("OS info version 0.1\n");
        } else if (strcmp(buf, "ls") == 0) {
            File *dir = Open("/", 0x01);
            if (dir) {
                char read_buf[256];
                ssize_t read_count = 0;
                DirEntry *entry = Readdir("/", read_count);
                while (entry) {
                    entry = Readdir("/", ++read_count);
                    if (!entry) {
                        break;
                    }
                    strcpy(read_buf, entry->name);
                    Printf("%s\n", read_buf);
                }
                Close(dir);
            } else {
                Printf("error: failed to open /.\n");
            }
        }
        else {
            Printf("init: %s: command not found.\n", buf);
        }
    }
    Printf("WARNING: thread init exited.\n");
    return 1;
}