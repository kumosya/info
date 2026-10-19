# init 与服务管理

src/init/main.cc 的 InitSupervisor 和 src/init/service_manager.h 的 ServiceManager 运行于 Ring 3。注册表线程使用 64 KiB 栈，避免大 IPC 消息和管理记录造成栈溢出。

ServiceManager 提供 Register、Lookup、Unregister；名字 24 字节，容量 64。每条记录保存发布者、内核角色、UID、访问策略、端点及 generation。注册保护名要求固定 ServiceRole；替换活动服务要求同一发布者，注销验证发布者及当前 generation。Lookup 仅传递 Write 句柄。

注册表每 50 ms 检查端点与发布者状态。服务死亡时关闭缓存端点并增加 generation。InitSupervisor 每秒检查 boot 服务，重启已退出服务，并回收已退出子进程。

引导时先发布驻留对象名称，再依次启动 pci、task、device、storage、fs、mm、users、tty、console、guiw。所有特权服务从固定 boot 模块启动。GUI 终端和六个文本 shell 是普通磁盘程序。完整依赖图见[架构](../x86-64/architecture.md)。

重启后的端点重新注册，旧客户端明确得到端点或对端死亡错误；下次操作重新发现。共享业务状态（例如 MM 旧 VMA、FS 打开描述符或驱动 DMA 状态）不能仅凭重新注册自动恢复，具体限制由各服务负责。
