# 任务子系统（src/kernel/task/）

本文档描述 `src/kernel/task/` 的核心实现：线程（`thread.cc`）、进程与句柄表（`process.cc`）、调度器（`scheduler.cc`）、信号投递（`signal.cc`）与上下文切换汇编（`switch.S`）。IPC 见 [ipc.md](ipc.md)，同步原语见 [sync.md](sync.md)，系统调用见 [syscall.md](syscall.md)。对应头文件 `include/kernel/thread.h`、`process.h`、`scheduler.h`、`signal.h`。

## 1. 线程（thread.cc）

### 状态与类型

```cpp
enum class ThreadState : uint8_t {
    Created, Ready, Running, Blocked, Sleeping, Stopped, Terminated
};
enum class ThreadType : uint8_t { Kernel, User };
using ThreadEntry = void (*)(void* argument);
```

### Thread 结构（关键字段）

```cpp
struct Thread {
    uint64_t id;
    ThreadState state;
    ThreadType type;
    void* kernelStack;          // 栈底
    void* kernelStackTop;       // 栈顶（当前）
    void* userStack;
    CpuContext context;
    Process* process;
    uint64_t cpuId;
    uint64_t cpuAffinity;       // 位掩码，默认 ~0ULL
    uint64_t vruntime, runtime, timeSlice, sliceRuntime;
    uint32_t weight;
    int32_t nice;
    ThreadEntry entryPoint;
    void* argument;
    uint64_t userArgument;
    int exitCode;
    // 红黑树节点
    Thread* rbParent; Thread* rbLeft; Thread* rbRight; bool rbRed;
    bool inRunQueue;
    bool wakePending;
    ThreadState stateBeforeStop;
    Thread* waitNext;           // 等待队列链接
    Thread* joinWaiters; Thread* joinWaitersTail;
    volatile uint32_t* waitAddress; uint64_t waitKey;  // futex
    uint64_t signalMask; uint64_t pendingSignals;
    Thread* processNext;
    uint64_t wakeDeadlineNanoseconds;
};
```

`CpuContext` 含 `stackPointer`、15 个通用寄存器、`rip`、`rflags`、`cr3`、`fsBase`、`gsBase`（实际切换只在栈上保存 15 个通用寄存器 + RFLAGS）。

### 创建与启动

- 内核栈固定 **32 KiB**（`kernelStackSize`）；线程 ID 由全局原子计数器分配（从 1 起）。
- `createThread(process, entryPoint, argument, type = Kernel, makeRunnable = true)`：分配 Thread + 栈；构造初始栈帧（自栈顶向下：填充 0、`threadBootstrap` 返回地址、RFLAGS `0x202`、15 个零寄存器槽，与 `switchThreadContext` 弹出顺序匹配）；默认 `weight = getSchedulerWeight(0)`（1024）、`timeSlice = 4 ms`；`process->attachThread`；`makeRunnable` 且调度器已初始化则入队。
- `createUserThread(process, entryPoint, userStackPointer, argument = 0)`：校验入口与栈指针在用户半区；复用 `createThread`（User 类型），记录 `userStack`/`userArgument`。
- `createForkedUserThread(process, context, signalMask)`：fork 用；堆上复制 `SyscallContext` 并置 `rax = 0`（子进程返回 0）；以 Kernel 类型、入口 `forkedUserEntry` 创建，随后显式入队；`forkedUserEntry` 将线程类型置 User 后 `resumeUserContext` 直接进入用户态。

### 生命周期

```cpp
extern "C" void threadBootstrap();
[[noreturn]] void exitThread(int exitCode);
void blockThread();
void wakeThread(Thread* thread);
void sleepThread(uint64_t milliseconds);
void switchThread(Thread* previous, Thread* next);
Thread* getCurrentThread();
void terminateCurrentThreadFromException(int exitCode);
```

