# 进程通信

## 枚举

### os::Ipc::Handle::Flag

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `NoInherit` | `1` | 禁止按默认规则继承此引用 |
| `CloseOnExec` | `2` | exec成功时关闭此引用 |

### os::Ipc::Msg::HandleOption

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `RestrictRights` | `1` | 缩减权限 |
| `Move` | `2` | 发送成功后转移引用 |

## 常量

| 名称 | 值 | 说明 |
| --- | --- | --- |
| `os::ipcDataWordCount` | `515` | 底层IpcMessage固定负载有多少个64位字 |

## 对象

### os::Ipc

消息端点对象，提供消息发送、接收和请求回复。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Ipc()` | 构造Ipc对象 | — |
| `~Ipc()` | 析构Ipc对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Ipc& operator=(Ipc&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int destroy()` | 明确销毁端点对象 | — |
| `int send(Msg& message, uint64_t deadline)` | 发送消息或向队列加入数据 | `message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int receive(Msg& message, uint64_t deadline)` | 接收端点消息 | `message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int call(Msg& request, Msg& reply, uint64_t deadline)` | 发送请求并等待对应回复 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int reply(const Msg& request, Msg& reply)` | 回复收到的请求 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象 |
| `int cancel(uint64_t threadId)` | 取消端点等待 | `threadId`：线程编号 |
| `int open(Handle&& handle)` | 打开对象连接 | `handle`：资源句柄 |
| `int create()` | 创建消息端点 | — |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getHandle() const` | 读取原始资源句柄 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |
| `uint64_t getInfiniteDeadline() const` | 读取不设置截止时间的特殊值UINT64_MAX | — |

### os::Ipc::Handle

