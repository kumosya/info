#include "task.h"
#include "tty.h"
#include "mm.h"
#include "page.h"
#include "cpu.h"
#include "io.h"
#include "vfs.h"
#include "block.h"
#include "multiboot2.h"

#include <cstdint>
#include <cstring>
#include <stddef.h>

namespace task {
    
Pcb *current_proc = nullptr;
// 任务队列头指针
Pcb *task_queue_head = nullptr;
// 任务队列尾指针
Pcb *task_queue_tail = nullptr;

namespace thread {

pid_t pid_counter;

std::int64_t Exec(task::pt_regs *regs) {
    /* TODO */
    
    return 0;
}

// 辅助函数：解析命令行参数字符串
static std::uint64_t ParseArgs(const char *arg, char ***argv) {
    std::uint64_t argc = 0;
    
    if (arg == nullptr || *arg == '\0') {
        return 0;
    }
    
    // 第一次遍历：计算参数数量
    char *ptr = const_cast<char *>(arg);
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
    *argv = reinterpret_cast<char **>(mm::page::Alloc((argc + 1) * sizeof(char *)));
    if (*argv == nullptr) {
        argc = 0;
        return 0;
    }
    
    // 第二次遍历：复制每个参数字符串
    ptr = const_cast<char *>(arg);
    std::uint64_t arg_index = 0;
    in_arg = false;
    char *arg_start = nullptr;
    
    while (*ptr != '\0' && arg_index < argc) {
        if (*ptr == ' ') {
            if (in_arg) {
                // 结束当前参数
                size_t len = ptr - arg_start;
                (*argv)[arg_index] = reinterpret_cast<char *>(mm::page::Alloc(len + 1));
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
            in_arg = true;
        }
        ptr++;
    }
    
    // 处理最后一个参数
    if (in_arg && arg_index < argc) {
        size_t len = ptr - arg_start;
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
    
    //tty::printf("ParseArgs: argc=%d, argv[0]=%s\n", argc, (*argv)[0]);
    
    return argc;
}

// 内核线程创建函数，使用pt_regs来设置线程上下文
pid_t KernelThread(std::int64_t *fn, const char *arg, std::uint64_t flags) {
    task::pt_regs regs;
    std::memset(&regs, 0, sizeof(regs));
    
    std::uint64_t argc = 0;
    char **argv = nullptr;
    
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
       
    pid_t pid = Fork(&regs, flags | THREAD_KERNEL, 0, 0);
 
    return pid;
}

void Init() {
    pid_counter = 0;
    wrmsr(0x174, KERNEL_CS);
    // 初始化系统调用表
    
    // 分配新的进程控制块
    Pcb *idle = reinterpret_cast<Pcb*>(mm::page::Alloc(sizeof(Pcb)));
    if (!idle) {
        tty::Panic("Failed to allocate memory for idle process.\n");
    }
    
    std::memset(idle, 0, sizeof(Pcb));
    
    // 初始化第一个进程
    idle->parent = nullptr;
    
    idle->pid = pid_counter++;
    idle->stat = task::Blocked;
    
    // 创建线程控制块
    Tcb *thread = reinterpret_cast<Tcb*>(mm::page::Alloc(sizeof(Tcb)));
    if (thread == nullptr) {
        mm::page::Free(idle);
        tty::Panic("Failed to allocate memory for idle thread.\n");
    }
    
    std::memset(thread, 0, sizeof(Tcb));
    idle->thread = thread;
    
    // 设置内核栈指针
    thread->rsp0 = reinterpret_cast<std::uint64_t>(idle) + STACK_SIZE;
    // 设置idle线程的指令指针和栈指针
    thread->rip = reinterpret_cast<std::uint64_t>(kernel_thread_entry);
    thread->rsp = thread->rsp0;
    
    idle->stat = task::Ready;
    
    // 将新创建的进程添加到任务队列
    queue::Add(idle);
    
    current_proc = idle;
    //tty::printf("Set first process (PID: %d) as current_proc\n", idle->pid);
    
    // 创建init线程
    KernelThread(reinterpret_cast<std::int64_t *>(SysInit), "init", 0);
    
    // 调用schedule函数进行第一次任务调度
    schedule();
}

}  // namespace thread
}  // namespace task