# 系统服务

## 枚举

### os::Sys::Architecture

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Unknown` | `0` | 未识别的处理器架构 |
| `Amd64` | `1` | AMD64 处理器架构 |

### os::Sys::Status::State

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Stopped` | `0` | 服务未运行 |
| `Running` | `1` | 服务正在运行 |
| `Failed` | `2` | 服务启动或运行失败 |
| `Starting` | `3` | 启动中 |

## 对象

### os::Sys

系统信息对象，提供时间、日志和系统状态访问。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Sys()` | 构造Sys对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool refresh()` | 更新对象属性 | — |
| `void log(const char* message)` | 把文本交给系统日志 | `message`：消息内容 |
| `uint64_t readClockResolution()` | 读取系统时钟能表示的最小时间步长 | — |
| `uint64_t readWallClockTime()` | 读取当前日期时间对应的系统时钟 | — |
| `uint64_t readMonotonicTime()` | 读取只随运行时间前进的时钟 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getUptimeNanoseconds() const` | 读取系统启动后经过的纳秒数 | — |
| `uint64_t getTotalMemoryBytes() const` | 读取系统物理内存总字节数 | — |
| `uint64_t getFreeMemoryBytes() const` | 读取空闲物理内存字节数 | — |
| `uint32_t getOnlineCpus() const` | 读取当前在线处理器数量 | — |
| `Architecture getArchitecture() const` | 读取系统处理器架构 | — |

### os::Sys::Transport

核心服务通信对象，保存目标服务类别。

### os::Sys::Status

服务状态对象，保存服务配置和运行状态。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `const char* getName() const` | 读取名称 | — |
| `const char* getOwner() const` | 读取页对象所属任务编号 | — |
| `const char* getExecutable() const` | 读取服务可执行文件路径 | — |
| `uint64_t getTaskId() const` | 读取所属或对应的进程编号 | — |
| `State getState() const` | 读取系统或用户的持久状态目录路径 | — |
| `bool getEnabled() const` | 读取是否允许该服务自动启动 | — |
| `bool getDesired() const` | 读取管理者是否希望该服务运行 | — |
| `bool getBuiltin() const` | 读取服务是否由系统内置 | — |
| `uint32_t getRestartCount() const` | 读取服务已重启次数 | — |
| `int32_t getExitCode() const` | 读取进程或线程的退出码 | — |
| `const char* getBlockedBy() const` | 读取阻止服务启动的依赖名称 | — |

### os::Sys::Recovery

服务恢复对象，保存恢复检查点和状态。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Recovery()` | 构造Recovery对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int acquire(IpcMessage& snapshot)` | 向恢复服务取得本服务的恢复域和现有快照信息 | `snapshot`：检查点快照消息 |
| `int prepare(uint64_t bytes, bool logRecord)` | 设置进程启动环境 | `bytes`：数据字节数<br>`logRecord`：将记录写入系统日志 |
| `int commit(uint64_t handle, uint64_t bytes, uint64_t sequence, IpcMessage& result)` | 提交一份完整状态快照 | `handle`：资源句柄<br>`bytes`：数据字节数<br>`sequence`：记录序号<br>`result`：接收服务回复消息 |
| `int append(uint64_t handle, uint64_t bytes, uint64_t sequence, IpcMessage& result)` | 追加一条已经定义格式的增量记录 | `handle`：资源句柄<br>`bytes`：数据字节数<br>`sequence`：记录序号<br>`result`：接收服务回复消息 |
| `int readLog(uint32_t index, IpcMessage& record)` | 读取指定位置的已保存增量记录 | `index`：项目索引<br>`record`：记录数据 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getEpoch() const` | 读取恢复域当前代次 | — |

### os::Sys::Registry

服务注册表对象，提供名称注册与查找。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit Registry(uint64_t endpoint = 0)` | 构造Registry对象 | `endpoint`：消息端点句柄 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int lookup(const char* name, Ipc& endpoint)` | 按服务名称查找已登记端点 | `name`：对象名称<br>`endpoint`：接收中断通知的消息端点对象 |
| `int registerName(const char* name, Ipc& endpoint, uint64_t& generation)` | 向注册中心登记名称与端点 | `name`：对象名称<br>`endpoint`：接收中断通知的消息端点对象<br>`generation`：检查点或就绪状态的版本号 |
| `int unregisterName(const char* name, uint64_t generation)` | 只撤销与给定代次相符的名称登记 | `name`：对象名称<br>`generation`：检查点或就绪状态的版本号 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getEndpoint() const` | 读取注册中心端点的借用句柄 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setDefault(uint64_t endpoint)` | 设置后续服务查找使用的注册中心端点 | `endpoint`：消息端点句柄 |

