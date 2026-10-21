# 任务管理（src/kernel/task/）

本文档描述 `src/kernel/task/` 的核心实现：线程（`thread.cc`）、进程与句柄表（`task.cc`）、调度器（`schedule.cc`）、信号投递（`signal.cc`）与上下文切换汇编（`switch.S`）。IPC 见 [ipc.md](ipc.md)，同步原语见 [sync.md](sync.md)，系统调用见 [syscall.md](syscall.md)。对应头文件 `include/kernel/thread.h`、`task.h`、`signal.h`。

## 1. 线程（thread.cc）

### 状态与类型

```cpp
enum class ThreadState : uint8_t {
    Created, Ready, Running, Blocked, Sleeping, Stopped, Terminated
};
enum class ThreadType : uint8_t { Kernel, User, SystemService };
using ThreadEntry = void (*)(void* argument);
```

### CpuContext 类

```cpp
class CpuContext {
   public:
    uint64_t getStackPointer() const;
    uint64_t setStackPointer(uint64_t value);
    uint64_t& accessStackPointer();
    uint64_t setRip(uint64_t value);
    uint64_t setRflags(uint64_t value);
   private:
    uint64_t stackPointer;
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags, cr3, fsBase, gsBase;
};
```

### Thread 类（关键字段）

```cpp
class Thread {
   public:
    static void init();
    static Thread* create(Task* task, ThreadEntry entryPoint, void* argument,
                          ThreadType type = ThreadType::Kernel,
                          bool makeRunnable = true);
    static Thread* createUser(Task* task, uint64_t entryPoint,
                              uint64_t userStackPointer,
                              uint64_t argument = 0,
                              uint64_t initialStackSize = 0);
    static Thread* createForkedUser(Task* task, const SyscallContext& context,
                                    uint64_t signalMask);
    [[noreturn]] static void exitCurrent(int exitCode);
    static void blockCurrent();
    static void prepareWait();
    static void wake(Thread* thread);
    static void sleepCurrent(uint64_t milliseconds);
    void switchTo(Thread* next);
    static Thread* getCurrent();
    static void terminateCurrentFromException(int exitCode);
    static void bootstrap();

    bool migrate(uint32_t cpuId);
    bool setAffinity(uint64_t cpuMask);

    // getter/setter: getId/setId, getState/setState, getType/setType, ...

   private:
    uint64_t id;
    ThreadState state;
    ThreadType type;
    void* kernelStack;
    void* kernelStackTop;
    void* userStack;
    CpuContext context;
    Task* task;
    uint64_t cpuId;
    uint64_t cpuAffinity;
    uint64_t vruntime;
    uint64_t runtime;
    uint64_t timeSlice;
    uint64_t sliceRuntime;
    uint32_t weight;
    int32_t nice;
    ThreadEntry entryPoint;
    void* argument;
    void* userArgument;
    int exitCode;
    Thread* rbParent;
    Thread* rbLeft;
    Thread* rbRight;
    bool rbRed;
    bool inRunQueue;
    bool wakePending;
    bool activeIpcCall;
    ThreadState stateBeforeStop;
    Thread* waitNext;
    Thread* joinWaiters;
    Thread* joinWaitersTail;
    volatile uint32_t* waitAddress;
    uint64_t waitKey;
    uint64_t signalMask;
    uint64_t pendingSignals;
    Thread* taskNext;
    uint64_t wakeDeadlineNanoseconds;
    alignas(16) uint8_t floatingPointState[512];
};
```

### 创建与启动

- 普通线程内核栈为 **64 KiB**（`kernelStackSize`）；固定 boot 服务的用户线程分配 256 KiB 内核栈；线程 ID 由全局原子计数器分配（从 1 起）。
- `Thread::create(task, entryPoint, argument, type, makeRunnable)`：分配 Thread + 栈；构造初始栈帧（自栈顶向下：填充 0、`Thread::bootstrap` 返回地址、RFLAGS `0x202`、15 个零寄存器槽，与 `switchThreadContext` 弹出顺序匹配）；默认 `weight = schedule::getSchedulerWeight(0)`（1024）、`timeSlice = 4 ms`；`task->attachThread`；`makeRunnable` 且调度器已初始化则入队。
- `Thread::createUser(task, entryPoint, userStackPointer, argument, initialStackSize)`：校验入口与栈指针在用户半区；复用 `create`（User 或 SystemService 类型），记录 `userStack`/`userArgument`。SystemService 类型时从用户空间复制初始栈到内核侧。
- `Thread::createForkedUser(task, context, signalMask)`：fork 用；堆上复制 `SyscallContext` 并置 `rax = 0`（子进程返回 0）；以 Kernel 类型、入口 `forkedUserEntry` 创建，随后入队；`forkedUserEntry` 将线程类型置 User 后 `resumeUserContext` 直接进入用户态。

