#ifndef INFO_KERNEL_TASK_H_
#define INFO_KERNEL_TASK_H_
#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/vfs.h"

#define STACK_SIZE 0x8000

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS 0x28
#define USER_DS 0x30

#define THREAD_NO_ARGS (1 << 2)
#define THREAD_KERNEL (1 << 3)

#define IDLE_NICE 19

#define SYSCTL_SCHED_LATENCY 20000000ULL
#define SYSCTL_SCHED_MIN_GRANULARITY 4000000ULL
#define SYSCTL_SCHED_WAKEUP_GRANULARITY 2000000ULL

namespace task {

struct Pcb;

using pid_t = std::int64_t;
enum State {
    Running,
    Ready,
    Blocked,
    Zombie,
    Dead,
};

struct Registers {
    std::uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    std::uint64_t rbx, rcx, rdx, rsi, rdi, rbp;
    std::uint64_t ds, es, rax;
    std::uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

struct Mem {
    PTE *pml4;

    std::uint64_t text_start, text_end;
    std::uint64_t data_start, data_end;
    std::uint64_t rodata_start, rodata_end;
    std::uint64_t bss_start, bss_end;
    std::uint64_t heap_start, heap_end;
    std::uint64_t stack_base;
};

struct Tcb {
    std::uint64_t rsp0, rip, rsp;
    std::uint16_t fs, gs;
    std::uint64_t cr2, trap_nr, error_code;
};

namespace thread {

extern pid_t pid_counter;

pid_t UserFork(void);

std::int64_t Exec(Registers *regs);
std::int64_t Exit(std::int64_t code);
std::int64_t Kill(Pcb *proc, std::int64_t code);
pid_t Fork(Registers *regs, std::uint64_t flags, std::uint64_t stack_size,
           int nice = 0);
int Execve(const char *filename, const char *argv[], const char *envp[]);
pid_t KernelThread(std::int64_t *func, const char *arg, std::int32_t nice,
                   std::uint64_t flags);
void Init();

}  // namespace thread

class SpinLock {
   public:
    SpinLock();
    ~SpinLock();

    void lock();
    void unlock();
    bool try_lock();

   private:
    std::uint32_t state;
};

class Sem {
   public:
    Sem(std::int32_t value);
    ~Sem();

    void wait();
    void signal();
    std::int32_t get_value() const;

   private:
    std::int32_t value;
    Pcb *wait_queue;
    SpinLock lock;
};

namespace ipc {

struct Message {
    Pcb *sender;
    std::uint64_t dst_pid;
    std::uint64_t type;
    std::uint64_t size;

    union {
        char data[256];
        std::uint64_t num[32];
        struct {
            char str[240];
            std::uint64_t arg;
        } s;
    };
};

struct Pipe {
    char buffer[4096];
    std::uint64_t read_pos;
    std::uint64_t write_pos;
    std::uint64_t buffer_size;
    SpinLock lock;
    Sem *readable;
    Sem *writable;
    Pcb *reader;
    Pcb *writer;
    bool is_closed;
};

int Send(Message *msg);
int Receive(Message *msg);
std::int64_t PipeCreate(int pipefd[2]);
std::int64_t PipeRead(int fd, void *buf, std::uint64_t size);
std::int64_t PipeWrite(int fd, const void *buf, std::uint64_t size);
void PipeClose(int fd);

}  // namespace ipc

namespace cfs {

const std::uint32_t PRIO_TO_WEIGHT[40] = {
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
    9548,  7620,  6070,  4854,  3890,  3121,  2501,  2008,  1607,  1285,
    1024,  820,   655,   526,   423,   335,   272,   215,   172,   137,
    110,   87,    70,    56,    45,    36,    29,    23,    18,    15,
};

const std::uint32_t PRIO_TO_INV_WEIGHT[40] = {
    82982,     98370,     125693,    155708,    200929,    248041,    317246,
    388135,    478052,    595891,    740742,    922450,    1181682,   1475697,
    1861631,   2317688,   2893871,   3610940,   4522467,   5682162,   7084980,
    8786910,   10938093,  13700946,  17116283,  21558656,  26815000,  33538007,
    42070292,  52958222,  66630628,  84254597,  105380904, 131291284, 164756170,
    207791506, 261200000, 330926701, 419417000, 525518802,
};

struct Entity {
    Pcb *pcb;
    std::uint64_t vruntime;
    std::uint64_t sum_exec_runtime;
    std::uint64_t weight;
    std::uint64_t min_vruntime;
    Pcb *rb_left;
    Pcb *rb_right;
    Pcb *rb_parent;
    bool rb_is_red;
};

inline std::int32_t Weight2Nice(std::uint32_t weight) {
    for (std::int32_t i = 0; i < 40; i++) {
        if (PRIO_TO_WEIGHT[i] == weight) {
            return i - 20;
        }
    }
    return 0;
}

inline std::uint32_t Nice2Weight(std::int32_t nice) {
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    return PRIO_TO_WEIGHT[nice + 20];
}

inline std::uint32_t Nice2InvWeight(std::int32_t nice) {
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    return PRIO_TO_INV_WEIGHT[nice + 20];
}

class Rq {
   public:
    Rq()
        : rb_root(nullptr),
          leftmost(nullptr),
          min_vruntime(0),
          nr_running(0),
          total_weight(0) {}
    ~Rq() {}

