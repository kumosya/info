# 进程与线程

## 枚举

### os::Task::Spawn::Flag

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `NewSession` | `1` | 为新进程请求新会话 |
| `ManagedService` | `2` | 请求受控管理服务启动 |

### os::Task::Core::State

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `None` | `0` | 空值 |
| `Requested` | `1` | 已请求生成core |
| `Capturing` | `2` | 正在捕获任务状态 |
| `Ready` | `3` | 捕获完成 |
| `Writing` | `4` | 正在写入core |
| `Complete` | `5` | core已完成 |
| `Truncated` | `6` | core因大小等限制被截断 |
| `Failed` | `7` | 服务启动或运行失败 |
| `Skipped` | `8` | 按策略跳过core |
| `TimedOut` | `9` | core操作超时 |

## 对象

### os::Task

进程对象，保存进程连接、身份和运行状态。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Task()` | 构造Task对象 | — |
| `~Task()` | 析构Task对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Task& operator=(Task&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int refresh()` | 更新对象属性 | — |
| `int refreshExit()` | 读取目标进程的退出信号、异常位置和core状态 | — |
| `int open(uint64_t id)` | 打开对象连接 | `id`：进程编号 |
| `int openCurrent()` | 绑定当前进程使用的控制台 | — |
| `int create(const Spawn& image)` | 创建进程 | `image`：程序映像或执行选项 |
| `int close()` | 关闭对象连接 | — |
| `int terminate(int exitCode)` | 请求强制结束绑定进程 | `exitCode`：进程或线程退出码 |
| `int wait(int& exitCode)` | 等待本对象对应的文件请求完成 | `exitCode`：进程或线程退出码 |
| `int next(uint64_t minimumId, Task& task)` | 按游标枚举任务或恢复域中的资源 | `minimumId`：枚举的起始编号<br>`task`：目标进程 |
| `int querySignalMask(uint64_t& mask)` | 读取目标支持查询的当前信号屏蔽集合 | `mask`：信号集合 |
| `int queryPendingSignals(uint64_t& mask)` | 查询待处理信号集合 | `mask`：信号集合 |
| `int querySignalStack(SignalStack& stack)` | 读取当前配置的备用信号栈 | `stack`：线程栈或备用信号栈 |
| `int queryCorePolicy(Core& policy)` | 读取core转储是否启用、大小及并发等限制 | `policy`：核心转储策略 |
| `int queryResourceLimit(int resource, uint64_t& soft, uint64_t& hard)` | 读取指定资源的软限制和硬限制 | `resource`：资源限制类型<br>`soft`：资源软限制<br>`hard`：资源硬限制 |
| `int sendSignal(uint32_t signal)` | 向绑定的进程或线程发送信号 | `signal`：信号编号 |
| `int waitSignal(uint64_t mask)` | 等待屏蔽集合中的一个信号 | `mask`：信号集合 |
| `int querySignalAction(uint32_t signal, Signal& action)` | 读取指定信号当前的处理方式 | `signal`：信号编号<br>`action`：信号处理设置 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |
| `uint32_t getState() const` | 读取系统或用户的持久状态目录路径 | — |
| `int32_t getExitCode() const` | 读取进程或线程的退出码 | — |
| `const char* getName() const` | 读取名称 | — |
| `uint32_t getUid() const` | 读取真实用户编号 | — |
| `uint32_t getGid() const` | 读取真实用户组编号 | — |
| `uint32_t getEffectiveUid() const` | 读取实际用于权限检查的用户编号 | — |
| `uint32_t getEffectiveGid() const` | 读取实际用于权限检查的用户组编号 | — |
| `uint64_t getParentId() const` | 读取父块设备编号 | — |
| `uint32_t getTrustedDeviceService() const` | 读取目标是否具有受控设备服务身份 | — |
| `uint32_t getServiceRole() const` | 读取目标的系统服务角色 | — |
| `uint64_t getGroupId() const` | 读取进程组编号 | — |
| `uint64_t getSessionId() const` | 读取会话编号 | — |
| `uint64_t getTerminalForegroundGroup() const` | 读取控制终端的前台进程组编号 | — |
| `uint64_t getStartedNanoseconds() const` | 读取启动时间 | — |
| `uint64_t getCpuNanoseconds() const` | 读取已消耗的CPU时间 | — |
| `uint32_t getTerminalKind() const` | 读取控制终端的种类 | — |
| `uint32_t getTerminalIndex() const` | 读取控制终端的编号 | — |
| `uint32_t getCommandLineSize() const` | 读取命令行有效字节数 | — |
| `uint32_t getManagedService() const` | 读取是否是init管理的服务 | — |
| `const char* getCommandLine() const` | 读取启动命令行 | — |
| `uint64_t getMainThreadId() const` | 读取主线程编号 | — |
| `int32_t getNice() const` | 读取调度优先级调整值 | — |
| `uint32_t getThreadCount() const` | 读取当前线程数量 | — |
| `uint64_t getId() const` | 读取编号 | — |
| `uint32_t getExitSignal() const` | 读取导致进程退出的信号 | — |
| `Core::State getExitCoreState() const` | 读取退出时的core转储状态 | — |
| `uint64_t getExitFaultThread() const` | 读取发生异常的线程编号 | — |
| `uint64_t getExitVector() const` | 读取处理器异常向量号 | — |
| `uint64_t getExitError() const` | 读取处理器异常错误码 | — |
| `uint64_t getExitRip() const` | 读取异常时的指令地址 | — |
| `uint64_t getExitRsp() const` | 读取异常时的栈地址 | — |
| `uint64_t getExitAddress() const` | 读取异常访问的地址 | — |
| `uint64_t getExitStarted() const` | 读取发生异常的时间 | — |
| `uint32_t getExitAddressValid() const` | 读取异常地址是否有效 | — |
| `int32_t getExitSignalCode() const` | 读取退出信号的具体原因 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setNice(int value)` | 设置调度优先级调整值 | `value`：调度优先级调整值 |
| `int setGroup(uint64_t group)` | 设置指定位置的补充组编号 | `group`：进程组编号 |
| `int setSignalMask(int how, uint64_t mask)` | 设置当前信号屏蔽集合 | `how`：信号屏蔽集合的修改方式<br>`mask`：信号集合 |
| `int setSignalStack(const SignalStack& stack)` | 设置备用信号栈 | `stack`：线程栈或备用信号栈 |
| `int setCorePolicy(const Core& policy)` | 设置core转储配置 | `policy`：核心转储策略 |
| `int setResourceLimit(int resource, uint64_t soft, uint64_t hard)` | 设置资源软限制和硬限制 | `resource`：资源限制类型<br>`soft`：资源软限制<br>`hard`：资源硬限制 |
| `int setSignalAction(uint32_t signal, const Signal& action)` | 设置指定信号的处理方式 | `signal`：信号编号<br>`action`：信号处理设置 |

### os::Task::Image

进程映像对象，保存映像内容和启动参数。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `const void* getImage() const` | 读取映像字节的借用地址 | — |
| `uint64_t getImageSize() const` | 读取映像字节数 | — |
| `const char* getName() const` | 读取名称 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setImage(const void* image, uint64_t size)` | 设置映像字节的借用地址 | `image`：程序映像或执行选项<br>`size`：数据大小，单位为字节 |
| `void setName(const char* value)` | 设置名称 | `value`：名称 |

