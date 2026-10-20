# tty：PTY 与行规程

源码 `src/tty/main.cc`，Ring3、Tty 角色，注册名 `tty`。init 从 `/etc/services` 指定的 `/sbin/sys/tty.elf` 加载它，依赖 users；console 依赖 tty。注册服务成功后才算就绪，普通程序通过 `os::Pty` 访问。

## 数据路径与协议

每个 PTY 使用两对 pipe 双向通道。tty 持有服务端，创建请求向客户端转移 master/slave 端句柄；数字句柄不是 PTY 身份，服务使用内核通道 identity 查找对象。

| TtyRequest | 参数 | 结果 |
| --- | --- | --- |
| CreatePty=1 | 无 | 状态、master/slave 两个句柄 |
| GetAttributes=2 / SetAttributes=3 | 一个通道能力；设置时 data 为 TerminalAttributes | 状态及属性 |
| GetWindowSize=4 / SetWindowSize=5 | 通道能力；行列和像素大小 | 状态及尺寸 |
| SetForegroundGroup=6 | 通道能力、data[0]=进程组 | 状态 |
| SetNonblocking=7 | 通道能力、data[0]=布尔值 | 状态 |
| GetNumber=8 | 通道能力 | 状态、稳定槽位号 pts/N |

所有非创建请求必须传一个属于活动 PTY 的能力。窗口大小和行规程不是内核专用 syscall；同一通道端的非阻塞设置由所有继承引用共享。

规范模式缓冲最多 4096 字节，处理回显、擦除、EOF；非规范模式逐字节转发。Ctrl+C/Z/反斜杠分别向前台组发 SIGINT/TSTP/QUIT，空行 Ctrl+D 产生一次性 EOF。终端和 shell 用同一实际 PTY 编号设置控制终端元数据。

## 线程与回收

最多八个活动 PTY，每个两个 64 KiB 栈的工作线程。服务端通道设为非阻塞；工作线程有关闭标记，短暂等待后继续读取，写入处理部分成功和 WouldBlock。任一端读到持久 EOF 或不可恢复错误后标记关闭、通知对端；主循环等待两个线程均退出并 join 后才关闭服务端句柄、取消栈映射并释放槽位。

已退出线程的栈不能在 join 前复用。创建失败关闭客户端句柄，已创建工作线程沿相同关闭流程退出。关闭的 PTY 不占用永久递增的槽位；`tests/service-runtime.cc` 的 20 轮创建/关闭覆盖超出八槽的重复使用。

## 会话与重启

GUI 终端是普通应用，读取 `/etc/sessions`。shell 异常退出后终端关闭旧 PTY，回收子任务，再使用 recovery 登录入口创建新 PTY；正常 exit 保留结束标签。窗口主动关闭不会由 shell 监护当成故障。认证完成前不能恢复为原先的高权限 shell。

停止 tty 前需先停 console/guiw 等依赖服务；systemctl 检查依赖。tty 进程重启会丢失原有行规程、PTY 和队列，旧客户端必须重新建立会话；不能把重新注册当作通道恢复。诊断看 `[TTY]`、`/proc/<pid>/status` 的 TtyKind/TtyIndex、systemctl status tty。
