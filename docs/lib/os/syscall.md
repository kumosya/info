# os/syscall.h



## 内容与边界

仅保留 SendMessage、ReceiveMessage、CallService、ReplyMessage、WaitAddress、WakeAddress 六个调用入口。包含 ErrorCode 及 OsSignalInfo、OsSignalStack、OsMachineContext、OsUserContext 固定布局。

invokeSyscall 是底层桥接，无需包装成无状态类。任务、句柄、内存管理走相应服务 IPC；地址等待保留直接 syscall。引导端点从加载器启动信息解析并缓存，不新增获取引导端点的 syscall。

本文件不包含内核调度对象、服务业务枚举或 GUI 定义。当前执行上下文操作放到 task.h 或运行时内部。

## 枚举

### os::SyscallNumber

底层类型：`uint64_t`。直接 syscall 编号；不是服务操作编号。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `SendMessage` | `1` | 向端点发送消息 |
| `ReceiveMessage` | `2` | 从端点接收消息 |
| `CallService` | `3` | 发送请求并等待回复 |
| `ReplyMessage` | `4` | 回复此前领取的请求 |
| `WaitAddress` | `18` | 比较用户地址值并等待 |
| `WakeAddress` | `19` | 唤醒等待同一地址的线程 |
| `Count` | `6` | 枚举项或入口的数量，不是最大编号 |

### os::ErrorCode

底层类型：`int64_t`。通用接口错误码。失败为负值，不同服务错误需要转换或明确标注。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Success` | `0` | 成功 |
| `InvalidArgument` | `-1` | 参数无效 |
| `InvalidHandle` | `-2` | 资源句柄无效 |
| `PermissionDenied` | `-3` | 权限不足 |
| `NotFound` | `-4` | 没有找到目标 |
| `OutOfMemory` | `-5` | 内存不足 |
| `WouldBlock` | `-6` | 当前不能立即完成 |
| `NotSupported` | `-7` | 当前不支持此操作 |
| `Busy` | `-8` | 目标仍在使用 |
| `Timeout` | `-9` | 等待超时 |
| `Interrupted` | `-10` | 被信号等事件打断 |
| `IoError` | `-11` | 输入输出失败 |
| `Cancelled` | `-12` | 请求已取消 |
| `EndpointClosed` | `-13` | 端点已关闭 |
| `PeerDied` | `-14` | 通信对端已经退出 |
| `BadAddress` | `-15` | 用户地址不可访问 |

### OS_REG_*

底层类型：`int`。固定信号上下文中寄存器数组的下标；不是寄存器内容。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `OS_REG_R8` | `0` | R8 寄存器保存位置 |
| `OS_REG_R9` | `1` | R9 寄存器保存位置 |
| `OS_REG_R10` | `2` | R10 寄存器保存位置 |
| `OS_REG_R11` | `3` | R11 寄存器保存位置 |
| `OS_REG_R12` | `4` | R12 寄存器保存位置 |
| `OS_REG_R13` | `5` | R13 寄存器保存位置 |
| `OS_REG_R14` | `6` | R14 寄存器保存位置 |
| `OS_REG_R15` | `7` | R15 寄存器保存位置 |
| `OS_REG_RDI` | `8` | RDI 寄存器保存位置 |
| `OS_REG_RSI` | `9` | RSI 寄存器保存位置 |
| `OS_REG_RBP` | `10` | RBP 寄存器保存位置 |
| `OS_REG_RBX` | `11` | RBX 寄存器保存位置 |
| `OS_REG_RDX` | `12` | RDX 寄存器保存位置 |
| `OS_REG_RAX` | `13` | RAX 寄存器保存位置 |
| `OS_REG_RCX` | `14` | RCX 寄存器保存位置 |
| `OS_REG_RSP` | `15` | RSP 寄存器保存位置 |
| `OS_REG_RIP` | `16` | RIP 寄存器保存位置 |
| `OS_REG_EFL` | `17` | 处理器状态标志 |
| `OS_REG_CSGSFS` | `18` | 段寄存器保存位置 |
| `OS_REG_ERR` | `19` | 异常错误码 |
| `OS_REG_TRAPNO` | `20` | 异常向量号 |
| `OS_REG_OLDMASK` | `21` | 此前的信号屏蔽集 |
| `OS_REG_CR2` | `22` | 异常访问地址 |

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

| 当前名字 | 当前值或表达式 | 数据含义 | 推荐归属 |
| --- | --- | --- | --- |
| `OS_SYS_BASE` | `1` | 直接调用编号起点 | 底层 ABI 保留 |
| `OS_SYS_SEND_MESSAGE` | `1` | 发送消息入口 | 底层 ABI 保留 |
| `OS_SYS_RECEIVE_MESSAGE` | `2` | 接收消息入口 | 底层 ABI 保留 |
| `OS_SYS_CALL_SERVICE` | `3` | 同步请求回复入口 | 底层 ABI 保留 |
| `OS_SYS_REPLY_MESSAGE` | `4` | 回复消息入口 | 底层 ABI 保留 |
| `OS_MAP_ANONYMOUS` | `0` | 内核内存请求中的匿名后备种类 | 移入内存服务协议；不是 POSIX MAP_ANONYMOUS 数值 |
| `OS_MAP_PAGE_OBJECT` | `256` | 内核内存请求中的页对象后备种类 | 移入内存服务协议 |
| `OS_MAP_SHARED_OBJECT` | `512` | 内核内存请求中的共享对象后备种类 | 移入内存服务协议 |
| `OS_PROTECT_COPY_ON_WRITE` | `4` | 内核页保护中的写时复制位 | 移入内存服务协议；不能解释为 POSIX PROT_EXEC |
| `OS_SYS_WAIT_ADDRESS` | `18` | 地址等待入口 | 底层 ABI 保留 |
| `OS_SYS_WAKE_ADDRESS` | `19` | 地址唤醒入口 | 底层 ABI 保留 |
| `OS_SYS_COUNT` | `6` | 直接调用共有六个，编号并不连续 | 底层 ABI 保留，不用它判断最大编号 |

## 全局变量

当前公共头文件没有导出可写全局变量。需要共享的初始化或缓存状态放在实现中，不通过应用头文件开放可写变量。

## 其他类型与固定布局

- 固定信号布局 OsSignalInfo、OsSignalStack、OsMachineContext、OsUserContext 用于信号返回及 POSIX 对接，不能按普通类布局重新解释。
- OS_REG_* 为 registers 数组的下标，新增寄存器字段必须单独维护 ABI。

## 类与函数

保留四个消息 syscall 和两个地址等待 syscall；原始调用桥接供库实现使用，不建立无状态包装类。

## 实现文件

`lib/os/syscall.cc` 对应本头文件。汇编桥接可使用 syscall.S，并使用不同对象名以免与 syscall.cc 冲突。