### os::Task::Spawn

进程创建对象，保存新进程的程序、参数和资源设置。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `const void* getImage() const` | 读取映像字节的借用地址 | — |
| `uint64_t getImageSize() const` | 读取映像字节数 | — |
| `const char* getName() const` | 读取名称 | — |
| `uint64_t getBootstrapHandle() const` | 读取服务注册表端点 | — |
| `uint64_t getStandardInputHandle() const` | 读取新程序标准输入句柄 | — |
| `uint64_t getStandardOutputHandle() const` | 读取新程序标准输出句柄 | — |
| `uint64_t getStandardErrorHandle() const` | 读取新程序标准错误句柄 | — |
| `uint64_t getCapabilities() const` | 读取请求授予新程序的能力位 | — |
| `uint64_t getParentTaskId() const` | 读取受控启动请求中的父任务编号 | — |
| `uint32_t getArgumentCount() const` | 读取程序参数数量 | — |
| `uint32_t getServiceRole() const` | 读取目标的系统服务角色 | — |
| `const char* const* getArguments() const` | 读取程序参数字符串数组 | — |
| `uint32_t getSharedImageCount() const` | 读取共享映像数量 | — |
| `uint32_t getFlags() const` | 读取当前标志位 | — |
| `uint32_t getTerminalKind() const` | 读取控制终端的种类 | — |
| `uint32_t getTerminalIndex() const` | 读取控制终端的编号 | — |
| `const Image* getSharedImages() const` | 读取共享映像数组 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setImage(const void* image, uint64_t size)` | 设置映像字节的借用地址 | `image`：程序映像或执行选项<br>`size`：数据大小，单位为字节 |
| `void setName(const char* value)` | 设置名称 | `value`：名称 |
| `void setBootstrapHandle(uint64_t value)` | 设置服务注册表端点 | `value`：服务注册表端点 |
| `void setStandardInputHandle(uint64_t value)` | 设置新程序标准输入句柄 | `value`：新程序标准输入句柄 |
| `void setStandardOutputHandle(uint64_t value)` | 设置新程序标准输出句柄 | `value`：新程序标准输出句柄 |
| `void setStandardErrorHandle(uint64_t value)` | 设置新程序标准错误句柄 | `value`：新程序标准错误句柄 |
| `void setCapabilities(uint64_t value)` | 设置请求授予新程序的能力位 | `value`：请求授予新程序的能力位 |
| `void setParentTaskId(uint64_t value)` | 设置受控启动请求中的父任务编号 | `value`：受控启动请求中的父任务编号 |
| `void setServiceRole(uint32_t value)` | 设置目标的系统服务角色 | `value`：目标的系统服务角色 |
| `void setArguments(const char* const* arguments, uint32_t count)` | 设置程序参数字符串数组 | `arguments`：程序启动参数数组<br>`count`：程序启动参数数量 |
| `void setFlags(uint32_t value)` | 设置当前标志位 | `value`：当前标志位 |
| `void setTerminalKind(uint32_t value)` | 设置控制终端的种类 | `value`：控制终端的种类 |
| `void setTerminalIndex(uint32_t value)` | 设置控制终端的编号 | `value`：控制终端的编号 |
| `void setSharedImages(const Image* images, uint32_t count)` | 设置共享映像数组 | `images`：共享映像数组<br>`count`：共享映像数量 |

### os::Task::Exec

程序执行对象，保存映像替换所需的路径、参数和环境。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `const void* getImage() const` | 读取映像字节的借用地址 | — |
| `uint64_t getImageSize() const` | 读取映像字节数 | — |
| `const char* getName() const` | 读取名称 | — |
| `uint32_t getArgumentCount() const` | 读取程序参数数量 | — |
| `uint32_t getEnvironmentCount() const` | 读取环境字符串数量 | — |
| `const char* const* getArguments() const` | 读取程序参数字符串数组 | — |
| `const char* const* getEnvironment() const` | 读取环境字符串数组 | — |
| `const void* getRuntimeState() const` | 读取exec时交接的运行时数据地址 | — |
| `uint64_t getRuntimeStateSize() const` | 读取运行时交接数据字节数 | — |
| `uint32_t getSharedImageCount() const` | 读取共享映像数量 | — |
| `const Image* getSharedImages() const` | 读取共享映像数组 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setImage(const void* image, uint64_t size)` | 设置映像字节的借用地址 | `image`：程序映像或执行选项<br>`size`：数据大小，单位为字节 |
| `void setName(const char* value)` | 设置名称 | `value`：名称 |
| `void setArguments(const char* const* arguments, uint32_t count)` | 设置程序参数字符串数组 | `arguments`：程序启动参数数组<br>`count`：程序启动参数数量 |
| `void setEnvironment(const char* const* environment, uint32_t count)` | 设置环境字符串数组 | `environment`：进程环境变量数组<br>`count`：项目数量 |
| `void setRuntimeState(const void* value)` | 设置exec时交接的运行时数据地址 | `value`：exec时交接的运行时数据地址 |
| `void setRuntimeStateSize(uint64_t value)` | 设置运行时交接数据字节数 | `value`：运行时交接数据字节数 |
| `void setSharedImages(const Image* images, uint32_t count)` | 设置共享映像数组 | `images`：共享映像数组<br>`count`：共享映像数量 |

