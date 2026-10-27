# system.kernel 系统信息服务

实现位于 `src/kernel/task/sys/service.cc`，入口 `kernel::task::systemKernelService()` 返回绑定处理函数的 EndpointService。服务经统一 CallService 进入，在调用线程的内核路径中执行。

## 请求与数据来源

| SystemOperation | 实现方法 | 返回数据 |
| --- | --- | --- |
| MonotonicTime | `kernel::time::getMonotonicTime()` | 单调纳秒值，放在 reply.data[0]，用于计算间隔和截止时间。 |
| WallClockTime | `kernel::time::getWallClockTime()` | 当前日历时钟值，放在 reply.data[0]。 |
| ClockResolution | `kernel::time::getClockResolution()` | 时间模块提供的时钟精度，放在 reply.data[0]。 |
| Info | 组合时间、内存及 CPU 查询结果 | SystemInfo 从 reply.data[1] 开始，包含单调时间、总内存字节、空闲内存字节和在线 CPU 数量。 |
| Log | 检查消息中的字符串后调用 `kernel::device::serialWrite()` | 向内核串口输出文本。 |

时间值采用时间模块的纳秒接口。日历时间可能调整，不能用来代替单调时间计算 IPC deadline。SystemInfo 各项来自独立读取，不是对整台机器冻结后的同一时刻快照。

## 请求处理

文件内的 `Sys::call(caller, request, reply)` 按 request.type 分派。这里不保存调用者或会话状态，caller 由 Endpoint 认证后传入。

Log 使用 `readMessageName(request, 0, text, 512)` 验证数据字边界、非空内容和字符串结束符，最多在 512 字节范围内寻找终止符；不能把任意用户指针直接交给 serialWrite。

未知操作或非法日志内容返回 -1，合法请求返回 0，并将 reply.length 设为既有消息数据容量。该服务提供查询和串口日志，不实现设置系统时间，也不保存桌面、窗口、通知或 GUI 设置。
