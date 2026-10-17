# 进程间通信（src/kernel/task/ipc/）

本文档描述 `src/kernel/task/ipc/` 的实现：同步端点消息传递（`ipc.cc`）、匿名管道和通用双向通道（`pipe.cc`）与 POSIX 命名消息队列（`mqueue.cc`）。对应头文件 `include/kernel/ipc.h`。

所有 IPC 对象都通过进程句柄表访问（见 [task.md](task.md)），句柄类型为 `Endpoint`、`Pipe`、`Channel`、`MessageQueue`。

## 1. 消息格式（IpcMessage）

```cpp
constexpr uint32_t ipcDataWordCount = 515;
struct IpcMessage {
    uint64_t type;
    uint64_t senderPid;     // 内核强制填写，用户不可伪造
    uint64_t senderTid;
    uint32_t length;        // data 有效字数，≤ 515
    uint32_t handleCount;   // ≤ 4
    uint64_t data[515];     // 内联数据，约 4 KB
    uint64_t handles[4];    // 传递的句柄
};
```

`IpcMessage` 是 `os::IpcMessage` 的类型别名。

## 2. 端点（ipc.cc）

微内核风格的同步消息传递与 RPC。所有方法为 `Endpoint` 类的实例方法或静态方法。

### 结构

```cpp
constexpr uint32_t endpointRightSend    = 1U << 0;
constexpr uint32_t endpointRightReceive = 1U << 1;
constexpr uint32_t endpointRightManage  = 1U << 2;

class Endpoint {
   public:
    static void init();
    static Endpoint* create();
    static uint64_t duplicateHandle(Task* source, Task* destination,
                                    uint64_t handle,
                                    uint64_t destinationHandle = 0);
    int send(IpcMessage* message);
    int receive(IpcMessage* message);
    int reply(IpcMessage* message);
    int call(IpcMessage* request, IpcMessage* reply);
    bool grantAccess(Task* task, uint32_t rights);
    void destroy();
    bool notifyFromInterrupt(uint64_t type, uint64_t value);

    uint64_t getId() const;
    Task* getOwner() const;
    // setter ...
   private:
    uint64_t id;
    Task* owner;
    EndpointImplementation* implementation;
};
```

`EndpointImplementation`（内部）：`SpinLock`、8 槽环形消息队列（`queueCapacity = 8`）、收发等待者链表、挂起调用链表、访问授权链表、destroyed 标志、`pendingIrqs` 16 位集合。

授权记录按需分配并复用已退出任务的记录，不再限制为 16 个任务；再次传递同一端点会合并已有权限。

硬件 IRQ 通知保存在单独的 16 位待处理集合中，同一 IRQ 合并，receive 取出一个 IRQ；普通消息队列满不会丢失 IRQ。

### 关键行为

- **权限**：owner 进程拥有全部权限；其他进程需在授权表（grants）中有对应权利。`grantAccess` 需 Manage 权限。
- **句柄传递**（`transferHandles`）：仅 Endpoint/Task/Pipe/Channel 四类可跨进程传递。Endpoint 先对接收方授权；Pipe/Channel 用 `Pipe::duplicateHandle` 生成新句柄；消息中的句柄值被改写为接收方句柄。
- **`send`**：异步发送；队列满则阻塞发送者。
- **`receive`**：需 Receive 权限；空则阻塞；优先取 pendingIrqs（类型 `0x495251` = ASCII "IRQ"）；取到消息时记录 `pendingCall->server = 当前线程`（供 reply 配对）；唤醒一个发送者。
- **`call`**：同步 RPC。栈上构造 `PendingCall`，设置 `activeIpcCall`，入队后阻塞等待 `completed`，返回结果。
- **`reply`**：服务端回复。在挂起调用链表中查找 `server == 当前线程` 的调用；句柄反向转移给调用者进程；写回复目标、置完成、唤醒调用者。
- **`destroy`**：需 Manage 权限；唤醒所有等待者；所有挂起调用置失败并唤醒。
- **`notifyFromInterrupt`**：中断上下文可用的非阻塞通知；IRQ 类型（`0x495251`）走 pendingIrqs 位集合；其余类型队列满则丢弃；构造 `length=1` 消息；唤醒接收者。

## 3. 匿名管道（pipe.cc）

16 KB 环形缓冲（`pipeCapacity = 16 * 1024`）。所有方法为 `Pipe` 类的静态方法。

