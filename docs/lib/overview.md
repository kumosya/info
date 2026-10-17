# 用户库总览（lib/）

`lib/` 目录包含两个用户态库，与 `lib/os/` 的 os 运行时共同构成用户态软件栈：

```text
应用程序
  ↓
libc++（lib/libc++，C++ 标准库子集）
  ↓
libc（lib/libc，POSIX C 库）
  ↓
os 运行时（lib/os，文档见 lib/os/）
  ↓
系统调用 + 服务 IPC
```

| 库 | 源码 | 文档 |
|---|---|---|
| os 运行时 | `lib/os/` | [os/overview.md](os/overview.md) |
| libc | `lib/libc/` | [libc/libc.md](libc/libc.md) |
| libc++ | `lib/libc++/` | [libc++/libcxx.md](libc++/libcxx.md) |

动态链接时四个共享库一并加载（固定基址，见 [../x86-64/user.md](../x86-64/user.md)）：

```text
/usr/lib/libos-c++.so   （os 运行时）
/usr/lib/libc.so
/usr/lib/libc++.so
/usr/lib/libosgui-c++.so
```

SDK 头文件按类别安装，源码引用使用类别目录下面的命名空间路径：

| 库 | 源码引用 | 仓库路径 | 系统安装路径 |
|---|---|---|---|
| libos-c++ | `#include <os/abi.h>` | `include/os-c++/os/abi.h` | `/usr/include/os-c++/os/abi.h` |
| libosgui-c++ | `#include <osgui/gui.h>` | `include/osgui-c++/osgui/gui.h` | `/usr/include/osgui-c++/osgui/gui.h` |

编译应用时添加 `-I/usr/include/os-c++ -I/usr/include/osgui-c++`；在仓库中编译则使用对应的 `include/os-c++` 和 `include/osgui-c++` 根目录。库名和软件包名保持 `libos-c++`、`libosgui-c++`。
