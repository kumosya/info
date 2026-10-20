# system.kernel：时间、统计和串口日志

实现 `src/kernel/task/sys/service.cc`，Ring0 驻留 EndpointService。目录归属 task/sys；不位于 src/kernel/system/service。init 通过已有内核对象句柄发布 `system.kernel`。没有独立进程、线程、运行时启动配置或可重启 PID。

## 请求

| SystemOperation | 数据与返回 |
| --- | --- |
| MonotonicTime | data[0] 为启动以来单调纳秒 |
| WallClockTime | data[0] 为墙钟时间 |
| ClockResolution | data[0] 为时钟分辨率 |
| Info | data[1..] 为 SystemInfo：运行时间、总内存、可用内存和在线 CPU 数 |
| Log | 请求 data 为有界 NUL 文本，最大 512 字节；串口输出 |

客户端为 `os::Clock`、`os::System`。系统版本和 hostname 不属于硬件统计对象：FS 从 `/lib/version.1`、`/etc/hostname` 读取并提供 sysfs 节点。

## 权限、锁和寿命

查询不提供设备写能力、任意物理访问或设置系统凭据。Log 验证请求文本长度，不输出调用方指针指向的任意内存。时间与内存信息通过现有 time/memory/smp 实现查询；串口实现有自身中断安全锁。调用同步返回，不保存客户端业务对象或待回复 token。

## 故障和排查

未知操作或非终止文本返回 InvalidArgument。该对象不能经 systemctl 停止；若时间查询失败，检查内核对象发现与 IPC 参数，而非尝试重启独立系统服务。统计值为瞬时采样，不保证跨多次调用构成同一快照。服务运行时间与 ps CPU 时间应以各自单位解释。

关联：[init 注册表](../../init/README.md)、[procfs/sysfs](../../procfs.md)、`src/kernel/device/time.cc` 与串口实现。真实启动、进程信息与服务测试使用 `tests/service-runtime.sh`。
