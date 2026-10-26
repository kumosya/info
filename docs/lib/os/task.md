# os/task.h


## 内容与边界

原 TaskService 与 Task 合并为 Task，不同时保留两个 Task 或用继承重复公开全部接口。openCurrent 绑定当前进程，open(id) 绑定指定进程，create(image) 显式创建子进程，next 枚举，refresh 查询；sendSignal、terminate、wait 操作已绑定目标。关闭引用不终止进程。

getter 读取身份、父任务、状态、名称、凭据、会话、进程组、终端、运行时间、CPU 时间、主线程、线程数、nice 和命令行。refreshExit 后逐字段读取退出信号、异常和 core 状态。setNice、setGroup 等真实修改由服务校验权限。

| 嵌套类 | 功能 |
| --- | --- |
| Task::Thread | 线程绑定、创建、等待、信号、状态和 nice |
| Task::Session | 会话绑定、创建、刷新、控制终端和前台进程组 |
| Task::Credential | 目标凭据查询及受权限检查的身份修改 |
| Task::Image | 共享映像视图、长度及名称 |
| Task::Spawn、Task::Exec | 启动与替换映像参数，指针与数量成组维护 |
| Task::Signal、Task::SignalStack | 信号处理及备用栈配置 |
| Task::Child | 子进程事件结果 |
| Task::Core | core 转储配置、执行进度和结果；阶段枚举为 Core::State |
| Task::ImageMapping | 映像路径与地址范围结果 |

父进程、进程组和会话编号直接由 Task::getParentId、getGroupId、getSessionId 读取，不保留 Ids。

信号处理、屏蔽集、备用栈和资源限制的查询与修改分别使用 query、set 函数，不使用 bool set 切换操作。

fork、exec、当前进程/线程退出、yield、sleep 保持明确的当前上下文入口，通过服务 IPC 实现；运行时的 signalReturn 和装载信息登记不作为任意目标的普通应用操作。

## 枚举

### Task::Core::State

底层类型：`uint32_t`。嵌套在 Task::Core 中，表示一次 core 转储所处阶段；各成员数值保持不变。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `None` | `0` | 没有 core 转储 |
| `Requested` | `1` | 已请求生成 core |
| `Capturing` | `2` | 正在捕获任务状态 |
| `Ready` | `3` | 捕获完成，等待写出 |
| `Writing` | `4` | 正在写入 core |
| `Complete` | `5` | core 已完成 |
| `Truncated` | `6` | core 因大小等限制被截断 |
| `Failed` | `7` | core 捕获或写入失败 |
| `Skipped` | `8` | 按策略跳过 core |
| `TimedOut` | `9` | core 操作超时 |

### Task::Spawn::Flag

底层类型：`uint32_t`。后者是受控服务启动请求，普通程序不能靠置位获得服务身份。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `NewSession` | `1` | 为新进程请求新会话 |
| `ManagedService` | `2` | 请求受控管理服务启动 |

ProgramId、ServiceRole 是受控服务启动及身份定义，完整定义在 include/service/common/identity.h。它们不因在当前 task.h 中有前置声明就成为推荐应用枚举；普通应用通过路径启动，不填写内部程序目录编号。

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

| 当前名字 | 当前值或表达式 | 数据含义 | 推荐归属 |
| --- | --- | --- | --- |
| `maximumSupplementaryGroups` | `32` | 当前允许的最多补充组数量 | 私有常量；通过 Task::Credential::getMaximumGroups() 查询 |
| `spawnFlagNewSession` | `1` | 新会话启动标志 | 改为 Task::Spawn::Flag::NewSession |
| `spawnFlagManagedService` | `2` | 受控服务启动标志 | 改为 Task::Spawn::Flag::ManagedService |
| `execRuntimeStateAddress` | `0x30000` | exec 运行时交接区的当前约定地址 | 运行时与加载器内部约定，不作为普通应用地址常量 |
| `taskCapabilityDisplay` | `4` | 显示设备相关能力请求位 | 受控服务能力约定，移入服务头文件，置位不会自动授权 |
| `taskCapabilityUsers` | `8` | 账户管理相关能力请求位 | 受控服务能力约定，移入服务头文件 |

```cpp
uint32_t Task::Credential::getMaximumGroups() const;
```

数据：当前允许的最多补充组数量，现值为 32。

## 全局变量

当前公共头文件没有导出可写全局变量。进程与线程身份通过对象查询；启动参数、信号和退出快照不放全局。

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

