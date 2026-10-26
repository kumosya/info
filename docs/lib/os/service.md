# os/service.h


## 内容与边界

Sys 取代系统信息客户端，提供 log、refresh、系统统计 getter，以及 readMonotonicTime、readWallClockTime、readClockResolution 实时时钟读取。

| 嵌套类 | 功能 |
| --- | --- |
| Sys::Transport | protected 的连接和传输复用能力 |
| Sys::Registry | 注册中心发现、名称登记和撤销 |
| Sys::Connection | 保存服务名、端点及代次，connect/disconnect/call |
| Sys::Manager | list、query、start、stop、restart、enable、disable，各操作一个函数 |
| Sys::Status | 服务状态结果，逐字段 getter，无普通 setter |
| Sys::Recovery | 恢复域获取、准备、提交、日志追加和读取 |
| Sys::Checkpoint | open 返回准备或恢复检查点的结果，getRestored 区分恢复，close 显式释放映射，成员操作提交快照及日志 |

这些嵌套类独立构造，不需要先创建一个 Sys 实例。通用 Connection::call 是服务开发基础能力，业务客户端仍按操作提供成员函数。有副作用请求断连后不自动重放。

恢复能力不另建 recovery.h，固定持久化头部及服务私有内容留在实现中。

## 枚举

### ServiceOperation

底层类型：`uint64_t`。内部协议枚举，不进入推荐的 os/service.h。管理者服务请求编号，放在服务协议头文件。应用使用 Sys::Manager 的独立操作函数。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `List` | `1` | 枚举服务 |
| `Status` | `2` | 查询服务状态 |
| `Start` | `3` | 启动服务 |
| `Stop` | `4` | 停止服务 |
| `Restart` | `5` | 重启服务 |
| `Enable` | `6` | 启用自动启动 |
| `Disable` | `7` | 禁用自动启动 |

### Sys::Status::State

底层类型：`uint32_t`。服务生命周期状态；保持现有 Failed=2、Starting=3 的数值。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Stopped` | `0` | 服务未运行 |
| `Running` | `1` | 服务正在运行 |
| `Failed` | `2` | 服务启动或运行失败 |
| `Starting` | `3` | 服务正在启动 |

CoreServiceKind、TaskOperation 的前置声明从应用头文件移出，完整定义继续留在 include/service/common/kernel-requests.h。Sys::Transport 内部使用这些编号，普通调用者不需要填写。

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

| 当前名字 | 当前值或表达式 | 数据含义 | 推荐归属 |
| --- | --- | --- | --- |
| `magic` | `0x5356435354415445` | 检查点头部魔数 | Sys::Checkpoint 实现内部使用 |
| `maximumBytes` | `4194304` | 检查点最大字节数 | 恢复实现内部限制，不导出全局变量 |
| `logSchema` | `0x4c4f475245434f52` | 检查点增量日志格式标识 | 持久化格式内部约定 |
| `stagingAddress` | `0x6a000000` | 当前实现的暂存映射地址 | 恢复实现内部地址，不允许应用据此访问 |
| `snapshotAddress` | `0x6b000000` | 当前实现的恢复快照映射地址 | 恢复实现内部地址，不导出为应用常量 |

## 全局变量

当前公共头文件没有导出可写全局变量。恢复代次、序列和连接引用属于对象；共享运行时环境槽和析构入口保持私有。

### 当前运行时内部共享数据

下面是当前 .cc 中与服务连接、环境和退出有关的状态，不是要加入公共头文件的变量。拆分实现文件时每个进程仍只保留一份。

| 当前实现名字 | 初始值 | 保存什么 |
| --- | --- | --- |
| startupKernelHandle | 0 | 加载器传入的初始内核 IPC 端点 |
| startupRegistryHandle | 0 | 加载器或父进程传入的名称注册中心端点 |
| Runtime::initialEnvironment_ | nullptr | 启动时的环境字符串数组 |
| Runtime::environmentSlot_ | nullptr | C 运行库当前环境数组所在的槽，供更新后重新读取 |
| Runtime::sharedFinalizer_ | nullptr | 主程序结束后执行的共享库析构入口 |
| Runtime::prepared_ | false | 是否已经完成该进程的运行时准备 |

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### Sys

这个类表示系统统计、时钟和日志入口。

#### 构造与析构

```cpp
Sys();
```

用途：建立系统统计、时钟和日志入口；只保存借用数据或设置时，不接管调用者缓冲区。

参数：无。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### 操作

```cpp
bool refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：返回 true 或 false，具体含义见用途。