### os::Task::Core

进程转储对象，保存转储结果和映像信息。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `State getState() const` | 读取系统或用户的持久状态目录路径 | — |
| `bool getEnabled() const` | 读取是否允许该服务自动启动 | — |
| `uint64_t getMaximumBytes() const` | 读取单份core转储的最大字节数 | — |
| `uint64_t getMaximumConcurrent() const` | 读取允许同时进行的core转储数量 | — |
| `uint64_t getTimeoutMilliseconds() const` | 读取core操作超时毫秒数 | — |
| `uint64_t getId() const` | 读取编号 | — |
| `uint64_t getTaskId() const` | 读取所属或对应的进程编号 | — |
| `uint64_t getStarted() const` | 读取core操作开始时间 | — |
| `uint64_t getBytes() const` | 读取已生成的core字节数 | — |
| `uint64_t getDeadline() const` | 读取操作截止时间 | — |
| `uint32_t getSignal() const` | 读取相关信号编号 | — |
| `bool getTruncated() const` | 读取core内容是否被截断 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setEnabled(bool value)` | 设置是否允许该服务自动启动 | `value`：是否允许该服务自动启动 |
| `void setMaximumBytes(uint64_t value)` | 设置单份core转储的最大字节数 | `value`：单份core转储的最大字节数 |
| `void setMaximumConcurrent(uint64_t value)` | 设置允许同时进行的core转储数量 | `value`：允许同时进行的core转储数量 |
| `void setTimeoutMilliseconds(uint64_t value)` | 设置core操作超时毫秒数 | `value`：core操作超时毫秒数 |

### os::Task::Signal

信号对象，保存信号动作和投递参数。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getHandler() const` | 读取信号处理入口地址 | — |
| `uint64_t getMask() const` | 读取信号屏蔽位集合 | — |
| `uint64_t getFlags() const` | 读取当前标志位 | — |
| `uint64_t getRestorer() const` | 读取信号处理结束时使用的返回入口 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setHandler(uint64_t value)` | 设置信号处理入口地址 | `value`：信号处理入口地址 |
| `void setMask(uint64_t value)` | 设置信号屏蔽位集合 | `value`：信号屏蔽位集合 |
| `void setFlags(uint64_t value)` | 设置当前标志位 | `value`：当前标志位 |
| `void setRestorer(uint64_t value)` | 设置信号处理结束时使用的返回入口 | `value`：信号处理结束时使用的返回入口 |

### os::Task::Credential

进程身份对象，保存用户、组和附加组信息。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit Credential(const Task& task)` | 构造Credential对象 | `task`：目标进程 |
| `int refresh()` | 更新对象属性 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint32_t getMaximumGroups() const` | 读取当前允许的最多补充组数量 | — |
| `uint32_t getUid() const` | 读取真实用户编号 | — |
| `uint32_t getEffectiveUid() const` | 读取实际用于权限检查的用户编号 | — |
| `uint32_t getSavedUid() const` | 读取保存的用户编号 | — |
| `uint32_t getGid() const` | 读取真实用户组编号 | — |
| `uint32_t getEffectiveGid() const` | 读取实际用于权限检查的用户组编号 | — |
| `uint32_t getSavedGid() const` | 读取保存的用户组编号 | — |
| `uint32_t getGroupCount() const` | 读取补充组数量 | — |
| `uint32_t getGroup(uint32_t index) const` | 读取指定位置的补充组编号 | `index`：项目索引 |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setUserIds(uint32_t real, uint32_t effective, uint32_t saved)` | 设置真实、有效和保存的用户编号 | `real`：真实身份编号<br>`effective`：有效身份编号<br>`saved`：保存的身份编号 |
| `int setGroupIds(uint32_t real, uint32_t effective, uint32_t saved)` | 设置真实、有效和保存的用户组编号 | `real`：真实身份编号<br>`effective`：有效身份编号<br>`saved`：保存的身份编号 |
| `int setGroups(const uint32_t* groups, uint32_t count)` | 设置补充用户组列表 | `groups`：附加用户组编号数组<br>`count`：附加用户组数量 |