### 生命周期

- `Thread::bootstrap`：`schedule::finishContextSwitch()` → 开中断；User 线程 → `enterUserMode(entryPoint, userStack, userArgument)`（不返回）；SystemService 线程 → `enterSystemService`；Kernel 线程 → 执行 `entryPoint(argument)` 后 `exitCurrent(0)`。
- `Thread::exitCurrent`：关中断 → 记录退出码、状态 Terminated → `task->notifyThreadExit` → 请求重调度并 `schedule()`。
- `Thread::blockCurrent`：委托 `schedule::blockCurrentThread()`。
- `Thread::wake`：委托 `schedule::wakeThread()`。
- `Thread::terminateCurrentFromException`：仅允许 User 线程（否则 halt），随后走 `exitCurrent`。

### 浮点状态

每线程保存 512 字节 FXSAVE 区域（`floatingPointState`）。`switchTo` 时 `fxsave64` 保存旧线程、`fxrstor64` 恢复新线程。`resetFloatingPointState` 初始化 x87 控制字和 MXCSR。

### 上下文切换（switch.S）

- `switchThreadContext(uint64_t* prevRsp, uint64_t nextRsp)`：`pushfq` + 15 个寄存器（128 字节帧）→ 保存旧栈 → 切换 → 逆序恢复 → `popfq`、`ret`。
- `enterUserMode(entryPoint, stackPointer, argument)`：构造 iretq 帧（SS=0x1b、RFLAGS=0x202、CS=0x23），参数入 rdi，`swapgs`，`iretq`。
- `enterSystemService(entryPoint, stackPointer, argument)`：类似但进入系统服务入口。
- `resumeUserContext(SyscallContext*)`：恢复全部寄存器后 `swapgs`、`sysretq`（rcx→RIP、r11→RFLAGS）。

## 2. 进程（task.cc）

### 进程状态与能力

```cpp
enum class TaskState : uint8_t { Created, Running, Terminated, Stopped };
constexpr uint64_t taskCapabilityDevice  = 1ULL << 0;
constexpr uint64_t taskCapabilitySpawn   = 1ULL << 1;
constexpr uint64_t taskCapabilityDisplay = 1ULL << 2;
constexpr uint64_t taskCapabilityUsers   = 1ULL << 3;
```

### 地址空间与句柄表

```cpp
class AddressSpace {  // init/replaceRoot/rootPhysicalAddress/lock
};

enum class HandleType : uint16_t {
    None, Endpoint, Task, MmioRegion, DmaBuffer,
    InterruptBinding, SharedMemory, PageObject
};
constexpr uint32_t handleRightRead   = 1U << 0;
constexpr uint32_t handleRightWrite  = 1U << 1;
constexpr uint32_t handleRightManage = 1U << 2;
```

句柄编码：低 32 位 = 槽位索引 + 1，高 16 位 = generation（跳过 0）。`HandleTable` 接口：`init(capacity)`、`add`、`addAt`（fork 继承，指定句柄值）、`get`（校验类型与权限）、`inspect`/`inspectAt`、`close`、`closeAll`。支持动态增长（`growLocked`）。关闭时释放共享 Reference，最后一个引用再按类型释放对象；Task、Endpoint 和 PageObject 均有明确引用计数。dispose 释放 entries 数组并拒绝再次安装。每进程初始 64 槽。

### 进程创建与 fork/exec

- `Task::create(name, parent)`：`memory::createAddressSpaceRoot` 建页表根 → 初始化（pid 原子分配、名字 ≤63 字符、句柄表 64 槽、继承父进程 pgid/sid 或等于自身 id）→ 挂入父进程 child 链表与全局进程链表。
- `inheritForFork(parent)`：复制能力、引导句柄、3 个标准句柄、pgid/sid、全部信号动作；逐槽复制父进程句柄表（Endpoint/Pipe/Channel/MessageQueue/SharedMemory 用各自 duplicate，其余通过 copyTo 共享 Reference）。
- `commitExec(newRoot, name, currentThread)`：杀死除调用线程外的所有线程；线程计数置 1；复位非 ignore 信号动作；换名；`replaceRoot` 换页表根；释放共享内存映射、切换地址空间、销毁旧根。
- `waitForChild(pid, waiter, status, options, childId)`：pid 语义（-1 任意 / >0 指定 / 0 同组 / < -1 指定组）；options 位 1=NOHANG、2=UNTRACED、8=CONTINUED；status 编码：正常退出 `(exitCode & 0xff) << 8`、信号致死 `(-exitCode) & 0x7f`、停止 `(stopSignal << 8) | 0x7f`、继续 `0xffff`；返回 -1 无匹配 / 0 已回收 / 1 需阻塞 / 2 NOHANG 无事件。