旧 TaskHandle 是 uint64_t 别名，普通 C++ 调用改用 Task 对象。C ABI 需要原始编号时使用明确的 uint64_t 字段，不新增一个可伪装资源对象的别名。

## 类与函数

### Task
这个类表示一个进程引用及其查询、控制和退出结果。

#### 构造与析构

```cpp
~Task();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。释放引用不终止进程、线程或仍在使用的会话。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Task();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Task& operator=(Task&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Task&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int refreshExit();
```

用途：读取目标进程的退出信号、异常位置和 core 状态，供 getExit 系列函数使用。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int sendSignal(uint32_t signal);
```

用途：向绑定的进程或线程发送信号，由服务检查发送权限。

参数：

- `uint32_t signal`：信号编号；0 的特殊检查语义仅在对应接口明确支持时使用。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int terminate(int exitCode);
```

用途：请求强制结束绑定进程，并指定退出码。

参数：

- `int exitCode`：指定退出码或用于接收退出码的输出引用。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int wait(int& exitCode);
```

用途：等待线程退出、输入事件、文件完成或地址值变化；具体等待对象由当前类决定。

参数：

- `int& exitCode`：指定退出码或用于接收退出码的输出引用。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int querySignalAction(uint32_t signal, Task::Signal& action);
```

用途：读取指定信号当前的处理方式。

参数：

- `uint32_t signal`：信号编号；0 的特殊检查语义仅在对应接口明确支持时使用。
- `Task::Signal& action`：要设置的信号处理参数，或用于读取的输出对象。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int querySignalMask(uint64_t& mask);
```

用途：读取目标支持查询的当前信号屏蔽集合。

参数：

- `uint64_t& mask`：信号位集合，每一位对应一个信号。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int queryPendingSignals(uint64_t& mask);
```

用途：读取已经产生但尚未处理的信号集合。

参数：

- `uint64_t& mask`：信号位集合，每一位对应一个信号。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int waitSignal(uint64_t mask);
```

用途：等待屏蔽集合中的一个信号，返回实际收到的信号或错误。

参数：

- `uint64_t mask`：信号位集合，每一位对应一个信号。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int querySignalStack(Task::SignalStack& stack);
```

用途：读取当前配置的备用信号栈。

参数：

- `Task::SignalStack& stack`：备用信号栈或线程栈的借用内存。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int queryCorePolicy(Task::Core& policy);
```

用途：读取 core 转储是否启用、大小及并发等限制。

参数：

- `Task::Core& policy`：core 转储配置对象，查询时为输出。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int queryResourceLimit(int resource, uint64_t& soft, uint64_t& hard);
```

用途：读取指定资源的软限制和硬限制。

参数：

- `int resource`：资源限制项目编号。
- `uint64_t& soft`：可供程序使用的当前软限制，查询时为输出。
- `uint64_t& hard`：允许设置的硬限制上限，查询时为输出。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int next(uint64_t minimumId, Task& task);
```

用途：按游标枚举任务或恢复域中的资源，成功后更新输出。

参数：

- `uint64_t minimumId`：从此编号开始查找，包括该编号本身。
- `Task& task`：已绑定的目标进程；需要时借用其身份或引用。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(uint64_t id);
```

用途：按编号绑定进程，或按启动参数创建新进程。

参数：

- `uint64_t id`：要绑定的资源编号，含义由所属类确定。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int create(const Task::Spawn& image);
```

用途：按编号绑定进程，或按启动参数创建新进程。

参数：

- `const Task::Spawn& image`：可执行映像的借用字节或已整理的启动参数，不由函数自动释放。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int openCurrent();
```

用途：获取当前进程的引用并查询当前身份。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int close();
```

用途：释放进程引用，不终止进程。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint32_t getState() const;
```

数据：对象最近一次查询到的状态。

```cpp
int32_t getExitCode() const;
```

数据：进程或线程的退出码。

```cpp
const char* getName() const;
```

数据：名称。

```cpp
uint32_t getUid() const;
```

数据：真实用户编号。

```cpp
uint32_t getGid() const;
```

数据：真实用户组编号。

```cpp
uint32_t getEffectiveUid() const;
```

数据：实际用于权限检查的用户编号。

```cpp
uint32_t getEffectiveGid() const;
```

数据：实际用于权限检查的用户组编号。

```cpp
uint64_t getParentId() const;
```

数据：父进程编号。

```cpp
uint32_t getTrustedDeviceService() const;
```

