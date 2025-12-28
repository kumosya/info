#ifndef TASK_H
#define TASK_H
#include <cstdint>
#include "cpu.h"
#include "io.h"
#include "page.h"

#define STACK_SIZE 0x8000

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS 0x28
#define USER_DS 0x30

#define THREAD_KERNEL (1<<3)

namespace task {

using pid_t = std::int64_t;
enum State {
    Running,
    Ready,
    Blocked,
    Zombie,
    Dead,
};

struct Mem {
    PTE *pml4;

    std::uint64_t text_start, text_end;
    std::uint64_t data_start, data_end;
    std::uint64_t rodata_start, rodata_end;
    std::uint64_t bss_start, bss_end;
    std::uint64_t heap_start, heap_end;
    std::uint64_t stack_base;
};

struct pt_regs {
    std::uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    std::uint64_t rbx, rcx, rdx, rsi, rdi, rbp;
    std::uint64_t ds, es, rax;
    std::uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

struct Tcb {
    std::uint64_t rsp0, rip, rsp;
    std::uint16_t fs, gs;
    std::uint64_t cr2, trap_nr, error_code;
};

struct Pcb {
    pid_t pid;
    enum State stat;
    std::uint64_t flags;

    Pcb *parent;
    Tcb *thread;
    Mem mm;

    std::int64_t time_slice;
    std::uint64_t time_used;
    std::int64_t exit_code;
    std::uint64_t priority;
    
    // 任务队列链表指针
    Pcb *next;

    std::uint64_t argv;
};

extern Pcb *current_proc;
// 任务队列头指针
extern Pcb *task_queue_head;  
// 任务队列尾指针
extern Pcb *task_queue_tail;  

void schedule();

namespace queue {

void Add(Pcb *pcb);
void Remove(Pcb *pcb);
void PrintQueue();

}  // namespace queue
namespace thread {

extern pid_t pid_counter;

std::int64_t Exec(pt_regs *regs);
std::int64_t Exit(std::int64_t code);
pid_t Fork(pt_regs *regs, std::uint64_t flags, std::uint64_t stack_base, std::uint64_t stack_size);
pid_t KernelThread(std::int64_t *func, const char *arg, std::uint64_t flags);
void Init();

}  // namespace thread

inline Pcb *GetCurrent(void) {
    Pcb *current;
    __asm__ __volatile__("andq %%rsp, %0 \n\t" : "=r"(current) : "0"(~0x7fffUL));
    return current;
}

inline void SwitchTable(Pcb *prev, Pcb *next) {
    __asm__ __volatile__("movq	%0,	%%cr3	\n\t" ::"r"(next->mm.pml4) : "memory");
}

}  // namespace task

extern "C" void ret_syscall(void);
extern "C" void enter_syscall(void);
extern "C" void kernel_thread_entry(void);
extern "C" void __switch_to(task::Pcb *prev, task::Pcb *next);
int SysInit(int argc, char *argv[]);

#define SwitchContext(prev, next)     \
    do {                              \
        __asm__ __volatile__(                                                      \
            "pushq	%%rbp	\n\t"                                                     \
            "pushq	%%rax	\n\t"                                                     \
            "movq	%%rsp,	%0	\n\t"                                                  \
            "movq	%2,	%%rsp	\n\t"                                                  \
            "leaq	1f(%%rip),	%%rax	\n\t"                                   \
            "movq	%%rax,	%1	\n\t"                                                  \
            "pushq	%3		\n\t"                                                       \
            "jmp	__switch_to	\n\t"                                                 \
            "1:	\n\t"                                                              \
            "popq	%%rax	\n\t"                                                      \
            "popq	%%rbp	\n\t"                                                      \
            : "=m"(prev->thread->rsp), "=m"(prev->thread->rip)                     \
            : "m"(next->thread->rsp), "m"(next->thread->rip), "D"(prev), "S"(next) \
            : "memory");                                                           \
    } while (false)

#endif  // TASK_H
