# IPC 子系统（src/kernel/task/ipc/）

本文档描述 `src/kernel/task/ipc/` 的实现：同步端点消息传递（`ipc.cc`）、匿名管道（`pipe.cc`）、伪终端（`pty.cc`）与 POSIX 命名消息队列（`mqueue.cc`）。对应头文件 `include/kernel/ipc.h`、`pipe.h`、`pty.h`、`mqueue.h`。

所有 IPC 对象都通过进程句柄表访问（见 [task.md](task.md)），句柄类型为 `Endpoint`、`Pipe`、`Pty`、`MessageQueue`。

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

## 2. 端点（ipc.cc）

微内核风格的同步消息传递与 RPC。

### 结构

```cpp
constexpr uint32_t endpointRightSend    = 1U << 0;
constexpr uint32_t endpointRightReceive = 1U << 1;
constexpr uint32_t endpointRightManage  = 1U << 2;

struct Endpoint {
    uint64_t id;
    Process* owner;
    EndpointImplementation* implementation;
};
```

`EndpointImplementation`（内部）：`SpinLock`、8 槽环形消息队列（`queueCapacity = 8`）、收发等待者链表、挂起调用链表、16 项访问授权表（`grantCapacity = 16`）、destroyed 标志。

### 接口

```cpp
void initIpc();
Endpoint* createEndpoint();
void destroyEndpoint(Endpoint* endpoint);
bool grantEndpointAccess(Endpoint*, Process*, uint32_t rights);
uint64_t duplicateEndpointHandle(Process& src, Process& dst, uint64_t handle, uint64_t dstHandle = 0);
int sendMessage(Endpoint*, IpcMessage* message);
int receiveMessage(Endpoint*, IpcMessage* message);
int replyMessage(Endpoint*, IpcMessage* message);
int callService(Endpoint*, IpcMessage* request, IpcMessage* reply);
bool notifyEndpointFromInterrupt(Endpoint*, uint64_t type, uint64_t value);
```

### 关键行为

- **权限**：owner 进程拥有全部权限；其他进程需在授权表（grants）中有对应权利。`grantEndpointAccess` 需 Manage 权限。
- **句柄传递**（`transferHandles`）：仅 Endpoint/Process/Pipe/Pty 四类可跨进程传递。Endpoint 先对接收方授权；Pipe/Pty 用各自 duplicate 生成新句柄；消息中的句柄值被改写为接收方句柄。
- **`sendMessage`**：异步发送；队列满则阻塞发送者。
- **`receiveMessage`**：需 Receive 权限；空则阻塞；取到消息时记录 `pendingCall->server = 当前线程`（供 reply 配对）；唤醒一个发送者。
- **`callService`**：同步 RPC。栈上构造 `PendingCall`，入队后阻塞等待 `completed`，返回结果。
- **`replyMessage`**：服务端回复。在挂起调用链表中查找 `server == 当前线程` 的调用；句柄反向转移给调用者进程；写回复目标、置完成、唤醒调用者。
- **`destroyEndpoint`**：需 Manage 权限；唤醒所有等待者；所有挂起调用置失败并唤醒。
- **`notifyEndpointFromInterrupt`**：中断上下文可用的非阻塞通知；队列满则丢弃；构造 `length=1` 消息；唤醒接收者。用于设备中断通知（消息类型如 `0x495251` "IRQ"）。

## 3. 匿名管道（pipe.cc）

16 KB 环形缓冲（`pipeCapacity = 16 * 1024`）。

```cpp
struct Pipe {      // 共享的管道本体
    SpinLock lock; uint8_t buffer[16384];
    uint32_t head, tail, size;
    uint32_t readers, writers;
    Thread* readerWaiters; Thread* writerWaiters;
};
struct PipeEnd {   // 每个句柄一个
    Pipe* pipe; bool readable; bool closed;
};
```

接口：

```cpp
bool createPipeHandles(Process&, uint64_t& readHandle, uint64_t& writeHandle);
uint64_t duplicatePipeHandle(Process& src, Process& dst, uint64_t handle, uint64_t dstHandle = 0);
int64_t readPipeHandle(Process&, uint64_t handle, void* buffer, size_t size);
int64_t writePipeHandle(Process&, uint64_t handle, const void* buffer, size_t size);
void closePipeHandleObject(void* object);
```

语义：

- 读端权限 `handleRightRead`，写端 `handleRightWrite`。
- **读**：有数据 → 拷贝并唤醒写者；`writers == 0` → 返回 0（EOF）；空则阻塞。
- **写**：`readers == 0` → 返回 **-11**（IoError，broken pipe）；有空位 → 部分写入并唤醒读者；满则阻塞。
- `duplicate` 递增 readers/writers 计数；`close` 幂等，递减计数并唤醒对侧全部等待者。

## 4. 伪终端（pty.cc）

