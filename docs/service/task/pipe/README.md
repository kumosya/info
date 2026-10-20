# pipe 服务

src/task/pipe/main.cc 的 PipeServer 在 Ring3 保存管道和双向通道的字节缓冲区、EOF、非阻塞标志及待回复请求。内核中没有 PipeState 或管道后端。

每条流容量 16 KiB，单次协议数据最多 512 字节，最多 64 对端点、128 个延迟请求。句柄是不可伪造的 Endpoint 能力标签：服务按内核对象 ID 查找状态，按传递的 rights 校验读写权限；客户端不能通过猜测整数 ID 获得访问权。

阻塞操作保存 Reply Token，不阻塞服务主线程。服务查询 Token 是否仍有效，取消/超时后释放所持能力引用。最后一个写方关闭产生持久 EOF；通道显式 EOF 是一次性事件。对象引用消失后销毁标签并回收 Ring3 缓冲状态。

使用现有 IPC 和句柄子操作，不增加 syscall；服务死亡使客户端收到明确端点错误，旧流状态不自动重放或恢复。

## 引导与权限

task 由 init 创建后，首先创建并监护 pipe，等待注册名 pipe 就绪，再创建 msgq。pipe 为独立 Ring3 镜像，角色 Pipe，不依赖 PCI；控制经 control.task 路由，父任务句柄不由 init 重复持有。

PipeOperation 在 os/core_service.h 定义。Create/CreateChannel 返回两个能力句柄；Read/Write 带一个端点标签，data[1] 为最多 512 字节的长度，写数据从 data[3] 开始；Read 回复状态/字节数与数据。Identity/Nonblocking/Eof 只接受已有标签及所需 rights；非法句柄、权限、满缓冲、对端关闭分别返回错误。

主线程串行推进 Pair 与 Pending，等待通过 Reply Token 保存，不阻塞处理其他管道。pending 持有引用，客户端取消/死亡后查询 token 并释放；双方引用消失才释放 Pair。显式通道 EOF 与最后写引用关闭分开，PTY 不能用永久泄漏的客户端副本维持假存活。

基础 pipe 当前不允许 systemctl 单独停止/重启/禁用；其故障会使现有终端与应用流失效，新实例不能恢复旧字节。
