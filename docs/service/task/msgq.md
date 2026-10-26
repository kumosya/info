# msgq 服务

src/task/msgq/main.cc 的 MessageQueueServer 在 Ring3 保存命名消息队列、打开状态、优先级、通知及延迟请求。旧内核消息队列后端已移除。

容量为 16 个队列、64 个打开对象、128 个待回复请求；每队列最多 16 条消息，每条最多 256 字节。接收优先取最高优先级，同优先级保持入队顺序。O_NONBLOCK 在不能立即完成时返回 WouldBlock。

打开对象通过 Endpoint 能力标签标识。服务按对象 ID 与句柄 rights 校验权限，不信任客户端传入的裸句柄数值。关闭后按引用统计回收打开状态；unlink 只移除名字，已有打开引用保持有效。

阻塞发送/接收保存 Reply Token；取消或超时后清理引用。注册的消息到达通知由服务通过 task.kernel 发送。服务状态位于 Ring3，内核只提供 Endpoint、句柄及信号等机制，没有新增 syscall。

## 引导、协议与资源

task 在 pipe 就绪后创建并监护 msgq，等待 msgq 注册后才对外发布 task。角色 MessageQueue，无 PCI 依赖和硬件能力。管理端为 control.task，普通 kill 无法终止本服务；基础服务当前拒绝 systemctl 单独停止/重启/禁用。

协议为 os/core_service.h 的消息队列操作，经已有 IPC 传入名称、属性、长度、优先级或打开能力。打开名校验 NUL 与容量；发送/接收检查消息大小、读写 rights 和队列容量。错误使用原生 ErrorCode，再由 libc mqueue 适配为 errno。

单请求线程串行修改队列、打开记录和等待表；待请求持有调用能力引用和回复 token，不能在全局队列状态锁内等待用户。超时、取消、关闭分别回收，unlink 后保留已有打开引用，最后引用释放才销毁。通知是一种到达提示，不能作为消息本体或事务提交证明。

退出后命名对象、优先级队列和待回复内容丢失；新实例的同名队列不是旧实例的延续，禁止自动重放有副作用发送。诊断查看 `[MSGQ]`、systemctl status msgq 和 owner/PPid，区分容量耗尽、无权限和对端死亡。相关 [task](../README.md)、[pipe](../pipe/README.md)；验证入口包括 tests/service-runtime.sh 及 libc 消息队列回归。