- `threadBootstrap`：开中断；User 线程 → `enterUserMode(entryPoint, userStack, userArgument)`（不返回）；Kernel 线程 → 执行 `entryPoint(argument)` 后 `exitThread(0)`。
- `exitThread`：关中断 → 记录退出码、状态 Terminated → `process->notifyThreadExit` → 请求重调度并 `schedule()`。
- `blockThread`：先用原子交换检查 `wakePending`（防丢失唤醒），有则直接返回；否则置 Blocked 并重新调度。
- `wakeThread`：目标 Running 则置 `wakePending`；Stopped 且停止前为 Blocked/Sleeping 则把 `stateBeforeStop` 改为 Ready；Blocked/Sleeping 则置 Ready 并入队。
- `terminateCurrentThreadFromException`：仅允许 User 线程（否则 halt），随后走 `exitThread`。

### 上下文切换（switch.S）

- `switchThreadContext(uint64_t* prevRsp, uint64_t nextRsp)`：`pushfq` + 15 个寄存器（128 字节帧）→ 保存旧栈 → 切换 → 逆序恢复 → `popfq`、`ret`。
- `enterUserMode(entryPoint, stackPointer, argument)`：构造 iretq 帧（SS=0x1b、RFLAGS=0x202、CS=0x23），参数入 rdi，`swapgs`，`iretq`。
- `resumeUserContext(SyscallContext*)`：恢复全部寄存器后 `swapgs`、`sysretq`（rcx→RIP、r11→RFLAGS）。

## 2. 进程（process.cc）

### 进程状态与能力

```cpp
enum class ProcessState : uint8_t { Created, Running, Terminated, Stopped };
constexpr uint64_t processCapabilityDevice  = 1ULL << 0;
constexpr uint64_t processCapabilitySpawn   = 1ULL << 1;
constexpr uint64_t processCapabilityDisplay = 1ULL << 2;
```

### 地址空间与句柄表

```cpp
class AddressSpace {  // initialize/replaceRoot/rootPhysicalAddress/lock
};

enum class HandleType : uint16_t {
    None, Endpoint, Process, MmioRegion, DmaBuffer,
    InterruptBinding, Pipe, Pty, MessageQueue, SharedMemory
};
constexpr uint32_t handleRightRead   = 1U << 0;
constexpr uint32_t handleRightWrite  = 1U << 1;
constexpr uint32_t handleRightManage = 1U << 2;
```

句柄编码：低 32 位 = 槽位索引 + 1，高 16 位 = generation（跳过 0）。`HandleTable` 接口：`initialize(capacity)`、`add`、`addAt`（fork 继承，指定句柄值）、`get`（校验类型与权限）、`inspect`/`inspectAt`、`close`、`closeAll`。关闭时按类型分发对象释放：Pipe/Pty/MessageQueue/SharedMemory 调用各自的 `closeXxxHandleObject`；Endpoint/Process 等不释放。每进程初始 64 槽。

### 进程创建与 fork/exec

- `Process::create(name, parent)`：`createAddressSpaceRoot` 建页表根 → 初始化（pid 原子分配、名字 ≤63 字符、句柄表 64 槽、继承父进程 pgid/sid 或等于自身 id）→ 挂入父进程 child 链表与全局进程链表。
- `inheritForFork(parent)`：复制能力、引导句柄、3 个标准句柄、pgid/sid、全部信号动作；逐槽复制父进程句柄表（Endpoint/Pipe/Pty/MessageQueue/SharedMemory 用各自 duplicate，其余直接 `addAt`）。
- `commitExec(newRoot, name, currentThread)`：杀死除调用线程外的所有线程；线程计数置 1；复位非 ignore 信号动作；换名；`replaceRoot` 换页表根；释放共享内存映射、切换地址空间、销毁旧根。
- `waitForChild(pid, waiter, status, options, childId)`：pid 语义（-1 任意 / >0 指定 / 0 同组 / < -1 指定组）；options 位 1=NOHANG、2=UNTRACED、8=CONTINUED；status 编码：正常退出 `(exitCode & 0xff) << 8`、信号致死 `(-exitCode) & 0x7f`、停止 `(stopSignal << 8) | 0x7f`、继续 `0xffff`；返回 -1 无匹配 / 0 已回收 / 1 需阻塞 / 2 NOHANG 无事件。

