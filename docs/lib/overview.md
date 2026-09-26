# 用户库总览（lib/）

`lib/` 目录包含两个用户态库，与 `src/user/runtime/` 的 os 运行时共同构成用户态软件栈：

```text
应用程序
  ↓
libc++（lib/libc++，C++ 标准库子集）
  ↓
libc（lib/libc，POSIX C 库）
  ↓
os 运行时（src/user/runtime，文档见 lib/os/）
  ↓
系统调用 + 服务 IPC
```

| 库 | 源码 | 文档 |
|---|---|---|
| os 运行时 | `src/user/runtime/` | [os/overview.md](os/overview.md) |
| libc | `lib/libc/` | [libc/libc.md](libc/libc.md) |
| libc++ | `lib/libc++/` | [libc++/libcxx.md](libc++/libcxx.md) |

动态链接时三个共享库一并加载（固定基址，见 [../x86-64/user.md](../x86-64/user.md)）：

```text
/lib/x86-64-osinfo/libos-c++.so   （os 运行时）
/lib/x86-64-osinfo/libc.so
/lib/x86-64-osinfo/libc++.so
```
