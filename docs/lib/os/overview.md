# os 运行时库总览

os 库是用户态的核心运行时，源码位于 `src/user/runtime/`，头文件位于 `include/os/`。它封装全部系统调用，提供堆分配器、IPC 客户端（终端/文件/进程/窗口/输入）、C ABI 桥，并以共享库 `libos-c++.so` 形式被所有用户程序动态链接。

## 1. 分层结构

```text
应用程序 / 服务（main / userMain）
    ↓
libc / libc++（POSIX 与 C++ 语义）
    ↓
os_* C ABI（include/os/c_api.h，c_api.cc 实现）
    ↓
os:: C++ 运行时（runtime.cc 等）
    ↓
系统调用（syscall 指令，73 个）+ 端点 IPC（各服务）
```

## 2. 源码文件

| 文件 | 内容 | 文档 |
|---|---|---|
| `runtime.cc` | 系统调用封装、堆分配器、终端客户端、运行时初始化 | [runtime.md](runtime.md) |
| `service.cc` | 服务注册中心客户端 | [clients.md](clients.md) |
| `process.cc` | task 服务客户端（进程启动/等待/列表） | [clients.md](clients.md) |
| `file.cc` | fs 服务客户端（文件操作） | [clients.md](clients.md) |
| `gui.cc` | window 服务客户端 | [clients.md](clients.md) |
| `input.cc` | input 服务客户端（`InputClient`） | [clients.md](clients.md) |
| `pty.cc` | 伪终端系统调用封装 | [runtime.md](runtime.md) |
| `c_api.cc` | C ABI 桥（`os_*` 函数，供 libc 使用） | [c_api.md](c_api.md) |
| `start.S` | 普通程序 `_start` 入口 | [c_api.md](c_api.md) |
| `service_start.S` | 服务程序 `_start` 入口（`userMain`） | [c_api.md](c_api.md) |

## 3. 头文件

| 头文件 | 内容 |
|---|---|
| `include/os/abi.h` | 内核 ABI 总定义：系统调用号、错误码、IpcMessage、程序 id、各结构体 |
| `include/os/c_api.h` | 纯 C 接口（`os_*` 函数与 C 结构体） |
| `include/os/protocol.h` | 各服务 IPC 请求枚举与常量 |
| `include/os/service.h` | 服务发现 |
| `include/os/file.h` | 文件系统客户端 API |
| `include/os/process.h` | 进程客户端 API |
| `include/os/terminal.h` | 终端客户端 |
| `include/os/input.h` | 输入协议与 `InputClient` |
| `include/os/gui.h` | 窗口协议 |
| `include/os/pty.h` | 伪终端结构 |
| `include/os/storage.h` | 块设备协议类型 |
| `include/os/sync.h` | `os::SpinLock` / `SpinLockGuard` |
| `include/os/font.h` | 8×16 控制台位图字体 |

## 4. 关键约定

- **系统调用约定**：号在 `rax`，参数 `rdi/rsi/rdx/r10/r8/r9`，返回 `rax`；错误为负 `os::ErrorCode`。
- **错误码**（`os::ErrorCode`）：Success=0、InvalidArgument=-1、InvalidHandle=-2、PermissionDenied=-3、NotFound=-4、OutOfMemory=-5、WouldBlock=-6、NotSupported=-7、Busy=-8、Timeout=-9、Interrupted=-10、IoError=-11。
- **用户堆**：基址 `0x0000000010000000`，线性分配器，最多 64 个活动分配，每次分配后留 4 KB 保护页。
- **终端参数**：`--os-terminal=N`（N=1..7）由 `runtimeInit` 解析并从 argv 移除。
- **同步根基**：futex 式 `waitAddress`/`wakeAddress` + `__atomic_*`；`os::SpinLock` 为 test-and-set + pause。