### 线程与退出

- `attachThread`：需 `acceptsThreads_`；头插链表；首个线程为 `mainThread_`；进程状态置 Running。
- `notifyThreadExit`：唤醒该线程的 join 等待者；计数递减；归零且非内核进程 → 进程 Terminated、关闭全部句柄、通知父进程（`notifyChildStateChange` + 投递 `Signal::Child`）。
- `terminateOtherThreads(exitCode)`：杀死除当前线程外的所有线程。
- `destroy()`：跳过内核进程与当前进程；终止线程、关闭句柄、释放共享内存映射、销毁地址空间根；从链表摘除。
- `exitCurrentProcess(exitCode)`：内核进程只退出线程；否则先 `terminateOtherThreads` 再 `exitThread`。

### 会话与信号动作

- `createSession()`：已是进程组组长则失败；否则 sid = pgid = 自身 id。
- `setSignalAction(signal, action, oldAction)`：拒绝无效信号及 Kill/Stop；设 ignore 时清除该信号 pending 位。
- `takePendingSignal(thread, accepted, action, respectMask = true)`：取 `(进程|线程) & accepted &（可选）~signalMask` 中编号最小者；优先清线程私有位；`signalFlagResetHand` 时复位为默认。
- `deliverSignal(signal)` 进程级投递规则：
  - ignore → 成功；
  - 入队条件（置 `pendingSignals_` 位）：非 Kill/Stop 且（handler 非默认 或 所有线程都屏蔽）；
  - **Continue**：清 `stopped_`、恢复所有 Stopped 线程、通知父进程 + SIGCHLD；
  - **Stop/TerminalStop/TerminalInput/TerminalOutput**：置 `stopped_`、所有线程保存 `stateBeforeStop` 后置 Stopped 并出队、通知父进程 + SIGCHLD；
  - **其余致命信号**：`terminateOtherThreads(-signal)`；当前进程则 `exitThread(-signal)`；最后 `destroy()`。

### 全局接口

```cpp
void initProcessSubsystem();   // 创建 "kernel" 进程，attach 引导线程
Process* getCurrentProcess();
Process* getKernelProcess();
Process* findProcess(uint64_t processId);
bool isValidSignal(uint32_t signal);   // 1..64
bool sendSignalToProcess(uint64_t processId, uint32_t signal);
bool sendSignalToProcessGroup(uint64_t processGroupId, uint32_t signal);
```

## 3. 调度器（scheduler.cc）

每 CPU 独立运行队列的 CFS 风格调度器（红黑树 + 虚拟运行时）。

### 运行队列

```cpp
struct RunQueue {
    Thread* root;              // 红黑树根
    Thread* leftmost;          // 最小 vruntime
    Thread* currentThread;
    Thread* idleThread;
    Thread* sleepingThreads;   // 按到期时间排序的睡眠链表
    uint64_t runnableCount;
    uint64_t minVruntime;
    uint64_t clock;
    SpinLock lock;
};
```

### 权重与 vruntime

- 权重表 `niceWeights[40]`（Linux 风格，索引 = nice + 20）：nice 0 = 1024，范围 88761（nice -20）到 15（nice 19）。
- `calculateVruntimeDelta(runtime, weight) = (runtime * 1024 + weight - 1) / weight`（向上取整）。
- 红黑树排序键：先比 `vruntime`，相等比线程 `id`。

### 接口

```cpp
void initScheduler();
bool isSchedulerInitialized();
void enqueueThread(Thread* thread);
void dequeueThread(Thread* thread);
Thread* pickNextThread();
void schedule();
void yieldThread();
void requestReschedule();
void schedulerTick();
void sleepCurrentThread(uint64_t milliseconds);
bool setThreadAffinity(Thread* thread, uint64_t cpuMask);
bool migrateThread(Thread* thread, uint32_t cpuId);
uint32_t getSchedulerWeight(int32_t nice);
```