```cpp
void log(const char* message);
```

用途：把文本交给系统日志，不把文本当作格式字符串执行。

参数：

- `const char* message`：待发送或用于接收的消息对象。

结果：不返回数值；带有 noreturn 的当前上下文退出函数成功后不返回调用位置。

```cpp
uint64_t readClockResolution();
```

用途：读取系统时钟能表示的最小时间步长。

参数：无。

结果：返回对应时钟或内容的数值；单位沿用系统时钟 ABI，必须在声明中固定。

```cpp
uint64_t readWallClockTime();
```

用途：读取当前日期时间对应的系统时钟，可受校时影响。

参数：无。

结果：返回对应时钟或内容的数值；单位沿用系统时钟 ABI，必须在声明中固定。

```cpp
uint64_t readMonotonicTime();
```

用途：读取只随运行时间前进的时钟，适合计算间隔和超时。

参数：无。

结果：返回对应时钟或内容的数值；单位沿用系统时钟 ABI，必须在声明中固定。

#### getter

```cpp
uint64_t getUptimeNanoseconds() const;
```

数据：系统启动后经过的纳秒数。

```cpp
uint64_t getTotalMemoryBytes() const;
```

数据：系统物理内存总字节数。

```cpp
uint64_t getFreeMemoryBytes() const;
```

数据：空闲物理内存字节数。

```cpp
uint32_t getOnlineCpus() const;
```

数据：当前在线处理器数量。

### Sys::Transport

这个类表示具体服务客户端内部使用的发送和回复处理方法。

#### 构造与析构

```cpp
explicit Transport(CoreServiceKind kind);
```

用途：建立具体服务客户端内部使用的发送和回复处理方法；只保存借用数据或设置时，不接管调用者缓冲区。

参数：

- `CoreServiceKind kind`：需要解析的数据、配置、缓存等约定目录种类。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### 操作

```cpp
uint64_t openHandle();
```

用途：获取该传输对象对应的服务端点，供具体客户端发送请求。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int call(IpcMessage& request, IpcMessage& reply, uint64_t deadline = ipcInfiniteDeadline);
```

用途：发送请求并等待对应回复，deadline 限制本次等待。

参数：

- `IpcMessage& request`：待发送请求；回复接口中为此前收到的请求，用于关联回复。
- `IpcMessage& reply`：接收服务回复或提供待发送回复内容的消息。
- `uint64_t deadline = ipcInfiniteDeadline`：本次等待的单调时钟绝对截止时间；约定的无限值表示不设截止时间。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int64_t result(IpcMessage& request, IpcMessage& reply);
```

用途：发送服务请求并解释标准回复中的状态值，供派生客户端内部复用。

参数：

- `IpcMessage& request`：待发送请求；回复接口中为此前收到的请求，用于关联回复。
- `IpcMessage& reply`：接收服务回复或提供待发送回复内容的消息。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

### Sys::Registry

这个类表示一个名称注册中心的查找和登记连接。

#### 构造与析构

```cpp
explicit Registry(uint64_t endpoint = 0);
```

用途：建立一个名称注册中心的查找和登记连接；只保存借用数据或设置时，不接管调用者缓冲区。

参数：

- `uint64_t endpoint = 0`：接收设备中断或服务消息的 IPC 端点。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### 操作

```cpp
int lookup(const char* name, Ipc& endpoint);
```

用途：按服务名称查找已登记端点，成功时把引用交给 endpoint。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `Ipc& endpoint`：接收设备中断或服务消息的 IPC 端点。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int registerName(const char* name, Ipc& endpoint, uint64_t& generation);
```

用途：向注册中心登记名称与端点，并取得本次登记代次。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `Ipc& endpoint`：接收设备中断或服务消息的 IPC 端点。
- `uint64_t& generation`：名称登记代次；登记时输出，撤销时必须与登记结果相符。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int unregisterName(const char* name, uint64_t generation);
```

用途：只撤销与给定代次相符的名称登记，避免删掉重启后的新登记。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `uint64_t generation`：名称登记代次；登记时输出，撤销时必须与登记结果相符。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

#### getter

```cpp
uint64_t getEndpoint() const;
```

数据：注册中心端点的借用句柄。

#### setter

```cpp
void setDefault(uint64_t endpoint);
```

数据：后续服务查找使用的注册中心端点。

### Sys::Connection

这个类表示一个按名称建立的服务连接。

#### 操作

```cpp
bool connect(const char* name, uint64_t registry = 0);
```