### os::Task::SignalStack

信号栈对象，保存备用栈地址、容量和状态。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getAddress() const` | 读取映射开始的虚拟地址 | — |
| `uint64_t getSize() const` | 读取长度 | — |
| `uint32_t getFlags() const` | 读取当前标志位 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setAddress(uint64_t value)` | 设置映射开始的虚拟地址 | `value`：映射开始的虚拟地址 |
| `void setSize(uint64_t value)` | 设置长度 | `value`：长度 |
| `void setFlags(uint32_t value)` | 设置当前标志位 | `value`：当前标志位 |

### os::Task::Child

子进程对象，保存等待结果和退出状态。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getId() const` | 读取编号 | — |
| `uint32_t getUid() const` | 读取真实用户编号 | — |
| `uint32_t getCode() const` | 读取子进程事件原因 | — |
| `int32_t getStatus() const` | 读取异步文件操作的完成状态 | — |

### os::Task::ImageMapping

映像映射对象，保存进程映像的地址范围和文件信息。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getBegin() const` | 读取映像映射开始地址 | — |
| `uint64_t getEnd() const` | 读取映像映射结束地址 | — |
| `uint64_t getOffset() const` | 读取当前偏移位置 | — |
| `const char* getPath() const` | 读取指定种类的系统或用户约定目录路径 | — |