### os::Sys::Connection

服务连接对象，保存服务端点和连接状态。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool connect(const char* name, uint64_t registry = 0)` | 按名称查找服务 | `name`：对象名称<br>`registry`：服务注册中心端点 |
| `int call(IpcMessage& request, IpcMessage& reply, uint64_t deadline = UINT64_MAX)` | 发送请求并等待对应回复 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `void disconnect()` | 释放已保存的服务引用 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getGeneration() const` | 读取查询结果或连接的代次 | — |
| `bool getConnected() const` | 读取是否已经连接到服务 | — |
| `const char* getName() const` | 读取名称 | — |

### os::Sys::Manager

服务管理对象，提供启动、停止和配置操作。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit Manager(const char* name = "control.init")` | 构造Manager对象 | `name`：对象名称 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int list(uint32_t index, Status& status)` | 按序号读取一个由该管理者负责的服务及其当前状态 | `index`：项目索引<br>`status`：操作状态 |
| `int query(const char* name, Status& status)` | 查询服务状态 | `name`：对象名称<br>`status`：操作状态 |
| `int start(const char* name)` | 请求管理者启动给定服务并处理依赖 | `name`：对象名称 |
| `int stop(const char* name)` | 请求设备停止工作 | `name`：对象名称 |
| `int restart(const char* name)` | 请求管理者按权限和依赖规则重启指定服务 | `name`：对象名称 |
| `int enable(const char* name)` | 允许管理者按配置自动启动指定服务 | `name`：对象名称 |
| `int disable(const char* name)` | 取消指定服务自动启动配置 | `name`：对象名称 |

### os::Sys::Checkpoint

服务检查点对象，保存恢复数据和资源令牌。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Checkpoint()` | 构造Checkpoint对象 | — |
| `~Checkpoint()` | 析构Checkpoint对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int close()` | 关闭对象连接 | — |
| `int open(void* state, uint64_t bytes, uint64_t schema)` | 打开对象连接 | `state`：状态数据<br>`bytes`：数据字节数<br>`schema`：检查点数据格式编号 |
| `int prepare()` | 设置进程启动环境 | — |
| `int commit(const void* state)` | 提交一份完整状态快照 | `state`：状态数据 |
| `int prepareLog(uint64_t bytes)` | 为下一条增量记录预留空间 | `bytes`：数据字节数 |
| `int append(const void* record, uint64_t bytes)` | 追加一条已经定义格式的增量记录 | `record`：记录数据<br>`bytes`：数据字节数 |
| `int readLog(uint32_t index, void* record, uint64_t bytes)` | 读取指定位置的已保存增量记录 | `index`：项目索引<br>`record`：记录数据<br>`bytes`：数据字节数 |
| `uint64_t put(uint64_t handle)` | 把一个资源引用登记到恢复域 | `handle`：资源句柄 |
| `uint64_t get(uint64_t token)` | 读取根据保存的恢复令牌取回资源引用 | `token`：请求或资源令牌 |
| `bool drop(uint64_t token)` | 撤销一个恢复令牌并释放其保存的引用 | `token`：请求或资源令牌 |
| `uint64_t next(uint64_t& cursor)` | 按游标枚举任务或恢复域中的资源 | `cursor`：枚举位置 |
| `int queryGeneration(uint64_t task, uint64_t& value)` | 读取指定任务的代次 | `task`：目标进程<br>`value`：接收检查点版本号 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |
| `bool getRestored() const` | 读取最近一次open是否从已有检查点恢复了状态 | — |
| `uint64_t getSequence() const` | 读取检查点当前序列号 | — |
| `uint64_t getEpoch() const` | 读取恢复域当前代次 | — |
| `uint32_t getRecordCount() const` | 读取检查点之后保存的增量记录数量 | — |
