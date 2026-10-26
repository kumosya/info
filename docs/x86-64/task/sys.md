# system.kernel

实现位于 `src/kernel/task/sys/service.cc`。`systemKernelService()` 返回内核驻留服务对象，端点仍通过统一的 IPC 调用入口处理请求。

`call()` 根据 `SystemOperation` 分派：单调时间由 `time::getMonotonicTime()` 提供，日历时间由 `time::getWallClockTime()` 提供，时钟精度由 `time::getClockResolution()` 提供。系统信息请求组合当前运行时间、内存总量、空闲内存和在线 CPU 数量。

日志请求先检查消息内字符串的边界，再调用 `serial::serialWrite()`。应用通过 `os::Sys` 查询这些信息；此服务不参与窗口、桌面或 GUI 状态管理。