用途：按名称查找服务，保存端点和代次；连接失败不伪装成已连接。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `uint64_t registry = 0`：显式指定的名称注册中心端点；约定空值使用默认注册中心。

结果：返回 true 或 false，具体含义见用途。

```cpp
int call(IpcMessage& request, IpcMessage& reply, uint64_t deadline = ipcInfiniteDeadline);
```

用途：发送请求并等待对应回复，deadline 限制本次等待。

参数：

- `IpcMessage& request`：待发送请求；回复接口中为此前收到的请求，用于关联回复。
- `IpcMessage& reply`：接收服务回复或提供待发送回复内容的消息。
- `uint64_t deadline = ipcInfiniteDeadline`：本次等待的单调时钟绝对截止时间；约定的无限值表示不设截止时间。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
void disconnect();
```

用途：释放已保存的服务引用，清除连接状态。

参数：无。

结果：不返回数值；带有 noreturn 的当前上下文退出函数成功后不返回调用位置。

#### getter

```cpp
uint64_t getGeneration() const;
```

数据：查询结果或连接的代次。

```cpp
bool getConnected() const;
```

数据：是否已经连接到服务。

```cpp
const char* getName() const;
```

数据：名称。

### Sys::Manager

这个类表示服务启动、停止、重启和启用配置的管理入口。

#### 构造与析构

```cpp
explicit Manager(const char* name = "control.init");
```

用途：建立服务启动、停止、重启和启用配置的管理入口；只保存借用数据或设置时，不接管调用者缓冲区。

参数：

- `const char* name = "control.init"`：资源、服务或账户名称，以零字节结束。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### 操作

```cpp
int list(uint32_t index, Sys::Status& status);
```

用途：按序号读取一个由该管理者负责的服务及其当前状态。

参数：

- `uint32_t index`：从 0 开始的条目或参数序号。
- `Sys::Status& status`：接收子进程退出或异步操作状态。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int query(const char* name, Sys::Status& status);
```

用途：查询给定路径或硬件地址，填写结果对象；查询不意味着打开、挂载或授权资源。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `Sys::Status& status`：接收子进程退出或异步操作状态。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int start(const char* name);
```

用途：请求管理者启动给定服务并处理依赖。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int stop(const char* name);
```

用途：请求管理者停止指定服务；管理者检查依赖并执行停止流程，不是停止硬件设备。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int restart(const char* name);
```

用途：请求管理者按权限和依赖规则重启指定服务。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int enable(const char* name);
```

用途：允许管理者按配置自动启动指定服务。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int disable(const char* name);
```

用途：取消指定服务自动启动配置，不等于立即停止。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

### Sys::Status

这个类表示一项服务的查询结果。

#### getter

```cpp
const char* getName() const;
```

数据：名称。

```cpp
const char* getOwner() const;
```

数据：服务管理者的名称。

```cpp
const char* getExecutable() const;
```

数据：服务可执行文件路径。

```cpp
uint64_t getTaskId() const;
```

数据：所属或对应的进程编号。

```cpp
Sys::Status::State getState() const;
```

数据：服务停止、启动中、运行或失败状态。

```cpp
bool getEnabled() const;
```

数据：是否允许该服务自动启动。

```cpp
bool getDesired() const;
```

数据：管理者是否希望该服务运行。

```cpp
bool getBuiltin() const;
```

数据：服务是否由系统内置。

```cpp
uint32_t getRestartCount() const;
```

数据：服务已重启次数。

```cpp
int32_t getExitCode() const;
```

数据：进程或线程的退出码。

```cpp
const char* getBlockedBy() const;
```

数据：阻止服务启动的依赖名称。

### Sys::Recovery

这个类表示当前服务的恢复域连接。

#### 构造与析构

```cpp
Recovery();
```

用途：建立当前服务的恢复域连接；只保存借用数据或设置时，不接管调用者缓冲区。

参数：无。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### 操作

```cpp
int acquire(IpcMessage& snapshot);
```

用途：向恢复服务取得本服务的恢复域和现有快照信息。

参数：

- `IpcMessage& snapshot`：接收恢复服务返回的已有快照及恢复域信息。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int prepare(uint64_t bytes, bool logRecord);
```

用途：提前申请下一次提交所需空间，避免修改状态后才发现无法保存。

参数：

