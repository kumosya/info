# OS 库

## 对象

| 类 | 说明 |
| --- | --- |
| [`os::Console`](console.md) | 控制台对象，提供终端输入、输出和显示设置。 |
| [`os::BootConsole`](console.md) | 启动终端连接对象，持有进度报告和显示交接使用的端点。 |
| [`os::DisplayHandoff`](console.md) | 显示交接连接对象，持有窗口服务端点。 |
| [`os::GraphicalSession`](console.md) | 图形会话连接对象，持有会话查询和控制使用的端点。 |
| [`os::Device`](device.md) | 设备访问对象，持有设备连接。 |
| [`os::Dir`](dir.md) | 目录对象，提供目录访问和约定路径解析。 |
| [`os::Executable`](elf.md) | ELF 映像对象，保存文件头、程序段和加载信息。 |
| [`os::Mount`](file.md) | 挂载对象，保存设备、挂载点和文件系统属性。 |
| [`os::File`](file.md) | 打开文件对象，持有文件连接、访问方式和位置。 |
| [`os::Fs`](file.md) | 文件系统连接对象，提供路径操作和挂载查询。 |
| [`os::Input`](input.md) | 输入设备连接对象，接收键盘和鼠标事件。 |
| [`os::Ipc`](ipc.md) | 消息端点对象，提供消息发送、接收和请求回复。 |
| [`os::Mem`](mm.md) | 进程内存对象，查询内存占用和可用容量。 |
| [`os::Pty`](pty.md) | 伪终端对象，连接主从端并管理终端属性。 |
| [`os::Runtime`](runtime.md) | 进程运行环境对象，保存启动端点、环境变量、终端选择和退出处理。 |
| [`os::ConfigurationText`](runtime.md) | 配置文本对象，保存文本范围并检查字符和路径格式。 |
| [`os::Sys`](service.md) | 系统信息对象，提供时间、日志和系统状态访问。 |
| [`os::Storage`](storage.md) | 存储设备目录对象，枚举和查询块设备。 |
| [`os::SpinLock`](sync.md) | 自旋锁对象，管理短临界区的互斥访问。 |
| [`os::Mutex`](sync.md) | 互斥锁对象，管理线程间的互斥访问。 |
| [`os::AddressWait`](sync.md) | 地址等待对象，保存等待序号并协调线程唤醒。 |
| [`os::Task`](task.md) | 进程对象，保存进程连接、身份和运行状态。 |
| [`os::User`](user.md) | 用户服务连接对象，提供账户、认证和登录会话访问。 |
| [`os::SessionIdentity`](user.md) | 任务认证身份连接对象，保存目标任务、任务服务通道和身份查询结果。 |
| [`os::SessionCleanup`](user.md) | 认证会话清理对象，保存待结束的认证会话编号。 |
