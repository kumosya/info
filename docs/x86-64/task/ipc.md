# Endpoint IPC 与驻留服务

src/kernel/task/ipc/ipc.cc 实现内核 Endpoint。管道缓冲区、消息队列和等待请求分别位于 src/task/pipe/main.cc、src/task/msgq/main.cc，运行于 Ring3；旧内核 pipe/mqueue 后端已经移除。

IpcMessage 由 os/abi.h 定义：type、内核认证 senderPid/senderTid、replyToken、length、handleCount、515 个数据字、4 个句柄、逐句柄 handleRights 与 handleOptions。不得使用客户端自填 PID 进行授权。

## 队列、截止时间与取消

Endpoint 有 8 个 FIFO 普通消息槽。队列满时发送等待，空时接收等待；等待者按队列登记顺序唤醒，但唤醒后的 CPU 执行顺序由调度器决定。IRQ 通知独立累计并合并，不占普通消息槽；领取时优先取通知。

SendMessage、ReceiveMessage、CallService 接受绝对单调纳秒 deadline：UINT64_MAX 无限，0 非阻塞。线程在持队列锁时 prepareWait 并加入等待队列，随后释放锁与阻塞；调度阻塞检查预先登记的状态，避免丢失唤醒。

取消仅允许同进程线程，通过 SendMessage 的明确取消子操作实现，无新增 syscall。未处理请求移出队列；已被领取的请求使 Token 失效，不回滚服务已完成的业务操作。

| 情况 | 返回 |
|---|---|
| 非阻塞但不能完成 | WouldBlock |
| 截止时间到达 | Timeout |
| 显式取消 | Cancelled |
| 信号打断 | Interrupted |
| Endpoint 关闭/销毁 | EndpointClosed |
| 服务领取线程死亡 | PeerDied |

线程终止先完成远端停稳，再取消 Endpoint 和其他等待登记，避免等待队列保留指向已释放栈的指针。

## 请求关联

每个 RPC 分配唯一 Reply Token，关联原 Endpoint、调用者、状态和领取进程；同时记录领取线程用于死亡通知。ReplyMessage 必须显式提供 Token 并属于领取进程；重复、过期、取消后的回复以及错误服务方回复失败。同一服务进程可以跨线程回复或保存多个待回复 Token，不依赖“当前线程最近一次请求”的隐含关联。

## 句柄事务

发送先验证权限和容量，再捕获对象引用；不会提前向接收者安装句柄。实际领取时，以领取线程所属 Task 为目标，在一组锁下预留空间并安装全部句柄。任意无效句柄、权限错误或容量不足均不产生部分安装。

复制共享 HandleTable::Reference。Send/Call 的 move 在成功入队时关闭源句柄，队列引用接管所有权；入队失败保留源句柄。Reply 的 move 先验证接收表容量，提交全部接收句柄时才关闭源句柄。rights 只能取原 rights 的子集。DuplicateHandle、RestrictHandleRights、NoInherit、CloseOnExec 通过现有句柄子操作实现，不新增 syscall。

task.kernel 等机制对象的 EndpointService::call 在调用线程内联执行，使用显式 Task 参数。pipe/msgq 在 Ring3 保存 Reply Token 和能力引用，定期查询请求有效性，取消后释放所持引用。

运行验证见 tests/check-kernel-ipc.sh；RPC 状态图和 SMP 回收时序见[混合内核架构](../../architecture.md)。