### 用户凭据与文件表

```cpp
class Task {
   public:
    uint32_t uid() const;
    uint32_t gid() const;
    uint32_t effectiveUid() const;
    uint32_t effectiveGid() const;
    void setCredentials(uint32_t uid, uint32_t gid, bool effectiveOnly = false);
    bool isSystemService() const;
    void setSystemService(bool value);
    uint32_t terminalId() const;
    void setTerminalId(uint32_t value);
};
```

- 凭据模型：uid/gid + effectiveUid/effectiveGid；`setCredentials` 支持仅修改 effective 版本。
- POSIX 文件描述符表由 libc 与 fs 服务维护，不在 Task 中创建 FileTable。
- 所有服务进程使用 User 线程在 Ring3 运行；SystemService 枚举是未启用的旧入口，boot 加载器始终设置 isSystemService=false。

### 线程与退出

- `attachThread`：需 `acceptsThreads_`；头插链表；首个线程为 `mainThread_`；进程状态置 Running。
- `notifyThreadExit`：唤醒该线程的 join 等待者；计数递减；归零且非内核进程 → 进程 Terminated、关闭全部句柄、通知父进程（`notifyChildStateChange` + 投递 `Signal::Child`）。
- `terminateOtherThreads(exitCode)`：杀死除当前线程外的所有线程。
- `destroy()`：跳过内核进程与当前进程；终止线程、关闭句柄、释放共享内存映射、销毁地址空间根；从链表摘除。
- `task::exitCurrent(exitCode)`：内核进程只退出线程；否则先 `terminateOtherThreads` 再 `exitCurrent`。

### 会话与信号动作

- `createSession()`：已是进程组组长则失败；否则 sid = pgid = 自身 id。
- `setTaskGroup(groupId)`：在同一会话内加入现有进程组；`groupId == 0` 时以自身 id 创建进程组。
- `setSignalAction(signal, action, oldAction)`：拒绝无效信号及 Kill/Stop；设 ignore 时清除该信号 pending 位。
- `takePendingSignal(thread, accepted, action, respectMask = true)`：取 `(进程|线程) & accepted &（可选）~signalMask` 中编号最小者；优先清线程私有位；`signalFlagResetHand` 时复位为默认。
- `deliverSignal(signal)` 进程级投递规则：
  - ignore → 成功；
  - 入队条件（置 `pendingSignals_` 位）：非 Kill/Stop 且（handler 非默认 或 所有线程都屏蔽）；
  - **Continue**：清 `stopped_`、恢复所有 Stopped 线程、通知父进程 + SIGCHLD；
  - **Stop/TerminalStop/TerminalInput/TerminalOutput**：置 `stopped_`、所有线程保存 `stateBeforeStop` 后置 Stopped 并出队、通知父进程 + SIGCHLD；
  - **其余致命信号**：`terminateOtherThreads(-signal)`；当前进程则 `Thread::exitCurrent(-signal)`；最后 `destroy()`。

### 全局接口

```cpp
namespace kernel::task {
void init();                    // 创建 "kernel" 进程，attach 引导线程
Task* getCurrent();
Task* getIdle();                // 返回内核进程
Task* find(uint64_t taskId);
Task* at(uint32_t index);      // 按索引遍历（跳过内核进程）
[[noreturn]] void exitCurrent(int exitCode);
}

namespace kernel::signal {
bool isValidSignal(uint32_t signal);    // 1..64
bool sendSignalToTask(uint64_t taskId, uint32_t signal);
bool sendSignalToTaskGroup(uint64_t taskGroupId, uint32_t signal);
}
```

信号发送包含权限检查：调用者 effectiveUid 为 0 或与目标进程 uid/effectiveUid 匹配才允许投递。`sendSignalToTaskGroup` 支持 signal=0 的存在性检查，采用分批处理（每批 32 个）避免长期持有全局锁。

## 3. 调度器（schedule.cc）

每 CPU 独立运行队列的 CFS 风格调度器（红黑树 + 虚拟运行时）。所有函数位于 `namespace kernel::schedule`。

