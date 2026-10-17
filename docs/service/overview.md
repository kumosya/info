# 用户态服务总览

`src/` 下除 `kernel/` 与 `user/` 外的目录是运行在 Ring 3 的系统服务。它们通过端点 IPC（`os::IpcMessage`）协作，由内核内置镜像或磁盘 ELF 加载。

## 1. 服务清单

| 服务 | 源码 | 注册名 | 来源 | 角色 |
|---|---|---|---|---|
| init | `src/init/` | —（持有注册表） | 内核内置镜像 | 第一个用户进程，内嵌服务注册表，引导用户态 |
| task | `src/task/` | `task` | 内核内置镜像 | 进程服务器：从磁盘加载 ELF 并创建进程 |
| fs | `src/fs/` | `fs` | 内核内置镜像 | 文件系统服务器（ext3/iso9660） |
| device | `src/device/` | `device`、`input` | 内核内置镜像 | 设备管理 + 键盘/鼠标输入服务 |
| storage | `src/storage/` | `storage` | 内核内置镜像 | 块设备服务（virtio-blk） |
| tty | `src/tty/` | `tty` | 磁盘 `/sbin/sys/tty.elf` | 用户态 PTY 行规程、属性与前台进程组 |
| console | `src/console/` | `console`、`terminal` | 磁盘 `/sbin/sys/console.elf` | 文本终端（tty1-6）+ 图形会话切换 |
| guiw | `src/guiw/` | `guiw` | guiw 软件包 `/sbin/sys/guiw.elf` | 窗口服务器/合成器 |

内核内置镜像由 `SpawnProgram` 启动（见 [../x86-64/user.md](../x86-64/user.md)）；磁盘服务由 task 服务经 `Execute` 从 ext3 根文件系统加载。仅 `console.elf` 与 `guiw.elf` 获得 `taskCapabilityDisplay`（直接帧缓冲访问权）。

## 2. 启动顺序

```text
内核 → init（内置）
         ├─ 启动注册表线程，公布注册表端点
         ├─ spawnProgram: task、device、storage、fs（内置镜像）
         ├─ 等待 fs 服务上线
         ├─ execute: /sbin/sys/tty.elf
         ├─ execute: /sbin/sys/console.elf
         ├─ execute: /sbin/sys/guiw.elf
         ├─ execute: /usr/bin/terminal（GUI 终端）
         ├─ switchTerminal(6)（切到 tty7 图形会话）
         └─ tty1-6 各启动 /bin/sh --os-terminal=N
```

## 3. IPC 基础

所有服务间通信使用 `os::IpcMessage`（定义于 `include/os-c++/os/abi.h`）：

- 内联数据 515 个 `uint64_t` 字（约 4 KB）；
- 最多携带 4 个句柄（端点、进程、管道、双向通道等）；
- `senderPid`/`senderTid` 由内核强制填写。

各协议的最大传输单元由此决定（如文件读 4112 字节、写 4104 字节、异步 4088 字节）。

### 服务发现

注册表在 init 进程内（`RegistryRequest::Register/Lookup`）。客户端接口（`include/os-c++/os/service.h`）：

```cpp
void setServiceRegistry(uint64_t endpoint);
uint64_t serviceRegistry();
bool registerService(uint64_t registry, const char* name, uint64_t endpoint);
uint64_t lookupService(uint64_t registry, const char* name, uint32_t attempts = 100);
```

注册表容量 12 条记录，名字 ≤24 字符（`serviceNameCapacity`）。

### 异步完成通知类型

各服务的异步操作完成通知类型均为 ASCII 串常量：

| 常量 | 值 | 来源 |
|---|---|---|
| `"IRQ"` | `0x495251` | 内核设备中断 |
| `"INPUT"` | `0x494e505554` | device/input 服务 |
| `"STORAGE"` | `0x53544f52414745` | storage 服务 |
| `"FSCMPL"` | `0x4653434f4d504c` | fs 服务 |
| `"WINEVENT"` | `0x57494e4556454e54` | guiw 服务 |

## 4. 协议头文件

| 头文件 | 内容 |
|---|---|
| `include/os-c++/os/protocol.h` | 各服务请求枚举与常量汇总 |
| `include/os-c++/os/service.h` | 服务发现客户端 |
| `include/os-c++/os/file.h` | fs 客户端 API |
| `include/os-c++/os/task.h` | task 客户端 API（execute/wait/list/kill） |
| `include/os-c++/os/storage.h` | 块设备协议类型 |
| `include/os-c++/os/terminal.h` | 终端客户端 |
| `include/os-c++/os/input.h` | 输入协议与 `InputClient` |
| `include/osgui-c++/osgui/gui.h` | 窗口协议 |
| `include/osgui-c++/osgui/font.h` | 8×16 控制台位图字体（console 与 guiw 共用） |

## 5. 文档索引

- [init.md](init.md) — init 服务与服务注册表
- [task.md](task.md) — task 进程服务器
- [console.md](console.md) — console 终端服务
- [device.md](device.md) — device 设备管理与 input 输入服务
- [storage.md](storage.md) — storage 块设备服务
- [fs.md](fs.md) — fs 文件系统服务器与各文件系统驱动
- [tty.md](tty.md) — tty 伪终端服务
- [window.md](window.md) — guiw 窗口服务器