内核句柄对象，管理句柄的持有和关闭。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit Handle(uint64_t value = 0, bool owned = true)` | 构造Handle对象 | `value`：内核资源句柄值<br>`owned`：由对象管理句柄的释放 |
| `~Handle()` | 析构Handle对象 | — |
| `Handle(Handle&& other) noexcept` | 构造Handle对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Handle& operator=(Handle&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `uint64_t release()` | 交出对象持有的资源 | — |
| `int reset(uint64_t value = 0, bool owned = true)` | 重置对象状态 | `value`：内核资源句柄值<br>`owned`：由对象管理句柄的释放 |
| `int restrictRights(uint32_t rights)` | 缩减当前引用的权限 | `rights`：句柄访问权限 |
| `int destroyEndpoint()` | 明确请求销毁此句柄指向的端点 | — |
| `int requestActive(uint64_t token) const` | 询问给定回复令牌对应的请求是否仍有效 | `token`：请求或资源令牌 |
| `int queryState() const` | 请求服务返回底层资源当前状态 | — |
| `uint64_t queryIdentity() const` | 请求服务返回底层对象稳定身份 | — |
| `int64_t queryReferences() const` | 请求服务返回底层资源引用数量 | — |
| `int adopt(uint64_t handle)` | 接管资源句柄 | `handle`：资源句柄 |
| `int borrow(uint64_t handle)` | 借用资源句柄 | `handle`：资源句柄 |
| `int duplicate(uint32_t rights, uint32_t flags, Handle& result) const` | 为同一底层对象创建另一份引用 | `rights`：句柄访问权限<br>`flags`：操作标志位<br>`result`：接收资源句柄 |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getValue() const` | 读取原始句柄值 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `bool getOwned() const` | 读取资源是否由本对象负责释放 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setFlags(uint32_t flags)` | 设置当前标志位 | `flags`：操作标志位 |

### os::Ipc::Msg

消息对象，保存消息数据和附带句柄。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Msg()` | 构造Msg对象 | — |
| `~Msg()` | 析构Msg对象 | — |
| `Msg(Msg&& other) noexcept` | 构造Msg对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Msg& operator=(Msg&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int clear()` | 清除消息负载和回复状态 | — |
| `int read(size_t offset, void* buffer, size_t size) const` | 读取消息字段 | `offset`：数据起始偏移量<br>`buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `int write(size_t offset, const void* buffer, size_t size)` | 写入消息字段 | `offset`：数据起始偏移量<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `int attachHandle(Handle& handle, bool transfer)` | 向消息附加一个句柄 | `handle`：资源句柄<br>`transfer`：移交句柄所有权 |
| `int takeHandle(uint32_t index, Handle& handle)` | 取走消息中的一个接收引用 | `index`：项目索引<br>`handle`：资源句柄 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getType() const` | 读取目录项是文件、目录还是其他类型 | — |
| `size_t getSize() const` | 读取长度 | — |
| `size_t getCapacity() const` | 读取消息负载容量 | — |
| `uint32_t getHandleCount() const` | 读取消息携带的句柄数量 | — |
| `uint64_t getSenderId() const` | 读取由内核填写的发送进程编号 | — |
| `uint64_t getSenderThreadId() const` | 读取由内核填写的发送线程编号 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setType(uint64_t type)` | 设置目录项是文件、目录还是其他类型 | `type`：操作或对象类型 |

### os::Ipc::Pipe

管道对象，提供字节流读写。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Pipe()` | 构造Pipe对象 | — |
| `~Pipe()` | 析构Pipe对象 | — |
| `Pipe(Pipe&&) noexcept` | 构造Pipe对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Pipe& operator=(Pipe&&) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int create(bool duplex = false)` | 创建管道读写端点 | `duplex`：启用双向管道 |
| `int close()` | 关闭对象连接 | — |
| `int64_t read(void* buffer, size_t size)` | 读取管道数据 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `int64_t write(const void* buffer, size_t size)` | 写入管道数据 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `bool signalEof()` | 关闭管道继续写入的能力 | — |
| `uint64_t releaseReadHandle()` | 交出管道读端 | — |
| `uint64_t releaseWriteHandle()` | 交出管道写端 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getIdentity() const` | 读取底层对象的稳定身份 | — |
| `uint64_t getReadHandle() const` | 读取管道读端的借用句柄 | — |
| `uint64_t getWriteHandle() const` | 读取管道写端的借用句柄 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool setNonblocking(bool enabled)` | 设置读写是否立即返回而不等待 | `enabled`：功能开关 |

### os::Ipc::MsgQueue

消息队列对象，提供消息发送、接收和队列属性查询。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `MsgQueue()` | 构造MsgQueue对象 | — |
| `~MsgQueue()` | 析构MsgQueue对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int close()` | 关闭对象连接 | — |
| `int open(const char* name, int flags, uint32_t maximumMessages, uint32_t messageSize)` | 打开对象连接 | `name`：对象名称<br>`flags`：操作标志位<br>`maximumMessages`：队列可容纳的消息数量<br>`messageSize`：单条消息的最大字节数 |
| `int unlink(const char* name = nullptr)` | 删除资源的名称 | `name`：对象名称 |
| `uint64_t release()` | 交出对象持有的资源 | — |
| `int send(const void* data, size_t size, uint32_t priority)` | 发送消息或向队列加入数据 | `data`：数据缓冲区<br>`size`：数据大小，单位为字节<br>`priority`：消息优先级 |
| `int64_t receive(void* data, size_t capacity, uint32_t& priority)` | 接收队列消息 | `data`：数据缓冲区<br>`capacity`：接收缓冲区容量<br>`priority`：消息优先级 |
| `int refresh()` | 更新对象属性 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |
| `uint64_t getHandle() const` | 读取原始资源句柄 | — |
| `uint64_t getFlags() const` | 读取当前标志位 | — |
| `uint64_t getMaximumMessages() const` | 读取队列最多容纳的消息数 | — |
| `uint64_t getMessageSize() const` | 读取单条消息最大字节数 | — |
| `uint64_t getCurrentMessages() const` | 读取队列中的消息数量 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setFlags(uint64_t flags, uint64_t* previous = nullptr)` | 设置当前标志位 | `flags`：操作标志位<br>`previous`：接收修改前的设置 |
| `int setNotification(int type, uint32_t signal)` | 设置消息队列通知方式 | `type`：操作或对象类型<br>`signal`：信号编号 |
