# os 运行时核心（runtime.cc / pty.cc）

本文档描述 `src/user/runtime/runtime.cc` 与 `pty.cc`：系统调用封装、用户堆分配器、终端 IPC 客户端、运行时初始化与伪终端封装。全部位于 `namespace os`。

## 1. 系统调用封装

核心函数：

```cpp
int64_t invokeSyscall(SyscallNumber number, uint64_t arg1, ..., uint64_t arg6);
```

使用 `syscall` 指令内联汇编：号在 `rax`，参数 `rdi/rsi/rdx/r10/r8/r9`，返回 `rax`，clobber `rcx, r11, memory`。返回值强转 `int64_t`（负数为 `ErrorCode`）。

封装的调用按类别（均为薄包装）：

- **进程/线程**：`exitProcess`（noreturn）、`exitThread`（noreturn）、`createThread`、`waitThread(threadId, exitCode&, noHang)`、`forkProcess`、`waitChild(pid, status&, options)`、`replaceProcessImage(ExecImageInfo&)`、`spawnProgram`、`spawnImage`、`getProcessState`、`getProcessInfo`、`terminateProcess`、`sendSignal`、`createSession`、`getProcessIdentifiers`、`getProcessId`、`getThreadId`、`yield`、`sleep(ms)`。
- **futex 同步**：`waitAddress(volatile uint32_t*, expected)`、`wakeAddress(volatile uint32_t*, count)`。
- **信号**：`setSignalAction`、`setSignalMask(how, mask, oldMask&)`、`getPendingSignals(mask&)`、`waitSignal(mask)`、`signalReturn(frame)`（noreturn）、`sendThreadSignal`。
- **内存**：`mapMemory`、`unmapMemory`、`protectMemory`、`allocateMemory`、`freeMemory`。
- **IPC**：`createEndpoint`、`sendMessage`、`receiveMessage`、`callService(endpoint, request, reply)`、`replyMessage`。
- **管道/句柄**：`createPipe(PipeHandles&)`、`readPipe`、`writePipe`、`getStandardHandle(index)`、`getBootstrapHandle`、`closeHandle`。
- **消息队列**：`openMessageQueue`、`unlinkMessageQueue`、`sendMessageQueue`、`receiveMessageQueue`、`getMessageQueueAttributes`、`setMessageQueueFlags`、`registerMessageQueueNotification`。
- **共享内存**：`openSharedMemory`、`unlinkSharedMemory`、`resizeSharedMemory`、`mapSharedMemory`。
- **时钟**：`getMonotonicTime`、`getWallClockTime`、`getClockResolution`。
- **驱动/硬件**：`enumeratePci`、`readPciConfig`、`writePciConfig`、`mapDeviceMmio`、`allocateDma`、`bindIrq`、`readIoPort`、`writeIoPort`、`mapFramebuffer`。
- **其它**：`log(message)`、`posixUnsupported(PosixOperation)`。

## 2. 用户堆分配器

常量：`heapBase = 0x0000000010000000`、`pageSize = 4096`、`allocationCapacity = 64`。

```cpp
struct Allocation { uint64_t address; uint64_t size; };
Allocation allocations[64];
uint64_t nextAllocationAddress = heapBase;
uint8_t heapLock;   // test-and-set 自旋锁
```

- **`allocateMemory(size)`**：大小页对齐；加锁找空槽；`mapMemory(nextAllocationAddress, mappedSize, 1)`（保护位 1=可写）；记录 `{address, mappedSize}`；`nextAllocationAddress` 前移 `mappedSize + pageSize`（每次分配留一个 4 KB 保护页）。只进不退的线性分配器。
- **`freeMemory(ptr)`**：按地址精确匹配，清表项并 `unmapMemory`。不合并；槽位可复用但地址空间不回收重用。
- 最多 64 个活动分配；超出或映射失败返回 `nullptr`。

libc 的 `malloc` 在此基础上加分配头（见 libc 文档）。

## 3. 管道读写

- `readPipe`：单次传输上限 512 字节。
- `writePipe`：以 512 字节为块循环写入，返回累计写入字节数；出错返回已写字节数或错误码。

## 4. 终端客户端（经 IPC）

全局 `terminalEndpoint`（懒加载：`lookupService(serviceRegistry(), terminalServiceName, 1000)`）与 `terminalId`。

- **`writeTerminal(buffer, size)`**：按每块最多 48 字节（6 个 uint64）拆分，发送 `TerminalRequest::Write`（data[0]=终端 id，data[1]=块长，数据内联 data[2..]），循环直到写完。
- **`readTerminal(buffer, size)`**：发送 `TerminalRequest::Read`；回复 `WouldBlock` 则 `sleep(1)` 重试（阻塞式轮询）；成功拷入缓冲区，单次最多 48 字节。
- `getActiveTerminal` / `switchTerminal(id)` / `setTerminalForegroundProcess(pid)`。
- `printString`：`writeTerminal` 包装。
- `getTerminalId/setTerminalId`：id 限制在 `textTerminalCount` 以内。

## 5. 运行时初始化/收尾

- **`runtimeInit(argc, argv, envp)`**：清零全局状态；扫描参数中的 `--os-terminal=N`（N='1'..'7'，存为 `terminalId = N-1`），找到后从 argv 移除（参数左移、`--argc`、末尾置 NULL）；遍历 `__init_array_start..__init_array_end` 调用全部 C++ 全局构造函数；返回（可能减少的）`argc`。
- **`runtimeFini()`**：从 `__fini_array_end` 到 `__fini_array_start` 逆序调用全局析构函数。
- 工具：`stringLength`、`stringsEqual`、`copyString`（带容量安全拷贝）、`clearMemory`。

## 6. 伪终端封装（pty.cc）

直接系统调用封装（不经 IPC），全部返回 bool：

```cpp
bool createPty(PtyHandles& handles);              // SyscallNumber::CreatePty
bool getPtyAttributes(uint64_t handle, TerminalAttributes& attributes);
bool setPtyAttributes(uint64_t handle, const TerminalAttributes& attributes);
bool getPtyWindowSize(uint64_t handle, TerminalWindowSize& size);
bool setPtyWindowSize(uint64_t handle, const TerminalWindowSize& size);
bool setPtyForegroundGroup(uint64_t handle, uint64_t processGroupId);
bool setPtyNonblocking(uint64_t handle, bool enabled);
```

结构（`include/os/pty.h`）：

```cpp
struct PtyHandles { uint64_t masterHandle; uint64_t slaveHandle; };
struct TerminalAttributes {
    bool canonicalMode, echo, signalMode;
    uint8_t eraseCharacter, interruptCharacter, eofCharacter,
            suspendCharacter, quitCharacter;
};
struct TerminalWindowSize { uint32_t rows, columns, pixelWidth, pixelHeight; };
```
