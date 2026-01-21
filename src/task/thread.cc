#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/multiboot2.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

namespace task {

Pcb *current_proc = nullptr;
Pcb *idle = nullptr;

namespace thread {

pid_t pid_counter;

// 辅助函数：解析命令行参数字符串
static std::uint64_t ParseArgs(const char *arg, char ***argv) {
    std::uint64_t argc = 0;

    if (arg == nullptr || *arg == '\0') {
        return 0;
    }

    // 第一次遍历：计算参数数量
    char *ptr   = const_cast<char *>(arg);
    bool in_arg = false;

    while (*ptr != '\0') {
        if (*ptr == ' ') {
            in_arg = false;
        } else if (!in_arg) {
            in_arg = true;
            argc++;
        }
        ptr++;
    }

    if (argc == 0) {
        return 0;
    }

    // 分配argv数组
    *argv =
        reinterpret_cast<char **>(mm::page::Alloc((argc + 1) * sizeof(char *)));
    if (*argv == nullptr) {
        argc = 0;
        return 0;
    }

    // 第二次遍历：复制每个参数字符串
    ptr                     = const_cast<char *>(arg);
    std::uint64_t arg_index = 0;
    in_arg                  = false;
    char *arg_start         = nullptr;

    while (*ptr != '\0' && arg_index < argc) {
        if (*ptr == ' ') {
            if (in_arg) {
                // 结束当前参数
                size_t len = ptr - arg_start;
                (*argv)[arg_index] =
                    reinterpret_cast<char *>(mm::page::Alloc(len + 1));
                if ((*argv)[arg_index] != nullptr) {
                    std::memcpy((*argv)[arg_index], arg_start, len);
                    (*argv)[arg_index][len] = '\0';
                    arg_index++;
                }
                in_arg = false;
            }
        } else if (!in_arg) {
            // 开始新参数
            arg_start = ptr;
            in_arg    = true;
        }
        ptr++;
    }

    // 处理最后一个参数
    if (in_arg && arg_index < argc) {
        size_t len         = ptr - arg_start;
        (*argv)[arg_index] = reinterpret_cast<char *>(mm::page::Alloc(len + 1));
        if ((*argv)[arg_index] != nullptr) {
            std::memcpy((*argv)[arg_index], arg_start, len);
            (*argv)[arg_index][len] = '\0';
            arg_index++;
        }
    }

    // 确保argv以nullptr结尾
    (*argv)[arg_index] = nullptr;
    // 更新实际argc
    argc = arg_index;

    // tty::printk("ParseArgs: argc=%d, argv[0]=%s\n", argc, (*argv)[0]);

    return argc;
}

std::int64_t Exec(task::Registers *regs) {
    /* TODO */

    return 0;
}

// 内核线程创建函数，使用pt_regs来设置线程上下文
pid_t KernelThread(std::int64_t *fn, const char *arg, std::int32_t nice, std::uint64_t flags) {
    task::Registers regs;
    std::memset(&regs, 0, sizeof(regs));

    std::uint64_t argc = 0;
    char **argv        = nullptr;

    // 解析命令行参数
    argc = ParseArgs(arg, &argv);

    // 设置线程函数指针和参数
    regs.rbx = reinterpret_cast<std::uint64_t>(fn);
    regs.rdx = argc;
    regs.rcx = reinterpret_cast<std::uint64_t>(argv);

    // 设置段选择器
    regs.ds = KERNEL_DS;
    regs.es = KERNEL_DS;
    regs.cs = KERNEL_CS;
    regs.ss = KERNEL_DS;

    // 设置EFLAGS，确保中断使能
    regs.rflags = (1 << 9);  // IF位

    // 设置线程入口点为kernel_thread_entry包装器
    regs.rip = reinterpret_cast<std::uint64_t>(kernel_thread_entry);

    pid_t pid = Fork(&regs, flags | THREAD_KERNEL, 0, 0, nice);

    return pid;
}

void Init() {
    pid_counter = 0;
    wrmsr(0x174, KERNEL_CS);

    task::SchedInit();

    // 分配新的进程控制块，需要包含栈空间
    idle = reinterpret_cast<Pcb *>(mm::page::Alloc(sizeof(Pcb) + STACK_SIZE));
    if (!idle) {
        tty::Panic("Failed to allocate memory for idle process.\n");
    }

    std::memset(idle, 0, sizeof(Pcb) + STACK_SIZE);

    // 初始化第一个进程
    idle->parent = nullptr;

    idle->pid  = pid_counter++;
    idle->stat = task::Blocked;

    // 创建线程控制块
    Tcb *thread = reinterpret_cast<Tcb *>(mm::page::Alloc(sizeof(Tcb)));
    if (thread == nullptr) {
        mm::page::Free(idle);
        tty::Panic("Failed to allocate memory for idle thread.\n");
    }

    std::memset(thread, 0, sizeof(Tcb));
    idle->thread = thread;

    // 设置内核栈指针
    thread->rsp0 = reinterpret_cast<std::uint64_t>(idle) + sizeof(Pcb) + STACK_SIZE;
    // 设置idle线程的指令指针和栈指针
    thread->rip = reinterpret_cast<std::uint64_t>(kernel_thread_entry);
    thread->rsp = thread->rsp0;

    idle->mm.pml4 = mm::page::kernel_pml4;

    idle->weight = cfs::PRIO_TO_WEIGHT[20 + IDLE_NICE];  // 默认 nice=0，权重=1024
    idle->vruntime = 0;
    idle->sum_exec_runtime = 0;
    idle->min_vruntime = 0;

    idle->stat = task::Ready;
    idle->flags = THREAD_KERNEL;
    idle->weight = cfs::PRIO_TO_WEIGHT[20];
    idle->vruntime = 0;
    idle->sum_exec_runtime = 0;
    idle->min_vruntime = 0;

    // 将 idle 进程添加到 CFS 调度队列
    cfs::Enqueue(idle);

    current_proc = idle;
    // tty::printk("Set first process (PID: %d) as current_proc\n", idle->pid);

    wrmsr(0x175, current_proc->thread->rsp0);
    // 初始化系统调用
    wrmsr(0x176, reinterpret_cast<std::uint64_t>(enter_syscall));

    // 创建init线程
    KernelThread(reinterpret_cast<std::int64_t *>(SysInit), "init", 0, 0);

    KernelThread(reinterpret_cast<std::int64_t *>(block::Service), "block", -2, 0);
    KernelThread(reinterpret_cast<std::int64_t *>(vfs::Service), "vfs", -10, 0);
    KernelThread(reinterpret_cast<std::int64_t *>(tty::Service), "tty", 0, 0);
    KernelThread(reinterpret_cast<std::int64_t *>(mm::Service), "mm", 0, 0);
    KernelThread(reinterpret_cast<std::int64_t *>(Service), "task", 0, 0);

    asm volatile("sti");
    
    // 调用schedule函数进行第一次任务调度
    Schedule();
}
}  // namespace thread
}  // namespace task