    void RbPrintTree();

   protected:
    void RbInsert(task::Pcb *node);
    void RbErase(task::Pcb *node);

    task::Pcb *rb_root;
    task::Pcb *leftmost;
    std::uint64_t min_vruntime;
    std::uint32_t nr_running;
    std::uint64_t curr_vruntime;
    std::uint64_t total_weight;

   private:
    void RbLeftRotate(task::Pcb *x);
    void RbRightRotate(task::Pcb *y);
    void RbInsertColorFixup(task::Pcb *node);
    void RbEraseColorFixup(task::Pcb *node, task::Pcb *parent);
    void RbInitNode(task::Pcb *node);
    void RbReplaceNode(task::Pcb *u, task::Pcb *v);
};

class Sched : public Rq {
   public:
    Sched() : current(nullptr), clock(0) {}
    ~Sched() {}

    void Enqueue(Pcb *pcb);
    void Dequeue(Pcb *pcb);
    Pcb *PickNextTask();
    void UpdateClock(std::uint64_t delta);
    void UpdateVruntimeCurrent(std::uint64_t delta);
    bool NeedsSchedule();

    std::uint32_t NrRunning() { return nr_running; }
    Pcb *GetLeftmost() { return leftmost; }

    task::SpinLock lock;

   private:
    task::Pcb *FirstTask(void) { return leftmost; }
    void UpdateVruntime(task::Pcb *pcb, std::uint64_t delta);
    void NormalizeVruntime(task::Pcb *pcb);

    task::Pcb *current;
    std::uint64_t clock;
};

extern Sched sched;

}  // namespace cfs

struct Pcb {
    pid_t pid;
    enum State stat;
    std::uint64_t flags;

    Pcb *parent;
    Tcb *thread;
    Mem mm;

    std::uint64_t argv;

    ipc::Message *msg;

    std::uint64_t tty;

    std::uint64_t time_used;
    std::int64_t exit_code;

    vfs::FileDescriptorTable files;

    // CFS
    cfs::Entity se;
};

extern Pcb *current_proc;
extern Pcb *idle;
extern SpinLock run_queue_lock;
extern Pcb *run_queue_head;

void Schedule();
int Service(int argc, char *argv[]);

inline void SwitchTable(Pcb *next) {
    __asm__ __volatile__("movq	%0,	%%cr3	\n\t" ::"r"(
                             mm::Vir2Phy((std::uint64_t)next->mm.pml4))
                         : "memory");
}

}  // namespace task

extern "C" void ret_syscall(void);
extern "C" void enter_syscall(void);
extern "C" void kernel_thread_entry(void);
extern "C" void __switch_to(task::Pcb *prev, task::Pcb *next);
int SysInit(int argc, char *argv[]);

#define SwitchContext(prev, next)                                        \
    do {                                                                 \
        __asm__ __volatile__(                                            \
            "pushq	%%rbp\n"                                              \
            "pushq	%%rax\n"                                              \
            "movq	%%rsp, %0\n"                                           \
            "movq	%2,	%%rsp\n"                                           \
            "leaq	1f(%%rip), %%rax\n"                                    \
            "movq	%%rax, %1\n"                                           \
            "pushq	%3 \n"                                                \
            "jmp	__switch_to	\n"                                         \
            "1:	\n\t"                                                    \
            "popq	%%rax	\n"                                              \
            "popq	%%rbp	\n"                                              \
            : "=m"(prev->thread->rsp), "=m"(prev->thread->rip)           \
            : "m"(next->thread->rsp), "m"(next->thread->rip), "D"(prev), \
              "S"(next)                                                  \
            : "memory");                                                 \
    } while (false)

#endif  // INFO_KERNEL_TASK_H_