### 关键行为

- `initScheduler`：为当前 CPU 分配 RunQueue；创建 idle 线程并立即摘除；注册定时器中断（向量 32）并配置 APIC Timer（初始计数 1,000,000，约 1 ms）。
- `enqueueThread`：目标 CPU 首选 `thread->cpuId`（需亲和位允许），否则扫描在线 CPU；入队前 `vruntime < minVruntime` 则钳制；目标非当前 CPU 则 `requestRemoteReschedule`。
- `schedule`：关中断 + 队列锁；前一线程仍 Running 则置 Ready 重新插入；取 leftmost（无则 idle）；置 Running、更新 `cpuId`/`currentThread`/`currentProcess`；若切换则 `setKernelStack` → `switchAddressSpace`（无进程用内核根）→ `switchThread`。
- `schedulerTick`：更新时钟与 `tickCount`；当前线程累加 `runtime`/`sliceRuntime`/`vruntime`；**抢占条件**：时间片耗尽（`sliceRuntime >= timeSlice`）或 `leftmost->vruntime < current->vruntime`；随后唤醒睡眠链表上所有到期线程。
- `sleepCurrentThread`：0 → yield；否则置 Sleeping、计算 `wakeDeadlineNanoseconds`、按截止时间升序插入睡眠链表、`schedule`。
- `migrateThread`：仅允许 Ready 线程、目标在线、亲和允许。

## 4. 信号投递（signal.cc）

用户态信号帧的投递与恢复，配合系统调用返回路径（详见 [syscall.md](syscall.md)）。

```cpp
bool deliverPendingSignal(SyscallContext* context);
bool restoreSignalFrame(SyscallContext* context, uint64_t userFrame);
```

信号帧：

```cpp
struct SignalFrame {
    uint64_t magic;      // 0x5349474652414d45（"SIGFRAME"）
    uint64_t oldMask;
    SyscallContext context;
};
```

- `deliverPendingSignal`：系统调用返回前调用；取一个待处理信号；要求 `handler > signalIgnore && restorer != 0`；在用户栈上构造 16 字节对齐的帧，并在帧上方压 `restorer` 地址作为处理函数返回地址；更新线程信号掩码（`|= action.mask`，未设 NoDefer 时再屏蔽本信号，Kill/Stop 位永远清除）；改写上下文 `userRip = handler`、`rdi = signal`。
- `restoreSignalFrame`：sigreturn 路径；校验 magic 与帧内地址；净化 `userRflags`；恢复 `oldMask` 与上下文。

## 5. 信号编号（signal.h）

POSIX 风格编号（`enum class Signal : uint32_t`）：Hangup=1、Interrupt=2、Quit=3、IllegalInstruction=4、Trap=5、Abort=6、Bus=7、FloatingPoint=8、Kill=9、User1=10、SegmentationViolation=11、User2=12、BrokenPipe=13、Alarm=14、Terminate=15、StackFault=16、Child=17、Continue=18、Stop=19、TerminalStop=20、TerminalInput=21、TerminalOutput=22、Urgent=23、CpuLimit=24、FileSizeLimit=25、VirtualAlarm=26、Profiling=27、WindowChanged=28。`maximumSignal = 64`。

信号动作：

```cpp
constexpr uint64_t signalDefault = 0;
constexpr uint64_t signalIgnore = 1;
constexpr uint64_t signalFlagNoDefer = 0x40000000;
constexpr uint64_t signalFlagResetHand = 0x80000000;
struct SignalAction { uint64_t handler; uint64_t mask; uint64_t flags; uint64_t restorer; };
```

`handler` 取值：0 = 默认、1 = 忽略、>1 = 用户处理函数地址。
