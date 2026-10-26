# Endpoint IPC 与驻留服务

src/kernel/task/ipc/ipc.cc 实现内核 Endpoint。管道缓冲区、消息队列和等待请求分别位于 src/task/pipe/main.cc、src/task/msgq/main.cc，运行于 Ring3；旧内核 pipe/mqueue 后端已经移除。

IpcMessage 由 os/abi.h 定义：type、内核认证 senderPid/senderTid、replyToken、length、handleCount、515 个数据字、4 个句柄、逐句柄 handleRights 与 handleOptions。不得使用客户端自填 PID 进行授权。

## 队列、截止时间与取消

Endpoint 有 8 个 FIFO 普通消息槽。队列满时发送等待，空时接收等待；等待者按队列登记顺序唤醒，但唤醒后的 CPU 执行顺序由调度器决定。IRQ 通知独立累计并合并，不占普通消息槽；领取时优先取通知。

SendMessage、ReceiveMessage、CallService 接受绝对单调 deadline。线程在持队列锁时 prepareWait 并加入等待队列，随后释放锁与阻塞；调度阻塞检查预先登记的状态，避免丢失唤醒。

取消仅允许同进程线程，通过 SendMessage 的明确取消子操作实现，无新增 syscall。未处理请求移出队列；已被领取的请求使 Token 失效，不回滚服务已完成的业务操作。

| 情况 | 返回 |
|---|---|
| 非阻塞但不能完成 | WouldBlock |
| 截止时间到达 | Timeout |
| 取消 | Cancelled |
| 信号打断 | Interrupted |
| Endpoint 关闭/销毁 | EndpointClosed |
| 服务领取线程死亡 | PeerDied |

线程终止先完成远端停稳，再取消 Endpoint 和其他等待登记，避免等待队列保留指向已释放栈的指针。

## 请求关联

每个 RPC 分配唯一 Reply Token，关联原 Endpoint、调用者、状态和领取进程；同时记录领取线程用于死亡通知。ReplyMessage 必须提供 Token 并属于领取进程；重复、过期、取消后的回复以及错误服务方回复失败。同一服务进程可以跨线程回复或保存多个待回复 Token，不依赖“当前线程最近一次请求”的隐含关联。

## 句柄事务

发送先验证权限和容量，再捕获对象引用；不会提前向接收者安装句柄。实际领取时，以领取线程所属 Task 为目标，在一组锁下预留空间并安装全部句柄。任意无效句柄、权限错误或容量不足均不产生部分安装。

复制共享 HandleTable::Reference。Send/Call 的 move 在成功入队时关闭源句柄，队列引用接管所有权；入队失败保留源句柄。Reply 的 move 先验证接收表容量，提交全部接收句柄时才关闭源句柄。rights 只能取原 rights 的子集。DuplicateHandle、RestrictHandleRights、NoInherit、CloseOnExec 通过现有句柄子操作实现，不新增 syscall。

task.kernel 等机制对象的 EndpointService::call 在调用线程内联执行，使用 Task 参数。pipe/msgq 在 Ring3 保存 Reply Token 和能力引用，定期查询请求有效性，取消后释放所持引用。


## RPC 请求状态

~~~mermaid
stateDiagram-v2
  [*] --> Queued: 校验权限及容量，捕获句柄引用
  Queued --> Claimed: 实际接收 Task 原子安装句柄
  Claimed --> Replied: 验证 Endpoint、Token 和领取进程
  Queued --> Cancelled: 超时、取消、信号或关闭
  Claimed --> Cancelled: 超时、取消或服务死亡
  Replied --> [*]
  Cancelled --> [*]: 唤醒调用者，拒绝迟到回复
~~~

pipe/msgq 延迟回复时查询请求是否仍有效，取消后释放所持能力引用。服务状态不放在内核。消息已领取后取消不回滚此前已完成的业务副作用；服务重启不自动重放原请求。

## SMP 旧地址空间回收

终止清理由专用内核 TaskReclaimer 执行，队列持有 Task 引用，请求者死亡不丢失回收责任。exec 的不可逆提交区禁止抢占，保持中断可用；并发 kill 必须等待提交线程实际切走后才能回收其地址空间。

~~~mermaid
sequenceDiagram
  participant A as exec CPU / 内核回收线程
  participant S as 调度器
  participant B as 远端 CPU
  participant M as 地址空间
  A->>S: 禁止新线程，终止其他线程并摘运行队列
  S->>B: 请求重新调度
  B->>B: 完成上下文及 CR3 切换
  B->>S: 更新 executingTasks 和已选中线程
  A->>S: quiesceTask 等待所有 CPU 离开
  S-->>A: 已停稳
  A->>M: 替换根并回收旧页表
~~~

Task 退出状态保留至 wait 回收。独立生命周期测试内核覆盖清理请求者被杀和跨 CPU exec/kill 交错。CPU 停稳不能替代 DMA 设备停止；设备死亡后的硬件停机与安全重启协议仍需完善。