### os::Task::Thread

线程对象，保存线程连接、入口、调度信息和执行状态。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Thread()` | 构造Thread对象 | — |
| `~Thread()` | 析构Thread对象 | — |
| `Thread& operator=(Thread&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int refresh()` | 更新对象属性 | — |
| `int open(uint64_t taskId, uint64_t threadId)` | 打开对象连接 | `taskId`：进程编号<br>`threadId`：线程编号 |
| `int openCurrent()` | 绑定当前进程使用的控制台 | — |
| `int create(void (*entry)(void*), void* argument, void* stack, size_t stackSize)` | 创建线程 | `entry`：线程入口函数<br>`argument`：传给线程入口的上下文<br>`stack`：线程栈或备用信号栈<br>`stackSize`：线程栈大小，单位为字节 |
| `int close()` | 关闭对象连接 | — |
| `int wait(int& exitCode)` | 等待本对象对应的文件请求完成 | `exitCode`：进程或线程退出码 |
| `int sendSignal(uint32_t signal)` | 向绑定的进程或线程发送信号 | `signal`：信号编号 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getId() const` | 读取编号 | — |
| `uint64_t getTaskId() const` | 读取所属或对应的进程编号 | — |
| `uint64_t getCpuNanoseconds() const` | 读取已消耗的CPU时间 | — |
| `int32_t getNice() const` | 读取调度优先级调整值 | — |
| `uint32_t getState() const` | 读取系统或用户的持久状态目录路径 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setNice(int value)` | 设置调度优先级调整值 | `value`：调度优先级调整值 |

### os::Task::Session

进程会话对象，保存会话编号、进程组和控制终端。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Session()` | 构造Session对象 | — |
| `~Session()` | 析构Session对象 | — |
| `Session& operator=(Session&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int refresh()` | 更新对象属性 | — |
| `int openCurrent()` | 绑定当前进程使用的控制台 | — |
| `int create()` | 创建进程会话 | — |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getId() const` | 读取编号 | — |
| `uint64_t getLeaderId() const` | 读取会话首进程编号 | — |
| `uint64_t getForegroundGroup() const` | 读取控制终端的前台进程组编号 | — |
| `uint32_t getTerminalKind() const` | 读取控制终端的种类 | — |
| `uint32_t getTerminalIndex() const` | 读取控制终端的编号 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setForegroundGroup(uint64_t group)` | 设置控制终端的前台进程组编号 | `group`：进程组编号 |
