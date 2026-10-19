# msgq 服务

user/msgq/main.cc 的 MessageQueueServer 在 Ring3 保存命名消息队列、打开状态、优先级、通知及延迟请求。旧内核消息队列后端已移除。

容量为 16 个队列、64 个打开对象、128 个待回复请求；每队列最多 16 条消息，每条最多 256 字节。接收优先取最高优先级，同优先级保持入队顺序。O_NONBLOCK 在不能立即完成时返回 WouldBlock。

打开对象通过 Endpoint 能力标签标识。服务按对象 ID 与句柄 rights 校验权限，不信任客户端传入的裸句柄数值。关闭后按引用统计回收打开状态；unlink 只移除名字，已有打开引用保持有效。

阻塞发送/接收保存 Reply Token；取消或超时后清理引用。注册的消息到达通知由服务通过 task.kernel 发送。服务状态位于 Ring3，内核只提供 Endpoint、句柄及信号等机制，没有新增 syscall。
