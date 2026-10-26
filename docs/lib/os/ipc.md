# os/ipc.h


## 内容与边界

Ipc 合并原端点客户端与端点资源对象，持有自身端点引用，create 创建端点，open 接管已有端点，close 显式释放引用，destroy 明确销毁，send、receive、call、reply、cancel 进行通信。各操作使用本对象端点，不重复传入相同句柄。

Ipc::Handle 管理一个通用引用，构造、析构、adopt、borrow、duplicate、release 明确所有权；getter 读取本地属性，queryIdentity/queryReferences/queryState 明确访问服务；setFlags 设置标志。

Ipc::Msg 管理负载和携带句柄，clear、read、write、attachHandle、takeHandle 维护消息。发送者身份与回复关联由内核填充，应用不能通过 setter 伪造。未接管的接收句柄随消息清理释放。

Ipc::Pipe 持有读写端点，负责创建、读写、EOF 和阻塞设置，不保留 PipeHandles 公共聚合。Ipc::MsgQueue 管理队列引用、名称、优先级传输及属性快照，关闭引用和删除名称分开。

底层 IpcMessage 固定 ABI 可保留，普通对象与 wire 布局分离。

## 枚举

### HandleOperation

底层类型：`uint64_t`。内部协议枚举，不进入推荐的 os/ipc.h。内核 IPC 服务的句柄请求编号，放在 include/service/common/，由 Ipc::Handle 编码。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Close` | `0` | 关闭引用 |
| `Duplicate` | `1` | 复制引用 |
| `RestrictRights` | `2` | 缩减权限 |
| `SetFlags` | `3` | 设置标志 |
| `GetState` | `4` | 查询状态 |
| `ObjectIdentity` | `5` | 查询底层对象身份 |
| `References` | `6` | 查询引用数 |
| `DestroyEndpoint` | `7` | 明确销毁端点 |
| `RequestActive` | `8` | 检查请求是否仍有效 |

### Ipc::Handle::Flag

底层类型：`uint32_t`。可按位组合；权限集合另行传递。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `NoInherit` | `1` | 禁止按默认规则继承此引用 |
| `CloseOnExec` | `2` | exec 成功时关闭此引用 |

### Ipc::Msg::HandleOption

底层类型：`uint32_t`。限制接收权限与转移发送者引用。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `RestrictRights` | `1` | 缩减权限 |
| `Move` | `2` | 发送成功后转移引用 |

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

| 当前名字 | 当前值或表达式 | 数据含义 | 推荐归属 |
| --- | --- | --- | --- |
| `ipcDataWordCount` | `515` | 底层 IpcMessage 固定负载有多少个 64 位字 | 固定 ABI 定义保留；普通消息容量用 Ipc::Msg::getCapacity() 查询字节数 |
| `ipcHandleRestrictRights` | `1` | 消息携带句柄时缩减权限 | 改为 Ipc::Msg::HandleOption::RestrictRights |
| `ipcHandleMove` | `2` | 消息携带句柄时转移引用 | 改为 Ipc::Msg::HandleOption::Move |
| `handleFlagNoInherit` | `1` | 句柄禁止继承标志 | 改为 Ipc::Handle::Flag::NoInherit |
| `handleFlagCloseOnExec` | `2` | exec 关闭句柄标志 | 改为 Ipc::Handle::Flag::CloseOnExec |
| `handleFlagsMask` | `3` | 当前支持的两个句柄标志合并掩码 | 实现内部参数检查使用 |
| `ipcInfiniteDeadline` | `18446744073709551615` | 不设置 IPC 截止时间的特殊值 | 私有常量；通过 Ipc::getInfiniteDeadline() 查询 |

```cpp
size_t Ipc::Msg::getCapacity() const;
```

数据：消息负载容量，单位字节；当前固定底层布局为 515 × 8 = 4120。

```cpp
uint64_t Ipc::getInfiniteDeadline() const;
```

数据：不设置截止时间的特殊值 UINT64_MAX。

## 全局变量

当前公共头文件没有导出可写全局变量。端点、消息和引用属于对象；加载器传入的引导端点缓存在运行时内部。

## 其他类型与固定布局

- 底层 IpcMessage 固定布局保留，当前为 4224 字节；负载 515 个 64 位字，最多携带 4 个句柄以及各自权限与选项。
- 发送者进程、线程及回复关联由内核填写，不能当作可随意设置的全局数据。

## 类与函数

### Ipc
这个类表示一个 IPC 消息端点。

#### 构造与析构

```cpp
~Ipc();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Ipc();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Ipc& operator=(Ipc&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Ipc&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int destroy();
```

用途：明确销毁端点对象，使后续通信失败；析构仅释放本对象持有的引用。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int send(Ipc::Msg& message, uint64_t deadline);
```

用途：发送消息或向队列加入数据；携带资源按复制或转移规则处理。

参数：

- `Ipc::Msg& message`：待发送或用于接收的消息对象。
- `uint64_t deadline`：本次等待的单调时钟绝对截止时间；约定的无限值表示不设截止时间。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int receive(Ipc::Msg& message, uint64_t deadline);
```

用途：从本对象端点接收一条消息，内核填写发送者身份和回复关联信息。

参数：

- `Ipc::Msg& message`：待发送或用于接收的消息对象。
- `uint64_t deadline`：本次等待的单调时钟绝对截止时间；约定的无限值表示不设截止时间。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
int call(Ipc::Msg& request, Ipc::Msg& reply, uint64_t deadline);
```

用途：发送请求并等待对应回复，deadline 限制本次等待。

参数：

- `Ipc::Msg& request`：待发送请求；回复接口中为此前收到的请求，用于关联回复。
- `Ipc::Msg& reply`：接收服务回复或提供待发送回复内容的消息。
- `uint64_t deadline`：本次等待的单调时钟绝对截止时间；约定的无限值表示不设截止时间。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int reply(const Ipc::Msg& request, Ipc::Msg& reply);
```

用途：回复收到的请求，使用该请求携带的回复关联信息。

参数：

- `const Ipc::Msg& request`：待发送请求；回复接口中为此前收到的请求，用于关联回复。
- `Ipc::Msg& reply`：接收服务回复或提供待发送回复内容的消息。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int cancel(uint64_t threadId);
```

用途：请求取消指定线程在此端点上尚未完成的请求。

参数：

- `uint64_t threadId`：目标线程编号；必须结合所属任务或可用引用解释。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(Ipc::Handle&& handle);
```

用途：检查端点种类并接管传入的端点引用。

参数：

- `Ipc::Handle&& handle`：现有资源句柄或待转移的句柄对象。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int create();
```

用途：创建一个 IPC 端点并保存引用。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int close();
```

用途：释放当前端点引用；不代替明确销毁端点的 destroy。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getHandle() const;
```

数据：原始资源句柄。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

### Ipc::Handle
这个类表示一个内核资源引用及其释放责任。

#### 构造与析构

```cpp
Handle();
```

用途：只初始化本地空状态，不申请或创建系统资源。

参数：无。

结果：构造没有返回值；申请资源需显式调用 open 或 create。


```cpp
explicit Handle(uint64_t value = 0, bool owned = true);
```

用途：只记录给定句柄及所有权，不发送验证请求；需要检查有效性时调用 queryState。

参数：

- `uint64_t value = 0`：要设置的新值，其含义与当前 setter 对应的字段相同。
- `bool owned = true`：是否由新对象负责释放引用；false 表示借用。

结果：完成本地绑定，不代表该数值已通过服务端验证。

```cpp
~Handle();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Handle(Ipc::Handle&& other) noexcept;
```

用途：将 other 的资源及释放责任转移给本对象，之后 other 为空；不得把这当作复制一份系统资源。

参数：

- `Ipc::Handle&& other`：需要转移或复制的另一个同类对象。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

```cpp
Ipc::Handle& operator=(Ipc::Handle&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Ipc::Handle&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
uint64_t release();
```

用途：把资源管理责任交给调用者，本对象不再负责释放。

参数：无。

结果：返回用途所述字段或被交接的原始引用；空引用使用约定的无效值。

```cpp
int reset(uint64_t value = 0, bool owned = true);
```

用途：释放当前拥有的引用，再绑定新的句柄。

参数：

- `uint64_t value = 0`：要设置的新值，其含义与当前 setter 对应的字段相同。
- `bool owned = true`：是否由新对象负责释放引用；false 表示借用。

结果：不返回数值；带有 noreturn 的当前上下文退出函数成功后不返回调用位置。

```cpp
int restrictRights(uint32_t rights);
```

用途：缩减当前引用的权限，不能借此增加权限。

参数：

- `uint32_t rights`：复制后或缩减后允许的权限，不得超过原有权限。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int destroyEndpoint();
```

用途：明确请求销毁此句柄指向的端点，其他引用随后观察到关闭；这与只释放当前句柄不同。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int requestActive(uint64_t token) const;
```

用途：询问给定回复令牌对应的请求是否仍有效，供受控服务避免处理已取消的认证请求。

参数：

- `uint64_t token`：此前登记资源得到的恢复令牌。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int queryState() const;
```

用途：请求服务返回底层资源当前状态。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
uint64_t queryIdentity() const;
```

用途：请求服务返回底层对象稳定身份，用于判断不同句柄是否指向同一对象。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int64_t queryReferences() const;
```

用途：请求服务返回底层资源引用数量。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int adopt(uint64_t handle);
```

用途：接管已有引用，之后由本对象负责释放；接管失败时原所有者继续负责。

参数：

- `uint64_t handle`：现有资源句柄或待转移的句柄对象。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int borrow(uint64_t handle);
```

用途：临时使用已有引用，析构时不关闭借用资源。

参数：

- `uint64_t handle`：现有资源句柄或待转移的句柄对象。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int duplicate(uint32_t rights, uint32_t flags, Ipc::Handle& result) const;
```

用途：为同一底层对象创建另一份引用，权限不能超过原引用。

参数：

- `uint32_t rights`：复制后或缩减后允许的权限，不得超过原有权限。
- `uint32_t flags`：本次操作的标志位；只接受该操作声明支持的组合。
- `Ipc::Handle& result`：接收查询或复制结果的输出对象。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int close();
```

用途：释放这一份引用，按服务返回结果更新拥有状态；不能重复关闭已被释放的引用。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getValue() const;
```

数据：原始句柄值。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
bool getOwned() const;
```

数据：资源是否由本对象负责释放。

#### setter

```cpp
int setFlags(uint32_t flags);
```

数据：资源标志位。

### Ipc::Msg

这个类表示消息字节、接收者信息及消息携带的资源引用。

#### 构造与析构

```cpp
Msg();
```

用途：建立消息字节、接收者信息及消息携带的资源引用；只保存借用数据或设置时，不接管调用者缓冲区。

参数：无。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

```cpp
~Msg();
```

用途：释放本对象拥有的消息字节、接收者信息及消息携带的资源引用；借用资源不释放，已交接资源不重复释放。

参数：无。

结果：析构函数没有返回值，不抛出异常。需要确认操作结果时使用明确返回状态的业务函数。

```cpp
Msg(Ipc::Msg&& other) noexcept;
```

用途：将 other 的资源及释放责任转移给本对象，之后 other 为空；不得把这当作复制一份系统资源。

参数：

- `Ipc::Msg&& other`：需要转移或复制的另一个同类对象。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

```cpp
Ipc::Msg& operator=(Ipc::Msg&& other) noexcept;
```

用途：将 other 的资源及释放责任转移给本对象，之后 other 为空；不得把这当作复制一份系统资源。

参数：

- `Ipc::Msg&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int clear();
```

用途：清除消息负载和回复状态，释放未被取走的接收句柄。

参数：无。

结果：不返回数值；带有 noreturn 的当前上下文退出函数成功后不返回调用位置。

```cpp
int read(size_t offset, void* buffer, size_t size) const;
```

用途：把本消息负载中 offset 开始的 size 个字节复制到 buffer，不发 IPC。

参数：

- `size_t offset`：从消息负载开头计算的字节偏移。
- `void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。

结果：0 表示复制成功，负值表示负载范围、缓冲区或容量不合法。

```cpp
int write(size_t offset, const void* buffer, size_t size);
```

用途：把 buffer 的 size 个字节复制到本消息负载的 offset 位置，必要时更新有效长度，不发送消息。

参数：

- `size_t offset`：写入消息负载的位置，不能超过消息容量。
- `const void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。

结果：0 表示复制成功，负值表示负载范围、缓冲区或容量不合法。

```cpp
int attachHandle(Ipc::Handle& handle, bool transfer);
```

用途：向消息附加一个句柄，明确复制引用还是转移引用。

参数：

- `Ipc::Handle& handle`：现有资源句柄或待转移的句柄对象。
- `bool transfer`：true 时立即把释放责任交给消息对象，原 Handle 变为空；false 时借用原 Handle，调用者必须让它保持有效直到发送结束。

转移的句柄由消息对象保存，不保存指向原 Handle 的指针。发送前失败时，可以通过 takeHandle 取回；clear 会尝试关闭仍由消息持有的引用，关闭失败返回错误并保留引用。call 超时不表示请求未执行：请求入队就可能已经转移句柄。实现查询带有代数的原句柄是否仍有效，已失效的引用不再重复释放；查询暂时失败时保留清理责任，随后 close 仍由内核验证完整句柄。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int takeHandle(uint32_t index, Ipc::Handle& handle);
```

用途：取走消息中的一个接收引用，之后由输出对象负责释放。

参数：

- `uint32_t index`：从 0 开始的条目或参数序号。
- `Ipc::Handle& handle`：现有资源句柄或待转移的句柄对象。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

#### getter

```cpp
uint64_t getType() const;
```

数据：种类。

```cpp
size_t getSize() const;
```

数据：长度，单位为字节。

```cpp
uint32_t getHandleCount() const;
```

数据：消息携带的句柄数量。

```cpp
uint64_t getSenderId() const;
```

数据：由内核填写的发送进程编号。

```cpp
uint64_t getSenderThreadId() const;
```

数据：由内核填写的发送线程编号。

#### setter

```cpp
void setType(uint64_t type);
```

数据：待发送消息的类型。

### Ipc::Pipe
这个类表示一对单向或双向数据通道端点。

#### 构造与析构

```cpp
Pipe();
```

用途：只初始化本地空状态，不申请或创建系统资源。

参数：无。

结果：构造没有返回值；申请资源需显式调用 open 或 create。


```cpp
~Pipe();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Ipc::Pipe& operator=(Ipc::Pipe&&) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Ipc::Pipe&&`：另一个同类对象；移动时交接资源，禁止复制的声明不能调用。

结果：返回本对象的引用。

#### 操作

```cpp
int64_t read(void* buffer, size_t size);
```

用途：从文件、管道或消息负载读取字节到调用者缓冲区。

参数：

- `void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
int64_t write(const void* buffer, size_t size);
```

用途：把调用者缓冲区中的字节写入文件、管道或消息负载。

参数：

- `const void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
bool signalEof();
```

用途：关闭管道继续写入的能力，使对端在读完剩余内容后得到 EOF。

参数：无。

结果：返回 true 或 false，具体含义见用途。

```cpp
uint64_t releaseReadHandle();
```

用途：交出管道读端，之后对象不再负责关闭该端。

参数：无。

结果：返回用途所述字段或被交接的原始引用；空引用使用约定的无效值。

```cpp
uint64_t releaseWriteHandle();
```

用途：交出管道写端，之后对象不再负责关闭该端。

参数：无。

结果：返回用途所述字段或被交接的原始引用；空引用使用约定的无效值。

```cpp
int create(bool duplex = false);
```

用途：创建单向管道或双向通道；失败不交接一半成功的端点。

参数：

- `bool duplex = false`：false 创建单向管道；true 请求双向通道。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：关闭仍由本对象持有的两端；记录两端分别是否已经释放，失败后不重复关闭已释放端。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getIdentity() const;
```

数据：底层对象的稳定身份。

```cpp
uint64_t getReadHandle() const;
```

数据：管道读端的借用句柄。

```cpp
uint64_t getWriteHandle() const;
```

数据：管道写端的借用句柄。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

#### setter

```cpp
bool setNonblocking(bool enabled);
```

数据：读写是否立即返回而不等待。

### Ipc::MsgQueue
这个类表示一份命名消息队列引用。

#### 构造与析构

```cpp
~MsgQueue();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
MsgQueue();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


#### 操作

```cpp
int unlink(const char* name = nullptr);
```

用途：删除资源的名称；已有打开引用继续有效。

参数：

- `const char* name = nullptr`：资源、服务或账户名称，以零字节结束。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
uint64_t release();
```

用途：把资源管理责任交给调用者，本对象不再负责释放。

参数：无。

结果：返回用途所述字段或被交接的原始引用；空引用使用约定的无效值。

```cpp
int send(const void* data, size_t size, uint32_t priority);
```

用途：发送消息或向队列加入数据；携带资源按复制或转移规则处理。

参数：

- `const void* data`：输入数据或文件系统专用挂载参数，具体格式由对应操作规定。
- `size_t size`：本次数据的字节数。
- `uint32_t priority`：消息优先级；接收接口中为输出，返回收到消息的优先级。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int64_t receive(void* data, size_t capacity, uint32_t& priority);
```

用途：取出一条事件或消息，并把接收资源交给结果对象。

参数：

- `void* data`：输入数据或文件系统专用挂载参数，具体格式由对应操作规定。
- `size_t capacity`：输出缓冲区可容纳的字节数；条目数组接口中表示可容纳的条目个数。
- `uint32_t& priority`：消息优先级；接收接口中为输出，返回收到消息的优先级。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(const char* name, int flags, uint32_t maximumMessages, uint32_t messageSize);
```

用途：打开或按标志创建命名队列。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `int flags`：本次操作的标志位；只接受该操作声明支持的组合。
- `uint32_t maximumMessages`：消息队列最多保存的消息数量。
- `uint32_t messageSize`：每条消息允许的最大字节数。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：显式释放本对象拥有的资源并返回清理结果；借用资源不释放，失败时不能隐瞒仍然存在的引用或映射。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getHandle() const;
```

数据：原始资源句柄。

```cpp
uint64_t getFlags() const;
```

数据：当前标志位。

```cpp
uint64_t getMaximumMessages() const;
```

数据：队列最多容纳的消息数。

```cpp
uint64_t getMessageSize() const;
```

数据：单条消息最大字节数。

```cpp
uint64_t getCurrentMessages() const;
```

数据：队列中尚未取出的消息数量。

```cpp
bool getOpened() const;
```

数据：是否已经打开。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

#### setter

```cpp
int setFlags(uint64_t flags, uint64_t* previous = nullptr);
```

数据：资源标志位。

```cpp
int setNotification(int type, uint32_t signal);
```

数据：消息队列通知方式。

## 实现文件

`lib/os/ipc.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
