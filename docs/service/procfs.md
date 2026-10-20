# procfs、sysfs 与进程工具

FS 在 `src/fs/procfs.cc`、`sysfs.cc` 合成只读节点，虚拟目录不落入磁盘文件系统。目录可只读打开，即使 stat.size 为零也可枚举；条目携带真实 inode 与不透明 nextOffset，libc readdir/getdents 直接推进游标，不识别 `/proc` 特殊路径。

## `/proc`

根目录包含 self 与内核任务列表中的 PID。self 根据实际 FS 请求 senderPid 解析，为该调用者的进程目录别名，目前不是 POSIX 符号链接。目录条目与后续 stat 使用同一 inode 计算；退出进程可能在两次调用间消失，这是正常竞争。

根枚举游标是单调 PID 下界，返回条目后为 PID+1；不会因前面进程退出导致重复、漏掉后续项目或无限循环。单次打开的普通进程文件保存最多 1536 字节内容快照，分段读取不会混合不同时刻的记录。

| 文件 | 格式与权限 |
| --- | --- |
| status | 行格式 `Key:\tValue\n`；0444 |
| comm | 程序 basename 加换行，控制字节转为问号；0444 |
| cmdline | 最多 384 字节，NUL 分隔参数；0400，root 或相同有效 UID 可读 |
| stat | Linux 风格的基础位置字段；CPU/start 以 100 Hz 单位表示，未实现字段为 0；不是完整 Linux proc ABI |

status 字段为 Name、Pid、PPid、Uid/Euid、Gid/Egid、State、Pgid、Sid、TtyKind、TtyIndex、ForegroundPgid、StartNs、CpuNs、ServiceRole、Service。State 使用 TaskState 数值；TTY kind=0 无控制终端，1 为文本终端（index 0 表示 tty1），2 为 PTY（index 0 表示 pts/0）。StartNs 为单调启动纳秒，CpuNs 为任务线程实际调度时间累计，Service 为受监护标记。

fork 复制会话与控制终端，exec 保留身份并更新参数，setsid 清控制终端。新文本会话由 init 指定实际 TTY；PTY 会话通过 tty 的 GetNumber 解析标准通道身份。console/tty 在更改实际前台组时，通过固定角色授权的 SetTerminalForeground 同步内核终端元数据，stat 和 status 据此输出前台组。没有控制终端的进程不会因 stdin 是某文件而被猜测成一个 TTY；主要客户端读取 status，stat 其余未实现字段仍按表中限制解释。

## `/sys`

包含 kernel、cpu、memory 子目录，以及 uptime、meminfo。kernel 下的 ostype/osrelease/hostname，cpu/online 和 memory/total/free 可读取。version 与 hostname 来自启动配置快照，运行统计来自实际内核查询。只读打开目录及尾部斜线可用，写入/创建/改权限不得修改底层被遮蔽的磁盘目录。

## ps 选择与格式

源码在 osinfo-base-tools/coreutils 的 ps 与共享 processes.h。当前用户指有效 UID；选择项合并为并集，`-e/-A` 选择全部，只有没有选择项才应用默认条件。

| 命令 | 选择 |
| --- | --- |
| ps | 同有效 UID、同控制终端；自身无 TTY 时为空集合 |
| ps -a | 有控制终端，排除会话首领 |
| ps -x | 同有效 UID，包含无终端进程 |
| ps -e / -A | 全部 |
| ps -ef / -e -f | 全部、完整格式 |
| ps -t pts/0 | 指定终端，支持 /dev/pts/0、tty1、/dev/tty1 |

默认显示 PID TTY TIME CMD；完整显示 UID PID PPID C STIME TTY TIME CMD。CPU 是累计 CPU/存活时间的百分比；STIME 根据墙钟和单调启动时刻推算。账号查询失败用数字 UID；参数中的控制字符转义为问号，长参数有界截断。

pkill/killall/who 也从 procfs 读取；批量信号工具先快照全部目标再投递，避免把刚恢复的同名 shell 纳入本次操作。PID 单调分配，普通信号仍检查 UID 和服务保护。进程读取中途消失跳过，真正的目录读取错误使工具失败。

验证包括 `ls /sys`、`ls -l /sys`、嵌套目录、`ls /proc`、self、全部 ps 选项和持续创建退出竞争。测试入口为 `tests/service-runtime.sh`；控制台/PTY 会话恢复的结果单独记录。