数据：目标是否具有受控设备服务身份。

```cpp
uint32_t getServiceRole() const;
```

数据：目标的系统服务角色。

```cpp
uint64_t getGroupId() const;
```

数据：进程组编号。

```cpp
uint64_t getSessionId() const;
```

数据：会话编号。

```cpp
uint64_t getTerminalForegroundGroup() const;
```

数据：控制终端的前台进程组编号。

```cpp
uint64_t getStartedNanoseconds() const;
```

数据：启动时间，按单调时钟计纳秒。

```cpp
uint64_t getCpuNanoseconds() const;
```

数据：已消耗的 CPU 时间，单位为纳秒。

```cpp
uint32_t getTerminalKind() const;
```

数据：控制终端的种类。

```cpp
uint32_t getTerminalIndex() const;
```

数据：控制终端的编号。

```cpp
uint32_t getCommandLineSize() const;
```

数据：命令行有效字节数。

```cpp
uint32_t getManagedService() const;
```

数据：是否是 init 管理的服务。

```cpp
const char* getCommandLine() const;
```

数据：启动命令行。

```cpp
uint64_t getMainThreadId() const;
```

数据：主线程编号。

```cpp
int32_t getNice() const;
```

数据：调度优先级调整值，数值越大通常优先级越低。

```cpp
uint32_t getThreadCount() const;
```

数据：当前线程数量。

```cpp
uint64_t getId() const;
```

数据：编号。

```cpp
uint32_t getExitSignal() const;
```

数据：导致进程退出的信号。

```cpp
Task::Core::State getExitCoreState() const;
```

数据：退出时的 core 转储状态。

```cpp
uint64_t getExitFaultThread() const;
```

数据：发生异常的线程编号。

```cpp
uint64_t getExitVector() const;
```

数据：处理器异常向量号。

```cpp
uint64_t getExitError() const;
```

数据：处理器异常错误码。

```cpp
uint64_t getExitRip() const;
```

数据：异常时的指令地址。

```cpp
uint64_t getExitRsp() const;
```

数据：异常时的栈地址。

```cpp
uint64_t getExitAddress() const;
```

数据：异常访问的地址。

```cpp
uint64_t getExitStarted() const;
```

数据：发生异常的时间。

```cpp
uint32_t getExitAddressValid() const;
```

数据：异常地址是否有效。

```cpp
int32_t getExitSignalCode() const;
```