### 运行队列

```cpp
class RunQueue {
   public:
    static void clearRunQueue(RunQueue* queue);
    void insertNode(Thread* thread);
    void removeNode(Thread* thread);
    // getter/setter ...
   private:
    static bool isRed(const Thread* node);
    static bool comesBefore(const Thread* left, const Thread* right);
    void rotateLeft(Thread* node);
    void rotateRight(Thread* node);
    void insertFixup(Thread* node);
    void transplant(Thread* target, Thread* replacement);
    void deleteFixup(Thread* node, Thread* parent);

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
namespace kernel::schedule {
void init();
bool isSchedulerInitialized();
void enqueueThread(Thread* thread);
void prepareCurrentWait();
void blockCurrentThread();
void wakeThread(Thread* thread);
void finishContextSwitch();
void dequeueThread(Thread* thread);
Thread* pickNextThread();
void schedule();
void yieldThread();
void requestReschedule();
void schedulerTick();
void sleepCurrentThread(uint64_t milliseconds);
uint32_t getSchedulerWeight(int32_t nice);
}
```

### 关键行为

- `init`：为当前 CPU 分配 RunQueue；创建 idle 线程并立即摘除；注册定时器中断（向量 32）并配置 APIC Timer（初始计数 1,000,000，约 1 ms）。
- `enqueueThread`：目标 CPU 首选 `thread->cpuId`（需亲和位允许），否则扫描在线 CPU；入队前 `vruntime < minVruntime` 则钳制；目标非当前 CPU 则 `smp::requestRemoteReschedule`。
- `schedule`：关中断 + 队列锁；前一线程仍 Running 则置 Ready 重新插入；取 leftmost（无则 idle）；置 Running、更新 `cpuId`/`currentThread`/`currentTask`；若切换则 `setKernelStack` → `switchAddressSpace`（无进程用内核根）→ `switchThread`。
- `schedulerTick`：更新时钟与 `tickCount`；当前线程累加 `runtime`/`sliceRuntime`/`vruntime`；**抢占条件**：时间片耗尽（`sliceRuntime >= timeSlice`）或 `leftmost->vruntime < current->vruntime`；随后唤醒睡眠链表上所有到期线程。
- `sleepCurrentThread`：0 → yield；否则置 Sleeping、计算 `wakeDeadlineNanoseconds`、按截止时间升序插入睡眠链表、`schedule`。

## 4. 信号投递（signal.cc）

用户态信号帧的投递与恢复，配合系统调用返回路径（详见 [syscall.md](syscall.md)）。

```cpp
namespace kernel::signal {
bool deliverPendingSignal(SyscallContext* context);
bool restoreSignalFrame(SyscallContext* context, uint64_t userFrame);
}
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

class SignalAction {
   public:
    uint64_t getHandler() const;
    uint64_t getMask() const;
    uint64_t getFlags() const;
    uint64_t getRestorer() const;
   private:
    uint64_t handler;
    uint64_t mask;
    uint64_t flags;
    uint64_t restorer;
};
```

`handler` 取值：0 = 默认、1 = 忽略、>1 = 用户处理函数地址。

TaskReference 为跨锁、跨阻塞路径持引用；Task 的退出状态保留到 wait 回收。destroy 将清理提交给专用内核 TaskReclaimer，队列持有独立 Task 引用，以 resourceState 防止重复提交；请求线程被杀或取消不影响清理完成。内核工作线程释放线程栈、HandleTable entries、映射引用和地址空间。wait 摘除子进程时，在父进程锁内先提交回收并转移引用，再从全局表移除；等待线程中途死亡也不会遗失回收责任。

exec 的不可逆提交区禁止抢占但保持中断可用，保证已摘除线程和新地址空间的所有权交接完成；同时拒绝重复 exec 提交和对已终止进程重新开放线程创建。exec 与 destroy 先调用 quiesceTask 等待远端 CPU 实际离开旧 Task，再释放页表。fork 在子线程可运行前同步 Ring 3 MM 的 VMA 和 COW 页面状态；地址空间 generation 在 exec 时递增。

make build/tests/kernel-lifecycle.elf 构建独立测试内核，配合 sh tests/smoke-hybrid.sh --lifecycle-test --smp 4 运行。内存回归另用 --memory-test 模式运行，两种模式各自使用隔离镜像。生命周期测试把目标线程固定在 CPU 1、操作线程固定在 CPU 2；包含清理请求发起者被杀、远端持续访问旧页表，以及 exec 提交中并发 kill。生产内核不包含该测试代码。
