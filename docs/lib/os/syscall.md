# 系统调用

## 枚举

### 寄存器下标

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `OS_REG_R8` | `0` | R8寄存器保存位置 |
| `OS_REG_R9` | `1` | R9寄存器保存位置 |
| `OS_REG_R10` | `2` | R10寄存器保存位置 |
| `OS_REG_R11` | `3` | R11寄存器保存位置 |
| `OS_REG_R12` | `4` | R12寄存器保存位置 |
| `OS_REG_R13` | `5` | R13寄存器保存位置 |
| `OS_REG_R14` | `6` | R14寄存器保存位置 |
| `OS_REG_R15` | `7` | R15寄存器保存位置 |
| `OS_REG_RDI` | `8` | RDI寄存器保存位置 |
| `OS_REG_RSI` | `9` | RSI寄存器保存位置 |
| `OS_REG_RBP` | `10` | RBP寄存器保存位置 |
| `OS_REG_RBX` | `11` | RBX寄存器保存位置 |
| `OS_REG_RDX` | `12` | RDX寄存器保存位置 |
| `OS_REG_RAX` | `13` | RAX寄存器保存位置 |
| `OS_REG_RCX` | `14` | RCX寄存器保存位置 |
| `OS_REG_RSP` | `15` | RSP寄存器保存位置 |
| `OS_REG_RIP` | `16` | RIP寄存器保存位置 |
| `OS_REG_EFL` | `17` | 处理器状态标志 |
| `OS_REG_CSGSFS` | `18` | 段寄存器保存位置 |
| `OS_REG_ERR` | `19` | 异常错误码 |
| `OS_REG_TRAPNO` | `20` | 异常向量号 |
| `OS_REG_OLDMASK` | `21` | 此前的信号屏蔽集 |
| `OS_REG_CR2` | `22` | 异常访问地址 |

### os::SyscallNumber

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `SendMessage` | `1` | 向端点发送消息 |
| `ReceiveMessage` | `2` | 从端点接收消息 |
| `CallService` | `3` | 发送请求并等待回复 |
| `ReplyMessage` | `4` | 回复此前领取的请求 |
| `WaitAddress` | `18` | 比较用户地址值并等待 |
| `WakeAddress` | `19` | 唤醒等待同一地址的线程 |
| `Readiness` | `20` | 就绪状态通知 |
| `Count` | `7` | 项目数量 |

### os::ErrorCode

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Success` | `0` | 成功 |
| `InvalidArgument` | `-1` | 参数无效 |
| `InvalidHandle` | `-2` | 资源句柄无效 |
| `PermissionDenied` | `-3` | 权限不足 |
| `NotFound` | `-4` | 目标查找失败 |
| `OutOfMemory` | `-5` | 内存不足 |
| `WouldBlock` | `-6` | 等待就绪 |
| `NotSupported` | `-7` | 功能支持错误 |
| `Busy` | `-8` | 目标仍在使用 |
| `Timeout` | `-9` | 等待超时 |
| `Interrupted` | `-10` | 被信号等事件打断 |
| `IoError` | `-11` | 输入输出失败 |
| `Cancelled` | `-12` | 请求已取消 |
| `EndpointClosed` | `-13` | 端点已关闭 |
| `PeerDied` | `-14` | 通信对端已经退出 |
| `BadAddress` | `-15` | 地址访问错误 |

## 常量

| 名称 | 值 | 说明 |
| --- | --- | --- |
| `OS_SYS_BASE` | `1U` | 系统调用起始编号 |
| `OS_SYS_SEND_MESSAGE` | `1U` | 发送消息入口 |
| `OS_SYS_RECEIVE_MESSAGE` | `2U` | 接收消息入口 |
| `OS_SYS_CALL_SERVICE` | `3U` | 同步请求回复入口 |
| `OS_SYS_REPLY_MESSAGE` | `4U` | 回复消息入口 |
| `OS_SYS_WAIT_ADDRESS` | `18U` | 地址等待入口 |
| `OS_SYS_WAKE_ADDRESS` | `19U` | 地址唤醒入口 |
| `OS_SYS_READINESS` | `20U` | 就绪状态通知编号 |
| `OS_SYS_COUNT` | `7U` | 系统调用数量 |
| `OS_TASK_IMAGE_MAX_BYTES` | `128 * 1024 * 1024` | 单个任务 ELF 文件的最大字节数 |

## 函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void notifyReadiness()` | 通知就绪状态变化 | — |
| `int waitAddress(volatile uint32_t* address, uint32_t expected)` | 等待地址 | `address`：内存地址<br>`expected`：等待地址的预期值 |
| `int wakeAddress(volatile uint32_t* address, uint32_t count)` | 唤醒地址等待者 | `address`：内存地址<br>`count`：最多唤醒的线程数量 |
| `int sendMessage(uint64_t endpoint, IpcMessage& message, uint64_t deadline = UINT64_MAX)` | 发送端点消息 | `endpoint`：消息端点句柄<br>`message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int receiveMessage(uint64_t endpoint, IpcMessage& message, uint64_t deadline = UINT64_MAX)` | 接收端点消息 | `endpoint`：消息端点句柄<br>`message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int callService(uint64_t endpoint, IpcMessage& request, IpcMessage& reply, uint64_t deadline = UINT64_MAX)` | 调用服务端点 | `endpoint`：消息端点句柄<br>`request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int replyMessage(uint64_t endpoint, uint64_t token, IpcMessage& reply)` | 回复端点消息 | `endpoint`：消息端点句柄<br>`token`：请求或资源令牌<br>`reply`：接收回复消息的对象 |
| `int64_t invokeSyscall(SyscallNumber number, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t arg4 = 0, uint64_t arg5 = 0, uint64_t arg6 = 0)` | 执行系统调用 | `number`：系统调用编号<br>`arg1`：第1个系统调用参数<br>`arg2`：第2个系统调用参数<br>`arg3`：第3个系统调用参数<br>`arg4`：第4个系统调用参数<br>`arg5`：第5个系统调用参数<br>`arg6`：第6个系统调用参数 |