数据：退出信号的具体原因。

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
int setNice(int value);
```

数据：nice 值，由服务检查提高清优先级的权限或资源限制。

```cpp
int setGroup(uint64_t group);
```

数据：进程组归属。

```cpp
int setSignalAction(uint32_t signal, const Task::Signal& action);
```

数据：指定信号的处理方式。

```cpp
int setSignalMask(int how, uint64_t mask);
```

数据：当前信号屏蔽集合。

```cpp
int setSignalStack(const Task::SignalStack& stack);
```

数据：备用信号栈。

```cpp
int setCorePolicy(const Task::Core& policy);
```

数据：core 转储配置。

```cpp
int setResourceLimit(int resource, uint64_t soft, uint64_t hard);
```

数据：资源软限制和硬限制。

### Task::Thread
这个类表示一个线程引用及其等待、信号和调度属性。

#### 构造与析构

```cpp
~Thread();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。释放引用不终止进程、线程或仍在使用的会话。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Thread();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Task::Thread& operator=(Task::Thread&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Task::Thread&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int wait(int& exitCode);
```

用途：等待线程退出、输入事件、文件完成或地址值变化；具体等待对象由当前类决定。

参数：

- `int& exitCode`：指定退出码或用于接收退出码的输出引用。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int sendSignal(uint32_t signal);
```

用途：向绑定的进程或线程发送信号，由服务检查发送权限。

参数：

- `uint32_t signal`：信号编号；0 的特殊检查语义仅在对应接口明确支持时使用。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(uint64_t taskId, uint64_t threadId);
```

用途：按编号绑定线程，或使用给定入口和借用栈创建线程。

参数：

- `uint64_t taskId`：目标进程编号。
- `uint64_t threadId`：目标线程编号；必须结合所属任务或可用引用解释。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int create(void (*entry)(void*), void* argument, void* stack, size_t stackSize);
```

用途：按编号绑定线程，或使用给定入口和借用栈创建线程。

参数：

- `void (*entry)(void*)`：新线程开始执行的函数，库在新线程中调用 entry(argument)。
- `void* argument`：传给新线程入口的指针，库不接管该指针所指数据。
- `void* stack`：新线程栈的借用内存，线程结束前不得释放；对象析构不释放借用栈。
- `size_t stackSize`：线程栈缓冲区的字节数。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int openCurrent();
```

用途：绑定当前线程，获取有效引用。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int close();
```

用途：释放线程引用，不结束线程，也不释放借用的线程栈。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getId() const;
```

数据：编号。

```cpp
uint64_t getTaskId() const;
```

数据：所属或对应的进程编号。

```cpp
uint64_t getCpuNanoseconds() const;
```

数据：已消耗的 CPU 时间，单位为纳秒。

```cpp
int32_t getNice() const;
```

数据：调度优先级调整值，数值越大通常优先级越低。

```cpp
uint32_t getState() const;
```

数据：对象最近一次查询到的状态。

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
int setNice(int value);
```

数据：nice 值，由服务检查提高清优先级的权限或资源限制。

### Task::Session
这个类表示当前或指定会话的标识及控制终端信息。

#### 构造与析构

```cpp
~Session();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。释放引用不终止进程、线程或仍在使用的会话。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Session();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Task::Session& operator=(Task::Session&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Task::Session&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int openCurrent();
```

用途：查询并绑定当前进程所在会话。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int create();
```

用途：为当前进程请求新会话；不满足会话创建条件时返回错误。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int close();
```

用途：释放本对象的会话引用，不销毁仍有其他成员的会话。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getId() const;
```

数据：编号。

```cpp
uint64_t getLeaderId() const;
```

数据：会话首进程编号。

```cpp
uint64_t getForegroundGroup() const;
```

数据：控制终端的前台进程组编号。

```cpp
uint32_t getTerminalKind() const;
```

数据：控制终端的种类。

```cpp
uint32_t getTerminalIndex() const;
```

数据：控制终端的编号。

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
int setForegroundGroup(uint64_t group);
```

数据：控制终端前台进程组。

### Task::Credential

这个类表示指定任务的用户、用户组及补充组快照。

#### 构造与析构

```cpp
explicit Credential(const Task& task);
```

用途：只在本地记录目标任务的借用引用，初始化空的凭据缓存；不发送查询请求。使用前调用 refresh 读取用户、用户组及补充组。

参数：

- `const Task& task`：已绑定的目标进程；需要时借用其身份或引用。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### 操作

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

#### getter

```cpp
uint32_t getUid() const;
```

数据：真实用户编号。

```cpp
uint32_t getEffectiveUid() const;
```

数据：实际用于权限检查的用户编号。

```cpp
uint32_t getSavedUid() const;
```

数据：保存的用户编号。

```cpp
uint32_t getGid() const;
```

数据：真实用户组编号。

```cpp
uint32_t getEffectiveGid() const;
```

数据：实际用于权限检查的用户组编号。

```cpp
uint32_t getSavedGid() const;
```

数据：保存的用户组编号。

```cpp
uint32_t getGroupCount() const;
```

数据：补充组数量。

```cpp
uint32_t getGroup(uint32_t index) const;
```

数据：指定位置的补充组编号。

#### setter

```cpp
int setUserIds(uint32_t real, uint32_t effective, uint32_t saved);
```

数据：真实、有效和保存的用户编号。

```cpp
int setGroupIds(uint32_t real, uint32_t effective, uint32_t saved);
```

数据：真实、有效和保存的用户组编号。

```cpp
int setGroups(const uint32_t* groups, uint32_t count);
```

数据：补充用户组列表。

### Task::Image

这个类表示一段启动时共享的 ELF 映像视图。

#### getter

```cpp
const void* getImage() const;
```

数据：映像字节的借用地址。

```cpp
uint64_t getImageSize() const;
```

数据：映像字节数。

```cpp
const char* getName() const;
```

数据：名称。

#### setter

```cpp
void setName(const char* value);
```

数据：名称。

```cpp
void setImage(const void* image, uint64_t size);
```

数据：映像字节的借用地址。

### Task::Spawn

这个类表示创建新进程所需的映像、参数、标准输入输出和授权请求。

#### getter

```cpp
const void* getImage() const;
```

数据：映像字节的借用地址。

```cpp
uint64_t getImageSize() const;
```

数据：映像字节数。

```cpp
const char* getName() const;
```

数据：名称。

```cpp
uint64_t getBootstrapHandle() const;
```

数据：传给新程序的引导端点。

```cpp
uint64_t getStandardInputHandle() const;
```

数据：新程序标准输入句柄。

```cpp
uint64_t getStandardOutputHandle() const;
```

数据：新程序标准输出句柄。

```cpp
uint64_t getStandardErrorHandle() const;
```

数据：新程序标准错误句柄。

```cpp
uint64_t getCapabilities() const;
```

数据：请求授予新程序的能力位。

```cpp
uint64_t getParentTaskId() const;
```

数据：受控启动请求中的父任务编号。

```cpp
uint32_t getArgumentCount() const;
```

数据：程序参数数量。

```cpp
uint32_t getServiceRole() const;
```

数据：目标的系统服务角色。

```cpp
const char* const* getArguments() const;
```

数据：程序参数字符串数组。

```cpp
uint32_t getSharedImageCount() const;
```

数据：共享映像数量。

```cpp
uint32_t getFlags() const;
```

数据：当前标志位。

```cpp
uint32_t getTerminalKind() const;
```

数据：控制终端的种类。

```cpp
uint32_t getTerminalIndex() const;
```

数据：控制终端的编号。

```cpp
const Task::Image* getSharedImages() const;
```

数据：共享映像数组。

#### setter

```cpp
void setName(const char* value);
```

数据：名称。

```cpp
void setBootstrapHandle(uint64_t value);
```

数据：传给新程序的引导端点。

```cpp
void setStandardInputHandle(uint64_t value);
```

数据：新程序标准输入句柄。

```cpp
void setStandardOutputHandle(uint64_t value);
```

数据：新程序标准输出句柄。

```cpp
void setStandardErrorHandle(uint64_t value);
```

数据：新程序标准错误句柄。

```cpp
void setCapabilities(uint64_t value);
```

数据：请求授予新程序的能力位。

```cpp
void setParentTaskId(uint64_t value);
```

数据：受控启动请求中的父任务编号。

```cpp
void setServiceRole(uint32_t value);
```

数据：目标的系统服务角色。

```cpp
void setFlags(uint32_t value);
```

数据：资源标志位。

```cpp
void setTerminalKind(uint32_t value);
```

数据：控制终端的种类。

```cpp
void setTerminalIndex(uint32_t value);
```

数据：控制终端的编号。

```cpp
void setImage(const void* image, uint64_t size);
```

数据：映像字节的借用地址。

```cpp
void setArguments(const char* const* arguments, uint32_t count);
```

数据：程序参数字符串数组。

```cpp
void setSharedImages(const Task::Image* images, uint32_t count);
```

数据：共享映像数组。

### Task::Exec

这个类表示替换当前程序所需的映像、参数、环境和运行时交接数据。

#### getter

```cpp
const void* getImage() const;
```

数据：映像字节的借用地址。

```cpp
uint64_t getImageSize() const;
```

数据：映像字节数。

```cpp
const char* getName() const;
```

数据：名称。

```cpp
uint32_t getArgumentCount() const;
```

数据：程序参数数量。

```cpp
uint32_t getEnvironmentCount() const;
```

数据：环境字符串数量。

```cpp
const char* const* getArguments() const;
```

数据：程序参数字符串数组。

```cpp
const char* const* getEnvironment() const;
```

数据：环境字符串数组。

```cpp
const void* getRuntimeState() const;
```

数据：exec 时交接的运行时数据地址。

```cpp
uint64_t getRuntimeStateSize() const;
```

数据：运行时交接数据字节数。

```cpp
uint32_t getSharedImageCount() const;
```

数据：共享映像数量。

```cpp
const Task::Image* getSharedImages() const;
```

数据：共享映像数组。

#### setter

```cpp
void setName(const char* value);
```

数据：名称。

```cpp
void setRuntimeState(const void* value);
```

数据：exec 时交接的运行时数据地址。

```cpp
void setRuntimeStateSize(uint64_t value);
```

数据：运行时交接数据字节数。

```cpp
void setImage(const void* image, uint64_t size);
```

数据：映像字节的借用地址。

```cpp
void setArguments(const char* const* arguments, uint32_t count);
```

数据：程序参数字符串数组。

```cpp
void setSharedImages(const Task::Image* images, uint32_t count);
```

数据：共享映像数组。

```cpp
void setEnvironment(const char* const* environment, uint32_t count);
```

数据：环境字符串数组。

### Task::Signal

这个类表示一个信号的处理函数、屏蔽集、标志和返回入口。getter 读取本地字段，setter 修改本地字段；Task::querySignalAction 读取服务中的配置，Task::setSignalAction 提交配置。SignalStack 继续单独表示备用信号栈。

#### getter

```cpp
uint64_t getHandler() const;
```

数据：信号处理入口地址。

```cpp
uint64_t getMask() const;
```

数据：信号屏蔽位集合。

```cpp
uint64_t getFlags() const;
```

数据：当前标志位。

```cpp
uint64_t getRestorer() const;
```

数据：信号处理结束时使用的返回入口。

#### setter

```cpp
void setHandler(uint64_t value);
```

数据：信号处理入口地址。

```cpp
void setMask(uint64_t value);
```

数据：信号屏蔽位集合。

```cpp
void setFlags(uint64_t value);
```

数据：资源标志位。

```cpp
void setRestorer(uint64_t value);
```

数据：信号处理结束时使用的返回入口。

### Task::SignalStack

这个类表示备用信号栈的地址、长度和设置。

#### getter

```cpp
uint64_t getAddress() const;
```

数据：映射开始的虚拟地址。

```cpp
uint64_t getSize() const;
```

数据：长度，单位为字节。

```cpp
uint32_t getFlags() const;
```

数据：当前标志位。

#### setter

```cpp
void setAddress(uint64_t value);
```

数据：映射开始的虚拟地址。

```cpp
void setSize(uint64_t value);
```

数据：长度，单位为字节。

```cpp
void setFlags(uint32_t value);
```

数据：资源标志位。

### Task::Child

这个类表示一次子进程退出或状态变化结果。

#### getter

```cpp
uint64_t getId() const;
```

数据：编号。

```cpp
uint32_t getUid() const;
```

数据：真实用户编号。

```cpp
uint32_t getCode() const;
```

数据：子进程事件原因。

```cpp
int32_t getStatus() const;
```

数据：异步文件操作的完成状态。

### Task::Core

这个类同时保存 core 转储配置和一次转储的进度、结果，不再拆分配置类和结果类。启用状态、大小上限、并发上限和超时可以在本地设置，再由 Task::setCorePolicy 提交；Task::queryCorePolicy 只更新配置字段，不生成转储，也不覆盖已有转储结果。编号、所属进程、开始时间、写出字节数、截止时间、触发信号、执行阶段和截断状态由服务查询结果填写，不提供普通 setter。构造只初始化本地字段，不发送请求。

#### getter

```cpp
bool getEnabled() const;
```

数据：是否启用 core 转储。

```cpp
uint64_t getMaximumBytes() const;
```

数据：单份 core 转储的最大字节数。

```cpp
uint64_t getMaximumConcurrent() const;
```

数据：允许同时进行的 core 转储数量。

```cpp
uint64_t getTimeoutMilliseconds() const;
```

数据：core 操作超时毫秒数。

```cpp
uint64_t getId() const;
```

数据：编号。

```cpp
uint64_t getTaskId() const;
```

数据：所属或对应的进程编号。

```cpp
uint64_t getStarted() const;
```

数据：core 操作开始时间。

```cpp
uint64_t getBytes() const;
```

数据：已生成的 core 字节数。

```cpp
uint64_t getDeadline() const;
```

数据：操作截止时间。

```cpp
uint32_t getSignal() const;
```

数据：相关信号编号。

```cpp
bool getTruncated() const;
```

数据：core 内容是否被截断。

```cpp
Task::Core::State getState() const;
```

数据：本次 core 转储所处阶段。

#### setter

```cpp
void setEnabled(bool value);
```

数据：是否启用 core 转储。

```cpp
void setMaximumBytes(uint64_t value);
```

数据：单份 core 转储的最大字节数。

```cpp
void setMaximumConcurrent(uint64_t value);
```

数据：允许同时进行的 core 转储数量。

```cpp
void setTimeoutMilliseconds(uint64_t value);
```

数据：core 操作超时毫秒数。

### Task::ImageMapping

这个类表示已装载映像的路径与虚拟地址范围。

#### getter

```cpp
uint64_t getBegin() const;
```

数据：映像映射开始地址。

```cpp
uint64_t getEnd() const;
```

数据：映像映射结束地址。

```cpp
uint64_t getOffset() const;
```

数据：当前偏移位置。

```cpp
const char* getPath() const;
```

数据：对象对应的路径。

## 实现文件

`lib/os/task.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