master/slave 对，含行规程。每侧 16 KB 环形缓冲（input/output），4 KB 行编辑缓冲。

```cpp
struct Pty {
    SpinLock lock;
    RingBuffer input, output;
    uint8_t line[4096]; uint32_t lineLength;
    uint32_t masterReferences, slaveReferences;
    uint64_t foregroundProcessGroup;
    bool endOfFile;
    os::TerminalAttributes attributes;
    os::TerminalWindowSize windowSize;
    Thread* inputReaders/inputWriters/outputReaders/outputWriters;
};
struct PtyEnd { Pty* pty; bool master; bool nonblocking; bool closed; };
```

默认终端属性：规范模式、回显、信号模式开启；erase=0x7f、interrupt=0x03（Ctrl-C）、eof=0x04（Ctrl-D）、suspend=0x1a（Ctrl-Z）、quit=0x1c（Ctrl-\）。默认窗口 25×80（640×400 像素）。

数据流向：

- **master 读** ← output 环（slave 写入的内容）。
- **slave 读** ← input 环（经行规程处理后的输入）。
- **slave 写** → output 环（原样）。
- **master 写** → 逐字节过行规程 → input 环。

行规程（master 写时）：

- **信号模式**：命中中断/挂起/退出字符 → 生成 `Signal::Interrupt`/`TerminalStop`/`Quit` 发往前台进程组，清空行缓冲，回显 `^X\n`；信号在锁外投递。
- **规范模式**：erase/BS 删除行尾字符；EOF 字符 → 行非空则 flush，否则置 `endOfFile`；普通字符入行缓冲并回显，遇 `\n`/`\r` flush 整行。
- **非规范模式**：直接入 input 环并回显。

接口（`pty.h`）：`createPtyHandles`、`duplicatePtyHandle`、`readPtyHandle`/`writePtyHandle`、`getPtyAttributes`/`setPtyAttributes`（Manage）、`getPtyWindowSize`/`setPtyWindowSize`（Manage，成功后向前台进程组发 `Signal::WindowChanged`）、`setPtyForegroundGroup`（Manage）、`setPtyNonblocking`（Manage）、`closePtyHandleObject`。

slave 读 EOF：`endOfFile && 缓冲空` → 返回 0 并清除 EOF 标志（一次性）。非阻塞空读返回 -6（WouldBlock）。

## 5. 命名消息队列（mqueue.cc）

POSIX `mq_open` 风格，静态全局表：16 个队列、64 个句柄描述符、每队列最多 16 条消息、单消息 ≤256 字节。

```cpp
struct Message { uint32_t size; uint32_t priority; uint8_t data[256]; };
struct MessageQueue {
    bool used, linked; char name[64];
    uint32_t references, maximumMessages, messageSize, count;
    Message messages[16];
    Thread* senders/sendersTail; Thread* receivers/receiversTail;
    uint64_t notificationProcess; uint32_t notificationSignal;
    bool notificationRegistered; MessageQueueHandle* notificationHandle;
    SpinLock lock;
};
struct MessageQueueHandle { bool used; bool nonblocking; MessageQueue* queue; };
```

接口（`mqueue.h`）：

```cpp
uint64_t openMessageQueue(Process&, const char* name, int flags, uint32_t maximumMessages, uint32_t messageSize);
uint64_t duplicateMessageQueueHandle(Process& src, Process& dst, uint64_t handle, uint64_t dstHandle = 0);
bool unlinkMessageQueue(const char* name);
int sendMessageQueue(Process&, uint64_t handle, const void* data, size_t size, uint32_t priority);
int64_t receiveMessageQueue(Process&, uint64_t handle, void* data, size_t capacity, uint32_t& priority);
bool getMessageQueueAttributes(Process&, uint64_t handle, MessageQueueAttributes& attributes);
bool setMessageQueueFlags(Process&, uint64_t handle, uint64_t flags, uint64_t& oldFlags);
int registerMessageQueueNotification(Process&, uint64_t handle, int type, uint32_t signal);
void closeMessageQueueHandleObject(void* object);
```

语义：

- 名字必须以 `/` 开头；支持 `O_CREAT`（0x40）、`O_EXCL`（0x80）、`O_NONBLOCK`（0x800）。
- 创建时校验 `1 ≤ maximumMessages ≤ 16`、`1 ≤ messageSize ≤ 256`。
- **发送**：`priority ≤ 31`；`size ≤ messageSize`；有空位则追加；队列由空变非空且已注册通知 → 向 `notificationProcess` 投递 `notificationSignal`（一次性）；满时非阻塞返回 1，否则阻塞。
- **接收**：选取**优先级最高**的消息（相同优先级取先入者）；唤醒一个发送者；空时非阻塞返回 -2，否则阻塞；返回消息字节数。
- **通知注册**：type 0=注销、1=注册（无信号）、2=注册并发信号；已被注册返回 1。
- `unlink` 清链接；`references == 0` 时回收队列。
