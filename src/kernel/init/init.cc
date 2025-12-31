
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "block.h"
#include "cpu.h"
#include "io.h"
#include "mm.h"
#include "page.h"
#include "task.h"
#include "tty.h"
#include "vfs.h"

char *cmdline;

void user() {
    int i = 10, ret;
    i++;

    std::exit(i);
}

extern "C" std::uint64_t do_execve(task::pt_regs *regs) {
    void *start_addr = mm::page::Alloc(0x2000);

    regs->rdx = reinterpret_cast<std::uint64_t>(user);
    regs->rcx = reinterpret_cast<std::uint64_t>(start_addr);
    regs->rax = 1;
    regs->ds = regs->es = 0;
    tty::printf("Exec\n");
    return 1;
}

// 定义init函数，用于初始化系统核心功能
int SysInit(int argc, char *argv[]) {
    tty::printf("boot cmdline:%s\n", cmdline);

    cpu_id::PrintInfo();
    // 创建block线程
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(block::Proc),
                               "block", 0);
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(vfs::Proc),
                               "vfs", 0);
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(tty::Proc),
                               "tty", 0);
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(mm::Proc), "mm",
                               0);

    task::current_proc->thread->rip =
        reinterpret_cast<std::uint64_t>(ret_syscall);
    task::current_proc->thread->rsp = reinterpret_cast<std::uint64_t>(
        task::current_proc + STACK_SIZE - sizeof(task::pt_regs));
    task::pt_regs *regs = (task::pt_regs *)task::current_proc->thread->rsp;

    __asm__ __volatile__(
        "movq %1, %%rsp \n"
        "pushq %2\n"
        "jmp do_execve\n" ::"D"(regs),
        "m"(task::current_proc->thread->rsp),
        "m"(task::current_proc->thread->rip)
        : "memory");

    while (true) {
    }

    return 1;
}