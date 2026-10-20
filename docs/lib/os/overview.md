# os 运行时库总览

os 库是用户态的核心运行时，源码位于 `lib/os/`，头文件位于 `include/os-c++/os/`，源码通过 `#include <os/xxx.h>` 引用。它提供堆分配器、IPC 客户端、系统调用封装，并生成共享库 `libos-c++.so`。GUI 接口由独立的 `lib/osgui/` 实现，头文件位于 `include/osgui-c++/osgui/`，通过 `#include <osgui/xxx.h>` 引用。

## 1. 分层结构

```text
应用程序 / 服务（main / userMain）
    ↓
libc / libc++（POSIX 与 C++ 语义）
    ↓
libc 内部适配（lib/libc/os_adapter.cc，不安装头文件）
    ↓
os:: C++ 运行时（runtime.cc 等）
    ↓
系统调用（syscall 指令，19 个）+ 端点 IPC（各服务）
```

## 2. 源码文件

| 文件 | 内容 | 文档 |
|---|---|---|
| `runtime.cc` | 系统调用封装、堆分配器、终端客户端、运行时初始化 | [runtime.md](runtime.md) |
| `service.cc` | 服务注册中心客户端 | [clients.md](clients.md) |
| `task.cc` | task 服务客户端（进程启动/等待/列表） | [clients.md](clients.md) |
| `file.cc` | fs 服务客户端（文件操作） | [clients.md](clients.md) |
| `gui.cc` | guiw 服务客户端 | [clients.md](clients.md) |
| `input.cc` | input 服务客户端（`InputClient`） | [clients.md](clients.md) |
| `pty.cc` | tty 服务 IPC 客户端 | [runtime.md](runtime.md) |
| `start.S` | 普通程序 `_start` 入口 | [c_api.md](c_api.md) |
| `src/service/start.S`（库目录外） | 服务程序 `_start` 入口，CS=3 检查后调用 main | [c_api.md](c_api.md) |

## 3. 头文件

| 头文件 | 内容 |
|---|---|
| `include/os-c++/os/abi.h` | 内核 ABI 总定义：系统调用号、错误码、IpcMessage、程序 id、各结构体 |
| `include/os-c++/os/protocol.h` | 各服务 IPC 请求枚举与常量 |
| `include/os-c++/os/service.h` | 服务发现 |
| `include/os-c++/os/file.h` | 文件系统客户端 API |
| `include/os-c++/os/task.h` | 进程客户端 API |
| `include/os-c++/os/terminal.h` | 终端客户端 |
| `include/os-c++/os/input.h` | 输入协议与 `InputClient` |
| `include/osgui-c++/osgui/gui.h` | 窗口协议 |
| `include/os-c++/os/pty.h` | 伪终端结构 |
| `include/os-c++/os/storage.h` | 块设备协议类型 |
| `include/os-c++/os/sync.h` | `os::SpinLock` / `SpinLockGuard` |
| `include/osgui-c++/osgui/font.h` | 8×16 控制台位图字体 |

## 4. 关键约定

- **系统调用约定**：号在 `rax`，参数 `rdi/rsi/rdx/r10/r8/r9`，返回 `rax`；错误为负 `os::ErrorCode`。
- **错误码**（`os::ErrorCode`）：Success=0、InvalidArgument=-1、InvalidHandle=-2、PermissionDenied=-3、NotFound=-4、OutOfMemory=-5、WouldBlock=-6、NotSupported=-7、Busy=-8、Timeout=-9、Interrupted=-10、IoError=-11。
- **用户堆**：基址 `0x0000000010000000`，线性分配器，最多 4096 个活动分配，每次分配后留 4 KB 保护页。
- **终端参数**：`--os-terminal=N`（N=1..7）由 `runtimeInit` 解析并从 argv 移除。
- **同步根基**：futex 式 `waitAddress`/`wakeAddress` + `__atomic_*`；`os::SpinLock` 为 test-and-set + pause。

Ring3 硬件服务使用的 Ring0 机制见 [device.md](../../service/device/README.md)。File 方法使用负 errno；底层原生 ABI 的其他接口仍使用 os::ErrorCode。

OS 非 syscall 接口采用 Process、Terminal、File、Handle、Clock、Memory、VirtualMemory、ServiceRegistry 等类。VirtualMemory 的用户内存策略由 Ring 3 MM 提供；memory 机制对象驻留 Ring 0。

应用数据、状态、配置与缓存路径通过 [Directories 类](directories.md) 查询；完整支持 XDG 环境覆盖和搜索目录。应用的 ELF 加载与构建分别见 [用户态动态链接](../../x86-64/dynamic-linker.md) 和 [Info IDE / SDK](../../app/infoide.md)。
