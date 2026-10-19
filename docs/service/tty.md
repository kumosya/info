# tty 服务（src/tty/）

`tty` 是固定 boot 模块加载的 Ring3 服务，注册名为 `tty`，程序路径为 `/sbin/sys/tty.elf`。init 在启动 console 和 guiw 之前等待它注册。GUI 终端创建 PTY，shell 与 libc 通过 `lib/os/pty.cc` 向 tty 服务发送 IPC 请求。

每对 PTY 使用两组内核通用双向通道。tty 服务保留服务端句柄，通过 IPC 把主端和从端句柄交给调用者。内核只负责通道字节流、句柄传递、标准流继承与 fork 复制；规范模式、回显、擦除、控制字符、窗口大小和前台进程组保存在 tty 服务中。主端输入线程执行行规程并向从端传递输入；从端输出线程把输出送往主端。Ctrl+C、Ctrl+Z、Ctrl+\ 分别向前台进程组发送 SIGINT、SIGTSTP、SIGQUIT。空行上的 Ctrl+D 通过通用通道的一次性 EOF 事件让从端读取返回 0。

`os::TtyRequest` 包含 CreatePty、Get/SetAttributes、Get/SetWindowSize、SetForegroundGroup、SetNonblocking。除创建外，请求通过 IPC 携带一个通道句柄；服务用通用 `GetChannelIdentity` 判断它属于哪一对 PTY。`SetNonblocking` 调用通用 `SetChannelNonblocking`，同一端的继承句柄共享该设置。内核 syscall 表不含 PTY 专用调用。