```cpp
class Pipe {
   public:
    static bool createHandles(Task& task, uint64_t& readHandle,
                              uint64_t& writeHandle);
    static uint64_t duplicateHandle(Task& source, Task& destination,
                                    uint64_t handle,
                                    uint64_t destinationHandle = 0);
    static int64_t read(Task& task, uint64_t handle, void* buffer, size_t size);
    static int64_t write(Task& task, uint64_t handle, const void* buffer,
                         size_t size);
    static void closeHandleObject(void* object);
};
```

内部结构：

```cpp
struct PipeState {  // 共享的管道本体
    SpinLock lock; uint8_t buffer[16384];
    uint32_t head, tail, size;
    uint32_t readers, writers;
    Thread* readerWaiters; Thread* writerWaiters;
};
struct PipeEnd {   // 每个句柄一个
    PipeState* pipe; bool readable; bool closed;
};
```

语义：

- 读端权限 `handleRightRead`，写端 `handleRightWrite`。
- **读**：有数据 → 拷贝并唤醒写者；`writers == 0` → 返回 0（EOF）；空则阻塞。
- **写**：`readers == 0` → 返回 **-11**（IoError，broken pipe）；有空位 → 部分写入并唤醒读者；满则阻塞。
- `duplicateHandle` 递增 readers/writers 计数；`closeHandleObject` 幂等，递减计数并唤醒对侧全部等待者。

## 4. 通用双向通道（pipe.cc）

`Pipe::createChannelPair` 创建两个可双向读写的端点。每个端点的读、写方向分别由匿名管道缓冲承载；句柄复制和 IPC 传递保留端点标识与共享的非阻塞设置。`ReadPipe`/`WritePipe` 根据句柄类型处理普通管道或双向通道。`GetChannelIdentity` 提供跨进程传递后仍稳定的端点标识，`SetChannelNonblocking` 设置同一端点的非阻塞模式。

内核不再维护 PTY 行缓冲、终端属性或前台进程组；这些状态与 Ctrl+C/Ctrl+Z 等控制字符处理位于 [tty 服务](../../service/tty.md)。
## 5. 命名消息队列（mqueue.cc）

POSIX `mq_open` 风格，静态全局表：16 个队列、64 个句柄描述符、每队列最多 16 条消息、单消息 ≤256 字节。所有方法为 `MessageQueue` 类的静态方法。

```cpp
class MessageQueueAttributes {
   public:
    MessageQueueAttributes() = default;
    uint64_t getFlags() const;
    // ...
   private:
    uint64_t flags;
    uint64_t maximumMessages;
    uint64_t messageSize;
    uint64_t currentMessages;
};

class MessageQueue {
   public:
    static uint64_t open(Task& task, const char* name, int flags,
                         uint32_t maximumMessages, uint32_t messageSize);
    static uint64_t duplicateHandle(Task& source, Task& destination,
                                    uint64_t handle,
                                    uint64_t destinationHandle = 0);
    static bool unlink(const char* name);
    static int send(Task& task, uint64_t handle, const void* data, size_t size,
                    uint32_t priority);
    static int64_t receive(Task& task, uint64_t handle, void* data,
                           size_t capacity, uint32_t& priority);
    static bool getAttributes(Task& task, uint64_t handle,
                              MessageQueueAttributes& attributes);
    static bool setFlags(Task& task, uint64_t handle, uint64_t flags,
                         uint64_t& oldFlags);
    static int registerNotification(Task& task, uint64_t handle, int type,
                                    uint32_t signal);
    static void closeHandleObject(void* object);
};
```

语义：

- 名字必须以 `/` 开头；支持 `O_CREAT`（0x40）、`O_EXCL`（0x80）、`O_NONBLOCK`（0x800）。
- 创建时校验 `1 ≤ maximumMessages ≤ 16`、`1 ≤ messageSize ≤ 256`。
- **发送**：`priority ≤ 31`；`size ≤ messageSize`；有空位则追加；队列由空变非空且已注册通知 → 向 `notificationTask` 投递 `notificationSignal`（一次性）；满时非阻塞返回 1，否则阻塞。
- **接收**：选取**优先级最高**的消息（相同优先级取先入者）；唤醒一个发送者；空时非阻塞返回 -2，否则阻塞；返回消息字节数。
- **通知注册**：type 0=注销、1=注册（无信号）、2=注册并发信号；已被注册返回 1。
- `unlink` 清链接；`references == 0` 时回收队列。