- `uint64_t bytes`：状态或记录的有效字节数。
- `bool logRecord`：是否为增量记录准备空间。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int commit(uint64_t handle, uint64_t bytes, uint64_t sequence, IpcMessage& result);
```

用途：提交一份完整状态快照，成功后推进检查点序列。

参数：

- `uint64_t handle`：现有资源句柄或待转移的句柄对象。
- `uint64_t bytes`：状态或记录的有效字节数。
- `uint64_t sequence`：提交使用的序列号，防止覆盖不匹配的状态。
- `IpcMessage& result`：接收查询或复制结果的输出对象。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int append(uint64_t handle, uint64_t bytes, uint64_t sequence, IpcMessage& result);
```

用途：追加一条已经定义格式的增量记录。

参数：

- `uint64_t handle`：现有资源句柄或待转移的句柄对象。
- `uint64_t bytes`：状态或记录的有效字节数。
- `uint64_t sequence`：提交使用的序列号，防止覆盖不匹配的状态。
- `IpcMessage& result`：接收查询或复制结果的输出对象。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int readLog(uint32_t index, IpcMessage& record);
```

用途：读取指定位置的已保存增量记录。

参数：

- `uint32_t index`：从 0 开始的条目或参数序号。
- `IpcMessage& record`：增量日志内容；读取时为输出缓冲区。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

#### getter

```cpp
uint64_t getEpoch() const;
```

数据：恢复域当前代次。

### Sys::Checkpoint
这个类表示服务状态快照、增量日志和恢复资源令牌。

#### 构造与析构

```cpp
~Checkpoint();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Checkpoint();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


#### 操作

```cpp
int prepare();
```

用途：提前申请下一次提交所需空间，避免修改状态后才发现无法保存。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int commit(const void* state);
```

用途：提交一份完整状态快照，成功后推进检查点序列。

参数：

- `const void* state`：需要保存或恢复的状态缓冲区。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int prepareLog(uint64_t bytes);
```

用途：为下一条增量记录预留空间，必要时返回需要完整快照的状态。

参数：

- `uint64_t bytes`：状态或记录的有效字节数。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int append(const void* record, uint64_t bytes);
```

用途：追加一条已经定义格式的增量记录。

参数：

- `const void* record`：增量日志内容；读取时为输出缓冲区。
- `uint64_t bytes`：状态或记录的有效字节数。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int readLog(uint32_t index, void* record, uint64_t bytes);
```

用途：读取指定位置的已保存增量记录。

参数：

- `uint32_t index`：从 0 开始的条目或参数序号。
- `void* record`：增量日志内容；读取时为输出缓冲区。
- `uint64_t bytes`：状态或记录的有效字节数。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
uint64_t put(uint64_t handle);
```

用途：把一个资源引用登记到恢复域，返回可写入检查点的令牌。

参数：

- `uint64_t handle`：现有资源句柄或待转移的句柄对象。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
bool drop(uint64_t token);
```

用途：撤销一个恢复令牌并释放其保存的引用。

参数：

- `uint64_t token`：此前登记资源得到的恢复令牌。

结果：返回 true 或 false，具体含义见用途。

```cpp
uint64_t next(uint64_t& cursor);
```

用途：按游标枚举任务或恢复域中的资源，成功后更新输出。

参数：

- `uint64_t& cursor`：输入上次枚举位置，并输出新的继续位置。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int queryGeneration(uint64_t task, uint64_t& value);
```

用途：读取指定任务的代次，避免把复用的编号认成同一任务。

参数：

- `uint64_t task`：已绑定的目标进程；需要时借用其身份或引用。
- `uint64_t& value`：要设置的新值，其含义与当前 setter 对应的字段相同。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(void* state, uint64_t bytes, uint64_t schema);
```

用途：准备检查点或恢复已保存的状态；getRestored 读取是否发生恢复，错误通过返回值报告。

参数：

- `void* state`：需要保存或恢复的状态缓冲区。
- `uint64_t bytes`：状态或记录的有效字节数。
- `uint64_t schema`：检查点格式编号，用于判断恢复数据是否与当前实现兼容。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：解除检查点状态映射并释放本对象持有的检查点引用，不删除已保存的恢复记录。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t get(uint64_t token);
```

数据：根据保存的恢复令牌取回资源引用，不按内存地址读取。

```cpp
uint64_t getSequence() const;
```

数据：检查点当前序列号。

```cpp
uint64_t getEpoch() const;
```

数据：恢复域当前代次。

```cpp
uint32_t getRecordCount() const;
```

数据：检查点之后保存的增量记录数量。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

```cpp
bool getRestored() const;
```

数据：最近一次 open 是否从已有检查点恢复了状态。

## 实现文件

`lib/os/service.cc` 对应本头文件。服务连接、恢复和必要的运行时内部支持可在 lib/os/service/ 内细分。
