# POSIX 适配与程序入口

OS SDK 仅提供 C++ 接口，不再安装 `os/c_api.h`，libos-c++ 不再导出原来的 `os_*` C 适配函数。

C 程序使用标准头文件。文件描述符表及必要的 C/C++ 转换位于 libc 内部的 `os_adapter.cc` / `os_adapter.h`，这些头文件不进入 SDK，桥接符号使用隐藏可见性。基础机制使用统一的 Ring 3 syscall ABI；其余操作由 libc 内部连接 OS 类客户端。

`os::File` 保存文件服务端点、原生句柄、状态标志和私有元数据。`File::fromDescriptor(fd)` 返回描述符引用的 File 对象，`toDescriptor()` 把拥有的句柄移交给描述符表；dup 共享同一个对象，最后一个引用关闭时销毁它。

普通程序的 `lib/os/start.S` 解析 argc/argv/envp，初始化运行时及 environ，导入 exec 描述符状态，调用 main，最后调用 libc 的 exit 完成清理。

服务入口 `src/service/start.S` 初始化 C++ 运行时后调用 main，返回后通过 `os::exitTask` 退出；SDK 输出名仍为 service_start.o。CS 检查留在此内部入口，公共 os/ 头文件不包含寄存器或硬件 I/O 实现；基础机制通过 syscall 和 IPC 访问。
