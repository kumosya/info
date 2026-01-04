
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "kernel/block.h"
#include "kernel/cpu.h"
#include "kernel/io.h"
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
    asm volatile("cli");
    // schedule有问题，从用户态切换到内核态进程没事，从内核态进程切换到用户态进程就寄了
    //while (true);

    __asm__	__volatile__	(	"leaq	__exit_ret(%%rip),	%%rdx	\n"
					"movq	%%rsp,	%%rcx		\n"
					"sysenter			\n"
					"__exit_ret:	\n"
					:"=a"(ret):"a"(SYS_EXIT_NO), "D"(0):"memory");
    
}

extern "C" std::uint64_t do_execve(task::Registers *regs) {
    void *start_addr = mm::page::Alloc(0x2000);
    mm::page::Map(task::current_proc->mm.pml4, mm::Vir2Phy((std::uint64_t)start_addr), mm::Vir2Phy((std::uint64_t)start_addr), PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    mm::page::Map(task::current_proc->mm.pml4, mm::Vir2Phy((std::uint64_t)start_addr) + 0x1000, mm::Vir2Phy((std::uint64_t)start_addr) + 0x1000, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    regs->rdx = reinterpret_cast<std::uint64_t>(mm::Vir2Phy((std::uint64_t)user_ptr));    // vaddr of user
    regs->rcx = reinterpret_cast<std::uint64_t>(mm::Vir2Phy((std::uint64_t)start_addr)) + 0x2000;    // stack addr
    regs->rax = 1;
    regs->ds = regs->es = 0;

    __asm__ __volatile__("movq	%0,	%%cr3	\n\t" ::"r"(mm::Vir2Phy((std::uint64_t)task::current_proc->mm.pml4))
    : "memory");
    asm volatile("movq %%cr4, %%rax; orq $0x80, %%rax; movq %%rax, %%cr4" ::: "rax");

    tty::printf("Exec\n");
    return 1;
}

// 定义init函数，用于初始化系统核心功能
int SysInit(int argc, char *argv[]) {
    tty::printf("boot cmdline:%s\n", cmdline);

    CpuId cpu_id;
    cpu_id.PrintInfo();
    
    // 创建block线程
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(block::Proc),
                               "block", 0);
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(vfs::Proc),
                               "vfs", 0);
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(tty::Proc),
                               "tty", 0);
    task::thread::KernelThread(reinterpret_cast<std::int64_t *>(mm::Proc), "mm",
                               0);

    // 在切换到用户态之前，确保TSS的rsp0被更新为当前进程的内核栈地址
    if (task::current_proc != nullptr && task::current_proc->thread != nullptr) {
        gdt::tss.rsp0 = task::current_proc->thread->rsp0;
        wrmsr(0x175, task::current_proc->thread->rsp0);
    }
    
    task::current_proc->thread->rip =
        reinterpret_cast<std::uint64_t>(ret_syscall);
    task::current_proc->thread->rsp = reinterpret_cast<std::uint64_t>(
        task::current_proc + STACK_SIZE - sizeof(task::Registers));

    task::Registers *regs = (task::Registers *)task::current_proc->thread->rsp;

    PTE *user_pml4 = (PTE *)mm::page::Alloc(512*sizeof(PTE));
    memset(user_pml4, 0, 512*sizeof(PTE));
    memcpy(&user_pml4[256], &mm::page::kernel_pml4[256], 512*sizeof(PTE));

    user_ptr = mm::page::Alloc(0x2000);
    memcpy(user_ptr, (void *)user, 0x2000);
    
    mm::page::Map(user_pml4, mm::Vir2Phy((std::uint64_t)user_ptr), mm::Vir2Phy((std::uint64_t)user_ptr), PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    mm::page::Map(user_pml4, 0x11000, mm::Vir2Phy((std::uint64_t)user_ptr) + 0x1000, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    tty::printf("0x%x(0x%x)->0x%x\n", mm::Vir2Phy((std::uint64_t)user_ptr), user_ptr, mm::Vir2Phy((std::uint64_t)user_ptr));
    task::current_proc->mm.pml4 = user_pml4;

    task::current_proc->flags ^= THREAD_KERNEL;

    __asm__ __volatile__(
        "movq %1, %%rsp \n"
        "pushq %2\n"
        "jmp do_execve\n" ::"D"(regs),
        "m"(task::current_proc->thread->rsp),
        "m"(task::current_proc->thread->rip)
        : "memory");

    return 1;
}